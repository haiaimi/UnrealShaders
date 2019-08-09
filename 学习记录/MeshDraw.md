  UE4中Shader有GlobalShader，MaterialShader，MeshMaterialShader，其中GlobalShader是相对容易操作的，而后面个就相对比较复杂，下面主要就是渲染Mesh的大体流程。

1. 首先要知道FPrimitiveSceneProxy，它是图元渲染代理对应于UPrimitiveComponent组件，用于渲染线程，在向场景中添加图元时会调用FScene::AddPrimitive(UPrimitiveComponent* Primitive)，在该函数     中会用逻辑线程中的UPrimitiveComponent创建FPrimitiveSceneProxy，代码如下：
    ```
        // Create the primitive's scene proxy.
        FPrimitiveSceneProxy* PrimitiveSceneProxy = Primitive->CreateSceneProxy();
        Primitive->SceneProxy = PrimitiveSceneProxy;
    ```
    在AddPrimitive函数中还会在渲染线程中调用
    FScene::AddPrimitiveSceneInfo_RenderThread，该函数就是网FScene中填充渲染所需要的信息，如下代码：
    ```
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
    然后还需要执行 PrimitiveSceneInfo->AddToScene(RHICmdList, true);此方法主要就是把上面几个未初始化的内容，继续填充。同时添加渲染StaticMesh相关的内容，往FPrimitiveSceneInfo中的 StaticMeshes和StaticMeshRelevances成员填充数据。

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
    ```
        inline FMeshDraw 
        void FDeferredShadingSceneRenderer::RenderBasePassViewParallel(FViewInfo& View, FRHICommandListImmediate& ParentCmdList, FExclusiveDepthStencil::Type BasePassDepthStencilAccess, const FMeshPassProcessorRenderState& InDrawRenderState)
        {
                …
            View.ParallelMeshDrawCommandPasses[EMeshPass::BasePass].DispatchDraw(&ParallelSet, ParentCmdList);
        }
    ```
    在调用DispatchDraw时会调用FRHICommandList中的资源设置方法以及绘制的方法，如SetShaderUniformBuffer、SetShaderSampler、SetShaderTexture以及SetShaderResourceViewParameter，绘制的时候就调用DrawIndexedPrimitive / DrawPrimitive。注意在渲染的时候会使用到这一Pass的FMeshDrawCommand，这些Command会在DispatchPassSetup阶段传入进去，而传入的Command比较难找到生成的位置
