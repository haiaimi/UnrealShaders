  UE4中Shader有GlobalShader，MaterialShader，MeshMaterialShader，其中GlobalShader是相对容易操作的，而后面个就相对比较复杂，下面主要就是渲染Mesh的大体流程。

1. 首先要知道FPrimitiveSceneProxy，它是图元渲染代理对应于UPrimitiveComponent组件，用于渲染线程，在向场景中添加图元时会调用FScene::AddPrimitive(UPrimitiveComponent* Primitive)，在该函数中会用逻辑线程中的UPrimitiveComponent创建FPrimitiveSceneProxy，代码如下：
    ```cpp
        // Create the primitive's scene proxy.
        FPrimitiveSceneProxy* PrimitiveSceneProxy = Primitive->CreateSceneProxy();
        Primitive->SceneProxy = PrimitiveSceneProxy;
    ```
    在AddPrimitive函数中还会在渲染线程中调用
    FScene::AddPrimitiveSceneInfo_RenderThread，该函数就是往FScene中填充渲染所需要的信息，如下代码：
    ```cpp
        Primitives.Add(PrimitiveSceneInfo);
        const FMatrix LocalToWorld = PrimitiveSceneInfo->Proxy->GetLocalToWorld();
        PrimitiveTransforms.Add(LocalToWorld);
        PrimitiveSceneProxies.Add(PrimitiveSceneInfo->Proxy);
        PrimitiveBounds.AddUninitialized();
        PrimitiveFlagsCompact.AddUninitialized();
        PrimitiveVisibilityIds.AddUninitialized();
        PrimitiveOcclusionFlags.AddUninitialized();
        PrimitiveComponentIds.AddUninitialized();
        PrimitiveOcclusionBounds.AddUninitialized();
    ```
    然后还需要执行 PrimitiveSceneInfo->AddToScene(RHICmdList, true);此方法主要就是把上面几个未初始化的内容，继续填充。同时添加渲染StaticMesh相关的内容，往FPrimitiveSceneInfo中的StaticMeshes和StaticMeshRelevances成员填充数据，StaticMesh不需要每帧变动，调用FPrimitiveSceneInfo::CacheMeshDrawCommands来缓存数据。

2. 上面的步骤中，已经对各个PrimitiveSceneInfo填充信息，下面就是对这些数据的统一获取数据，这就需要 FMeshElementCollector，就是对各个PrimitiveSceneInfo进行搜集。
    大致步骤在DeferredShadingRenderer中：InitViews->ComputeViewVisibility->GatherDynamicMeshElements 经过这些步骤就算是把所有的MeshBatch收集完。

3. 下面还有FSceneRenderer::SetupMeshPass，主要有一下的流程：
	+ 创建FMeshPassProcessor
	+ 获取对应Pass的FParallelMeshDrawCommandPass，然后调用FParallelMeshDrawCommandPass::DispatchPassSetup
	+ FParallelMeshDrawCommandPass中会有一个FMeshDrawCommandPassSetupTaskContext类型的成员变量，用于保存后面执行Task所需要的信息
	+ 创建一个FDrawCommandPassSetupTask来执行渲染准备操作，调用其中的AnyThreadTask()，
	+ AnyThreadTask会调用其中指定的方法，如果非移动端Pass，调用GenerateDynamicMeshDrawCommands
	+ GenerateDynamicMeshDrawCommands函数中会调用FMeshPassProcessor的AddMeshBatch函数
	+ AddMeshBatch会调用其中的Process函数，Process会调用FMeshPassProcessor::BuildMeshDrawCommands从而进行对不同类型Shader的绑定对应的资源的绑定，绑定详细见  FMeshDrawSingleShaderBindings
	+ 上述流程已经把渲染所需要的所有资源准备完成，接下来就是执行渲染工作
4. 实际调度渲染操作，在UE4的渲染器中如DeferredShadingRenderer 会调用对应的FParallelMeshDrawCommandPass::DispatchDraw方法进行绘制，代码如下：
    ```cpp
        inline FMeshDraw 
        void FDeferredShadingSceneRenderer::RenderBasePassViewParallel(FViewInfo& View, FRHICommandListImmediate& ParentCmdList, FExclusiveDepthStencil::Type BasePassDepthStencilAccess, const FMeshPassProcessorRenderState& InDrawRenderState)
        {
                …
            View.ParallelMeshDrawCommandPasses[EMeshPass::BasePass].DispatchDraw(&ParallelSet, ParentCmdList);
        }
    ```
    在调用DispatchDraw时会调用FRHICommandList中的资源设置方法以及绘制的方法，如SetShaderUniformBuffer、SetShaderSampler、SetShaderTexture以及SetShaderResourceViewParameter，绘制的时候就调用DrawIndexedPrimitive / DrawPrimitive。注意在渲染的时候会使用到这一Pass的FMeshDrawCommand，这些Command会在DispatchPassSetup阶段传入进去，而传入的Command比较难找到生成的位置。

    ## StaticMesh和DynamicMesh
    这是个比较重要的地方，上面也稍微提到过，StaticMesh的MeshProcessor和DynamicMesh的生成规则不太一样，DynamicMesh需要每帧生成，而StaticMesh会有一个缓存，FPrimitiveSceneInfo会有一个函数用于生成，如下:
    ```cpp
    void FPrimitiveSceneInfo::CacheMeshDrawCommands(FRHICommandListImmediate& RHICmdList)
    {
        FCachedMeshDrawCommandInfo CommandInfo;
        CommandInfo.MeshPass = PassType;

        FCachedPassMeshDrawList& SceneDrawList = Scene->CachedDrawLists[PassType];
        FCachedPassMeshDrawListContext CachedPassMeshDrawListContext(CommandInfo, SceneDrawList, *Scene);

        PassProcessorCreateFunction CreateFunction = FPassProcessorManager::GetCreateFunction(ShadingPath, PassType);
        FMeshPassProcessor* PassMeshProcessor = CreateFunction(Scene, nullptr, &CachedPassMeshDrawListContext);

        if (PassMeshProcessor != nullptr)
        {
            check(!Mesh.bRequiresPerElementVisibility);
            uint64 BatchElementMask = ~0ull;
            //生成DrawCommand的位置
            PassMeshProcessor->AddMeshBatch(Mesh, BatchElementMask, Proxy);

            PassMeshProcessor->~FMeshPassProcessor();
        }

        if (CommandInfo.CommandIndex != -1 || CommandInfo.StateBucketId != -1)
        {
            static_assert(sizeof(MeshRelevance.CommandInfosMask) * 8 >= EMeshPass::Num, "CommandInfosMask is too small to contain all mesh passes.");

            MeshRelevance.CommandInfosMask.Set(PassType);
            StaticMeshCommandInfos.Add(CommandInfo);
        }
    }
    ```
简单理一下调用这个这个函数的顺序：
```cpp
//First
void FPrimitiveSceneInfo::AddToScene(FRHICommandListImmediate& RHICmdList, bool bUpdateStaticDrawLists, bool bAddToStaticDrawLists)
//Second
void FPrimitiveSceneInfo::AddStaticMeshes(FRHICommandListImmediate& RHICmdList, 
//Third
bool bAddToStaticDrawLists)
void CacheMeshDrawCommands(FRHICommandListImmediate& RHICmdList);
```
那么在游戏运行期添加DrawCommand的顺序：
```cpp
//First
bool FDeferredShadingSceneRenderer::InitViews(FRHICommandListImmediate& RHICmdList, FExclusiveDepthStencil::Type BasePassDepthStencilAccess, struct FILCUpdatePrimTaskData& ILCTaskData, FGraphEventArray& UpdateViewCustomDataEvents);
//Second
void FSceneRenderer::ComputeViewVisibility(FRHICommandListImmediate& RHICmdList, FExclusiveDepthStencil::Type BasePassDepthStencilAccess, FViewVisibleCommandsPerView& ViewCommandsPerView, 
	FGlobalDynamicIndexBuffer& DynamicIndexBuffer, FGlobalDynamicVertexBuffer& DynamicVertexBuffer, FGlobalDynamicReadBuffer& DynamicReadBuffer);
//Third global function
ComputeAndMarkRelevanceForViewParallel(...)
{
    if (StaticMeshCommandInfoIndex >= 0)
			{
                //获取已经缓存的DrawCommand
				const FCachedMeshDrawCommandInfo& CachedMeshDrawCommand = InPrimitiveSceneInfo->StaticMeshCommandInfos[StaticMeshCommandInfoIndex];
				const FCachedPassMeshDrawList& SceneDrawList = Scene->CachedDrawLists[PassType];

				FVisibleMeshDrawCommand NewVisibleMeshDrawCommand;

				const FMeshDrawCommand* MeshDrawCommand = CachedMeshDrawCommand.StateBucketId >= 0
					? &Scene->CachedMeshDrawCommandStateBuckets[FSetElementId::FromInteger(CachedMeshDrawCommand.StateBucketId)].MeshDrawCommand
					: &SceneDrawList.MeshDrawCommands[CachedMeshDrawCommand.CommandIndex];

				NewVisibleMeshDrawCommand.Setup(
					MeshDrawCommand,
					PrimitiveIndex,
					CachedMeshDrawCommand.StateBucketId,
					CachedMeshDrawCommand.MeshFillMode,
					CachedMeshDrawCommand.MeshCullMode,
					CachedMeshDrawCommand.SortKey);

				VisibleCachedDrawCommands[(uint32)PassType].Add(NewVisibleMeshDrawCommand);
			}
}

//Fourth global function
void MarkRelevant()
//Fifth
void FDrawCommandRelevancePacket::AddCommandsForMesh()
```

而DynamicMesh的MeshProcessor需要动态生成，大致的调用流程：
```cpp
//First 获取所用的DynamicMesh
void FSceneRenderer::GatherDynamicMeshElements()
//Second 
void FSceneRenderer::SetupMeshPass(FViewInfo& View, FExclusiveDepthStencil::Type BasePassDepthStencilAccess, FViewCommands& ViewCommands)
{
    if ((FPassProcessorManager::GetPassFlags(ShadingPath, PassType) & EMeshPassFlags::MainView) != EMeshPassFlags::None)
		{
			// Mobile: BasePass and MobileBasePassCSM lists need to be merged and sorted after shadow pass.
			if (ShadingPath == EShadingPath::Mobile && (PassType == EMeshPass::BasePass || PassType == EMeshPass::MobileBasePassCSM))
			{
				continue;
			}

			PassProcessorCreateFunction CreateFunction = FPassProcessorManager::GetCreateFunction(ShadingPath, PassType);
            //创建Processor
			FMeshPassProcessor* MeshPassProcessor = CreateFunction(Scene, &View, nullptr);

			FParallelMeshDrawCommandPass& Pass = View.ParallelMeshDrawCommandPasses[PassIndex];

			if (ShouldDumpMeshDrawCommandInstancingStats())
			{
				Pass.SetDumpInstancingStats(GetMeshPassName(PassType));
			}

            //创建MeshCommand
			Pass.DispatchPassSetup(
				Scene,
				View,
				PassType,
				BasePassDepthStencilAccess,
				MeshPassProcessor,
				View.DynamicMeshElements,
				&View.DynamicMeshElementsPassRelevance,
				View.NumVisibleDynamicMeshElements[PassType],
				ViewCommands.DynamicMeshCommandBuildRequests[PassType],
				ViewCommands.NumDynamicMeshCommandBuildRequestElements[PassType],
				ViewCommands.MeshCommands[PassIndex]);
		}
}
```