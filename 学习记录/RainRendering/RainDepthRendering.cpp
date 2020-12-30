#include "RainDepthRendering.h"
#include "PrimitiveViewRelevance.h"

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FMobileRainDepthPassUniformParameters, "MobileRainDepthPass");

IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, TRainDepthVS<true>, TEXT("/Engine/Private/RainDepthOnlyVertexShader.usf"), TEXT("PositionOnlyMain"), SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, TRainDepthVS<false>, TEXT("/Engine/Private/RainDepthOnlyVertexShader.usf"), TEXT("Main"), SF_Vertex);

IMPLEMENT_MATERIAL_SHADER_TYPE(, FRainDepthPS<true>,TEXT("/Engine/Private/RainDepthOnlyPixelShader.usf"),TEXT("Main"),SF_Pixel);

FRainDepthPassMeshProcessor::FRainDepthPassMeshProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, /*const FMeshPassProcessorRenderState& InPassDrawRenderState,*/ FMeshPassDrawListContext* InDrawListContext) :
             FMeshPassProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, InDrawListContext)
{
	//PassDrawRenderState = InPassDrawRenderState;
	PassDrawRenderState.SetViewUniformBuffer(Scene->UniformBuffers.ViewUniformBuffer);
	PassDrawRenderState.SetInstancedViewUniformBuffer(Scene->UniformBuffers.InstancedViewUniformBuffer);
	// #TODO Need to set custom rain depth uniform buffer
	PassDrawRenderState.SetPassUniformBuffer(Scene->UniformBuffers.MobileRainDepthPassUniformBuffer);
}

void FRainDepthPassMeshProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId /*= -1*/)
{
	//bool bDraw = MeshBatch.bUseForRainDepthPass;
	bool bDraw = MeshBatch.bUseForDepthPass;
	
	if (bDraw && bRespectUseAsOccluderFlag)
	{
		if (PrimitiveSceneProxy)
		{
			// Only render primitives marked as occluders.
			bDraw = PrimitiveSceneProxy->ShouldUseAsOccluder()
				// Only render static objects unless movable are requested.
				&& (!PrimitiveSceneProxy->IsMovable() || bEarlyZPassMovable);

			// Filter dynamic mesh commands by screen size.
			if (ViewIfDynamicMeshCommand)
			{
				extern float GMinScreenRadiusForDepthPrepass;
				const float LODFactorDistanceSquared = (PrimitiveSceneProxy->GetBounds().Origin - ViewIfDynamicMeshCommand->ViewMatrices.GetViewOrigin()).SizeSquared() * FMath::Square(ViewIfDynamicMeshCommand->LODDistanceFactor);
				bDraw = bDraw && FMath::Square(PrimitiveSceneProxy->GetBounds().SphereRadius) > GMinScreenRadiusForDepthPrepass * GMinScreenRadiusForDepthPrepass * LODFactorDistanceSquared;
			}
		}
		else
		{
			bDraw = false;
		}
	}
	bDraw = true;

	if (bDraw)
	{
		// Determine the mesh's material and blend mode.
		const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = nullptr;
		const FMaterial& Material = MeshBatch.MaterialRenderProxy->GetMaterialWithFallback(FeatureLevel, FallbackMaterialRenderProxyPtr);

		const FMaterialRenderProxy& MaterialRenderProxy = FallbackMaterialRenderProxyPtr ? *FallbackMaterialRenderProxyPtr : *MeshBatch.MaterialRenderProxy;

		const EBlendMode BlendMode = Material.GetBlendMode();
		const FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(MeshBatch);
		const ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, Material, OverrideSettings);
		const ERasterizerCullMode MeshCullMode = ComputeMeshCullMode(MeshBatch, Material, OverrideSettings);
		const bool bIsTranslucent = IsTranslucentBlendMode(BlendMode);

		if (!bIsTranslucent
			&& (!PrimitiveSceneProxy /*|| PrimitiveSceneProxy->ShouldRenderInDepthPass()*/)
			&& ShouldIncludeDomainInMeshPass(Material.GetMaterialDomain())
			&& ShouldIncludeMaterialInDefaultOpaquePass(Material))
		{
			// No mask material
			if (BlendMode == BLEND_Opaque
				&& MeshBatch.VertexFactory->SupportsPositionOnlyStream()
				&& !Material.MaterialModifiesMeshPosition_RenderThread()
				&& Material.WritesEveryPixel())
			{
				const FMaterialRenderProxy& DefaultProxy = *UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy();
				const FMaterial& DefaultMaterial = *DefaultProxy.GetMaterial(FeatureLevel);
				Process<true>(MeshBatch, BatchElementMask, StaticMeshId, BlendMode, PrimitiveSceneProxy, DefaultProxy, DefaultMaterial, MeshFillMode, MeshCullMode);
			}
			// Support for mask material
			else
			{
				const bool bMaterialMasked = !Material.WritesEveryPixel() || Material.IsTranslucencyWritingCustomDepth();
				if(bMaterialMasked)
				{
					const FMaterialRenderProxy* EffectiveMaterialRenderProxy = &MaterialRenderProxy;
					const FMaterial* EffectiveMaterial = &Material;

					if (!bMaterialMasked && !Material.MaterialModifiesMeshPosition_RenderThread())
					{
						// Override with the default material for opaque materials that are not two sided
						EffectiveMaterialRenderProxy = UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy();
						EffectiveMaterial = EffectiveMaterialRenderProxy->GetMaterial(FeatureLevel);
					}

					Process<false>(MeshBatch, BatchElementMask, StaticMeshId, BlendMode, PrimitiveSceneProxy, *EffectiveMaterialRenderProxy, *EffectiveMaterial, MeshFillMode, MeshCullMode);
				}
			}
		}
	}
}

template<bool bPositionOnly>
void FRainDepthPassMeshProcessor::Process(const FMeshBatch& MeshBatch, uint64 BatchElementMask, int32 StaticMeshId, EBlendMode BlendMode, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, const FMaterialRenderProxy& RESTRICT MaterialRenderProxy, const FMaterial& RESTRICT MaterialResource, ERasterizerFillMode MeshFillMode, ERasterizerCullMode MeshCullMode)
{
	const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;

	TMeshProcessorShaders<
		TRainDepthVS<bPositionOnly>,
		FBaseHS,
		FBaseDS,
		FRainDepthPS> RainDepthPassShaders;

	FShaderPipelineRef ShaderPipeline;

	GetRainDepthPassShaders(
		MaterialResource,
		VertexFactory->GetType(),
		FeatureLevel,
		RainDepthPassShaders.HullShader,
		RainDepthPassShaders.DomainShader,
		RainDepthPassShaders.VertexShader,
		RainDepthPassShaders.PixelShader,
		ShaderPipeline
		);

	FMeshPassProcessorRenderState DrawRenderState(PassDrawRenderState);

	SetDepthPassDitheredLODTransitionState(ViewIfDynamicMeshCommand, MeshBatch, StaticMeshId, DrawRenderState);

	FDepthOnlyShaderElementData ShaderElementData(0.0f);
	ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, true);

	const FMeshDrawCommandSortKey SortKey = CalculateDepthPassMeshStaticSortKey(BlendMode, RainDepthPassShaders.VertexShader.GetShader(), RainDepthPassShaders.PixelShader.GetShader());

	BuildMeshDrawCommands(
		MeshBatch,
		BatchElementMask,
		PrimitiveSceneProxy,
		MaterialRenderProxy,
		MaterialResource,
		DrawRenderState,
		RainDepthPassShaders,
		MeshFillMode,
		MeshCullMode,
		SortKey,
		bPositionOnly ? EMeshPassFeatures::PositionOnly : EMeshPassFeatures::Default,
		ShaderElementData);
}

void SetupRainDepthPassUniformBuffer(const FProjectedShadowInfo* ShadowInfo, FRHICommandListImmediate& RHICmdList, const FViewInfo& View, FMobileRainDepthPassUniformParameters& RainDepthPassParameters)
{
	FSceneRenderTargets& SceneRenderTargets = FSceneRenderTargets::Get(RHICmdList);
	SetupSceneTextureUniformParameters(SceneRenderTargets, View.FeatureLevel, ESceneTextureSetupMode::None, RainDepthPassParameters.SceneTextures);

	// #TODO
	RainDepthPassParameters.ProjectionMatrix;
	RainDepthPassParameters.ViewMatrix;
}


template<bool bPositionOnly>
void GetRainDepthPassShaders(const FMaterial& Material, FVertexFactoryType* VertexFactoryType, ERHIFeatureLevel::Type FeatureLevel, TShaderRef<FBaseHS>& HullShader, TShaderRef<FBaseDS>& DomainShader, TShaderRef<TRainDepthVS<bPositionOnly>>> &VertexShader, TShaderRef<FRainDepthPS>& PixelShader, FShaderPipelineRef& ShaderPipeline)
{
	
}

FMeshPassProcessor* CreateRainDepthPassProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
{
	FRHIUniformBuffer* PassUniformBuffer = nullptr;

	EShadingPath ShadingPath = Scene->GetShadingPath();
	if (ShadingPath == EShadingPath::Mobile)
	{
		PassUniformBuffer = Scene->UniformBuffers.MobileRainDepthPassUniformBuffer;
	}

	return new(FMemStack::Get()) FRainDepthPassMeshProcessor(Scene, InViewIfDynamicMeshCommand, InDrawListContext);
}

FRegisterPassProcessorCreateFunction RegisterRainDepthPass(&CreateRainDepthPassProcessor, EShadingPath::Mobile, EMeshPass::RainDepthPass, EMeshPassFlags::CachedMeshCommands);

/////////////////////////////////////////////////////////////////////////////////////////////////////

class FRainFrustumIntersectTask
{
public:
	FRainFrustumIntersectTask(FRainDepthProjectedInfo& Info, FScenePrimitiveOctree::FNode* PrimitiveNode, FViewInfo* InViewInfo):
		RainProjectedInfo(Info),
		Node(PrimitiveNode),
		ViewInfo(InViewInfo)
	{
		
	}

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FRainFrustumIntersectTask, STATGROUP_TaskGraphTasks);
	}

	ENamedThreads::Type GetDesiredThread()
	{
		return ENamedThreads::GetRenderThread_Local();
	}

	static ESubsequentsMode::Type GetSubsequentsMode()
	{
		return ESubsequentsMode::TrackSubsequents;
	}

	void AnyThreadTask()
	{
		if (Node)
		{
			for (FScenePrimitiveOctree::ElementConstIt NodePrimitiveIt(Node->GetElementIt()); NodePrimitiveIt; ++NodePrimitiveIt)
			{
				if (RainProjectedInfo.RainDepthFrustum.IntersectBox(NodePrimitiveIt.Bounds.Origin, NodePrimitiveIt.Bounds.BoxExtent))
				{
					RainProjectedInfo.AddProjectedPrimitive(NodePrimitiveIt.PrimitiveSceneInfo, ViewInfo, ViewInfo->GetFeatureLevel());
				}
			}
		}
	}

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		AnyThreadTask();
	}

private:
	FRainDepthProjectedInfo& RainProjectedInfo;
	FScenePrimitiveOctree::FNode* Node = nullptr;
	FViewInfo* ViewInfo;
};


void GatherRainDepthPrimitives(FRainDepthProjectedInfo& RainProjectedInfo, FScene* Scene)
{
	//FScene* Scene = nullptr;
	TArray<FRainFrustumIntersectTask*, SceneRenderingAllocator> Packets;
	//FRainDepthProjectedInfo RainProjectedInfo;

	Packets.Reserve(100);

	// Find primitives that are in a shadow frustum in the octree.
	for (FScenePrimitiveOctree::TConstIterator<SceneRenderingAllocator> PrimitiveOctreeIt(Scene->PrimitiveOctree); PrimitiveOctreeIt.HasPendingNodes(); PrimitiveOctreeIt.Advance())
	{
		const FScenePrimitiveOctree::FNode& PrimitiveOctreeNode = PrimitiveOctreeIt.GetCurrentNode();
		const FOctreeNodeContext& PrimitiveOctreeNodeContext = PrimitiveOctreeIt.GetCurrentContext();
		
		FOREACH_OCTREE_CHILD_NODE(ChildRef)
		{
			if (PrimitiveOctreeNode.HasChild(ChildRef))
			{
				const FOctreeNodeContext ChildContext = PrimitiveOctreeNodeContext.GetChildContext(ChildRef);
				bool bIsInFrustum = false;
				if (RainProjectedInfo.RainDepthFrustum.IntersectBox(ChildContext.Bounds.Center, ChildContext.Bounds.Extent))
				{
					bIsInFrustum = true;
				}
			}
		}

		if (bIsInFrustum)
		{
			// If the child node was in the frustum of at least one preshadow, push it on
			// the iterator's pending node stack.
			PrimitiveOctreeIt.PushChild(ChildRef);
		}

		if (PrimitiveOctreeNode.GetElementCount() > 0)
		{
			FRainFrustumIntersectTask* Packet = new(FMemStack::Get()) FRainFrustumIntersectTask(RainProjectedInfo, &PrimitiveOctreeNode, RainProjectedInfo.RainDepthView);
			Packets.Add(Packet);
		}
	}

	ParallelFor(Packets.Num(),
		[&Packets](int32 Index)
		{
			Packets[Index]->AnyThreadTask();
		},
			false
		);
}

void FRainDepthProjectedInfo::GatherDynamicMeshElements(FSceneRenderer& Renderer, TArray<const FSceneView*>& ReusedViewsArray, FGlobalDynamicIndexBuffer& DynamicIndexBuffer, FGlobalDynamicVertexBuffer& DynamicVertexBuffer, FGlobalDynamicReadBuffer& DynamicReadBuffer)
{
	if (DynamicProjectedPrimitives.Num() > 0 || DynamicProjectedTranslucentMeshElements.Num() > 0)
	{
		ReusedViewsArray[0] = RainDepthView;

		GatherDynamicMeshElementsArray(RainDepthView, Renderer, DynamicIndexBuffer, DynamicVertexBuffer, DynamicReadBuffer, DynamicProjectedPrimitives, ReusedViewsArray, DynamicProjectedMeshElements, NumDynamicProjectedMeshElements);

		Renderer.MeshCollector.ProcessTasks();
	}

	const EShadingPath ShadingPath = FSceneInterface::GetShadingPath(Renderer.FeatureLevel);
	FRHIUniformBuffer* PassUniformBuffer = nullptr;

	if (ShadingPath == EShadingPath::Mobile)
	{
		FMobileRainDepthPassUniformParameters RainDepthParameters;
		MobileRainDepthPassUniformBuffer = TUniformBufferRef<FMobileRainDepthPassUniformParameters>::CreateUniformBufferImmediate(RainDepthParameters, UniformBuffer_MultiFrame, EUniformBufferValidation::None);
		PassUniformBuffer = MobileRainDepthPassUniformBuffer;
	}

	SetupMeshDrawCommandsForRainDepth(Renderer, PassUniformBuffer);
}

void FRainDepthProjectedInfo::GatherDynamicMeshElementsArray(FViewInfo* View, FSceneRenderer& Renderer, FGlobalDynamicIndexBuffer& DynamicIndexBuffer, FGlobalDynamicVertexBuffer& DynamicVertexBuffer, FGlobalDynamicReadBuffer& DynamicReadBuffer, const TArray<const FPrimitiveSceneInfo*,SceneRenderingAllocator>& PrimitiveArray, const TArray<const FSceneView*>& ReusedViewsArray, TArray<FMeshBatchAndRelevance, SceneRenderingAllocator>& OutDynamicMeshElements, int32& OutNumDynamicSubjectMeshElements)
{
	FSimpleElementCollector DynamicSubjectSimpleElements;

	Renderer.MeshCollector.ClearViewMeshArrays();
	Renderer.MeshCollector.AddViewMeshArrays(
		View,
		&OutDynamicMeshElements,
		&DynamicSubjectSimpleElements,
		&View->DynamicPrimitiveShaderData,
		Renderer.ViewFamily.GetFeatureLevel(),
		&DynamicIndexBuffer,
		&DynamicVertexBuffer,
		&DynamicReadBuffer
	);

	const uint32 PrimitiveCount = PrimitiveArray.Num();

	for (uint32 PrimitiveIndex = 0; PrimitiveIndex < PrimitiveCount; ++PrimitiveIndex)
	{
		const FPrimitiveSceneInfo* PrimitiveSceneInfo = PrimitiveArray[PrimitiveIndex];
		const FPrimitiveSceneProxy* PrimitiveSceneProxy = PrimitiveSceneInfo->Proxy;

		FPrimitiveViewRelevance ViewRelevance = View->PrimitiveViewRelevanceMap[PrimitiveSceneInfo->GetIndex()];
		if (!ViewRelevance.bInitializedThisFrame)
		{
			ViewRelevance = PrimitiveSceneInfo->Proxy->GetViewRelevance(View);
		}

		// #TODO Add Rain depth specified relevance 
		if (ViewRelevance.bDynamicRelevance)
		{
			Renderer.MeshCollector.SetPrimitive(PrimitiveSceneProxy, PrimitiveSceneInfo->DefaultDynamicHitProxy);

			// Get dynamic mesh batch
			PrimitiveSceneInfo->Proxy->GetDynamicMeshElements(ReusedViewsArray, Renderer.ViewFamily, 0x1, Renderer.MeshCollector);
		}
	}
	OutNumDynamicSubjectMeshElements = Renderer.MeshCollector.GetMeshElementCount(0);
}

void FRainDepthProjectedInfo::AddCachedMeshDrawCommandsForPass(int32 PrimitiveIndex, const FPrimitiveSceneInfo* InPrimitiveSceneInfo, const FStaticMeshBatchRelevance& RESTRICT StaticMeshRelevance, const FStaticMeshBatch& StaticMesh, const FScene* Scene, EMeshPass::Type PassType, FMeshCommandOneFrameArray& VisibleMeshCommands, TArray<const FStaticMeshBatch*, SceneRenderingAllocator>& MeshCommandBuildRequests, int32& NumMeshCommandBuildRequestElements)
{
	const EShadingPath ShadingPath = Scene->GetShadingPath();
	const bool bUseCachedMeshCommand = UseCachedMeshDrawCommands()
		&& !!(FPassProcessorManager::GetPassFlags(ShadingPath, PassType) & EMeshPassFlags::CachedMeshCommands)
		&& StaticMeshRelevance.bSupportsCachingMeshDrawCommands;

	if (bUseCachedMeshCommand)
	{
		const int32 StaticMeshCommandInfoIndex = StaticMeshRelevance.GetStaticMeshCommandInfoIndex(PassType);
		if (StaticMeshCommandInfoIndex >= 0)
		{
			const FCachedMeshDrawCommandInfo& CachedMeshDrawCommand = InPrimitiveSceneInfo->StaticMeshCommandInfos[StaticMeshCommandInfoIndex];
			const FCachedPassMeshDrawList& SceneDrawList = Scene->CachedDrawLists[PassType];
			const FMeshDrawCommand* MeshDrawCommand = CachedMeshDrawCommand.StateBucketId >= 0 ? &Scene->CachedMeshDrawCommandStateBuckets[PassType].GetByElementId(CachedMeshDrawCommand.StateBucketId).Key : 
					&SceneDrawList.MeshDrawCommands[CachedMeshDrawCommand.CommandIndex];

			FVisibleMeshDrawCommand NewVisibleMeshDrawCommand;

			NewVisibleMeshDrawCommand.Setup(
				MeshDrawCommand, 
				PrimitiveIndex, 
				PrimitiveIndex, 
				CachedMeshDrawCommand.StateBucketId,
				CachedMeshDrawCommand.MeshFillMode,
				CachedMeshDrawCommand.MeshCullMode,
				CachedMeshDrawCommand.SortKey);

			VisibleMeshCommands.Add(NewVisibleMeshDrawCommand);
		}
		else
		{
			NumMeshCommandBuildRequestElements += StaticMeshRelevance.NumElements;
			MeshCommandBuildRequests.Add(&StaticMesh);
		}
	}
}

void FRainDepthProjectedInfo::AddProjectedPrimitive(FPrimitiveSceneInfo* PrimitiveSceneInfo, FViewInfo* View, ERHIFeatureLevel::Type FeatureLevel)
{
	if (PrimitiveSceneInfo && View)
	{
		int32 PrimitiveId = PrimitiveSceneInfo->GetIndex();
		const FPrimitiveSceneProxy* Proxy = PrimitiveSceneInfo->Proxy;

		FPrimitiveViewRelevance& ViewRelevance = View->PrimitiveViewRelevanceMap[PrimitiveId];

		bool bOpaque = false;
		bool bTranslucentRelevance = false;
		// #TODO
		bool bShouldReceiveRain = false; 

		bOpaque |= ViewRelevance.bOpaque || ViewRelevance.bMasked;
		bTranslucentRelevance |= ViewRelevance.HasTranslucency() && !ViewRelevance.bMasked;

		if (bOpaque)
		{
			bool bDrawingStaticMeshes = false;

			if (!View->PrimitiveVisibilityMap[PrimitiveId] || View->PrimitiveViewRelevanceMap[PrimitiveId].bStaticRelevance)
			{
				bDrawingStaticMeshes |= ShouldDrawStaticMeshes(*View, PrimitiveSceneInfo);
			}

			if (!bDrawingStaticMeshes)
			{
				DynamicProjectedPrimitives.Add(PrimitiveSceneInfo);
			}
		}
	}
}

bool FRainDepthProjectedInfo::ShouldDrawStaticMeshes(FViewInfo& InView, FPrimitiveSceneInfo* InPrimitiveSceneInfo)
{
	bool bDrawingStaticMeshes = false;
	int32 PrimitiveId = PrimitiveSceneInfo->GetIndex();

	const int32 ForcedLOD = InView.Family->EngineShowFlags.LOD ? GetCVarForceLOD() : -1;
	const FLODMask* VisiblePrimitiveLODMask = nullptr;

	// #TODO Use highest LOD for rendering
	int8 LODToRenderScan = 0;
	FLODMask LODToRender;

	LODToRender.SetLOD(LODToRenderScan);

	const bCanCache = !InPrimitiveSceneInfo->NeedsUpdateStaticMeshes();

	for (int32 MeshIndex = 0; MeshIndex < InPrimitiveSceneInfo->StaticMeshRelevances.Num(); MeshIndex++)
	{
		const FStaticMeshBatchRelevance& StaticMeshRelevance = InPrimitiveSceneInfo->StaticMeshRelevances[MeshIndex];
		const FStaticMeshBatch& StaticMesh = InPrimitiveSceneInfo->StaticMeshes[MeshIndex];

		if (LODToRender.ContainsLOD(StaticMeshRelevance.LODIndex))
		{
			if (bCanCache)
			{
				AddCachedMeshDrawCommandsForPass(
					PrimitiveId,
					InPrimitiveSceneInfo,
					StaticMeshRelevance,
					StaticMesh,
					InPrimitiveSceneInfo->Scene,
					EMeshPass::RainDepthPass,
					RainDepthPassVisibleCommands,
					ProjectedMeshCommandBuildRequests,
					NumProjectedMeshCommandBuildRequestElements);
			}
			else
			{
				NumProjectedMeshCommandBuildRequestElements += StaticMeshRelevance.NumElements;
				ProjectedMeshCommandBuildRequests.Add(&StaticMesh);
			}

			bDrawingStaticMeshes = true;
		}
	}

	return bDrawingStaticMeshes;
}

void FRainDepthProjectedInfo::SetupMeshDrawCommandsForRainDepth(FSceneRenderer& Renderer, FRHIUniformBuffer* PassUniformBuffer)
{
	FRainDepthPassMeshProcessor* MeshPassProcessor = new(FMemStack::Get()) FRainDepthPassMeshProcessor(Renderer.Scene, RainDepthView, nullptr);

	RainDepthCommandPass.DispatchPassSetup(
		Renderer.Scene,
		*RainDepthView,
		EMeshPass::Num,
		FExclusiveDepthStencil::DepthNop_StencilNop,
		MeshPassProcessor,
		DynamicProjectedMeshElements,
		nullptr,
		NumDynamicProjectedMeshElements,
		ProjectedMeshCommandBuildRequests,
		NumProjectedMeshCommandBuildRequestElements,
		RainDepthPassVisibleCommands);
}

void FRainDepthProjectedInfo::RenderRainDepthInner(FRHICommandListImmediate& RHICmdList, FSceneRenderer* SceneRenderer)
{
	if (FSceneInterface::GetShadingPath(FeatureLevel) == EShadingPath::Mobile)
	{
		FMobileRainDepthPassUniformParameters RainDepthPassParameters;
		SetupRainDepthPassUniformBuffer(this, RHICmdList, *RainDepthView, RainDepthPassParameters);
		SceneRenderer->Scene->UniformBuffers.MobileRainDepthPassUniformBuffer.UpdateUniformBufferImmediate(RainDepthPassParameters);
		MobileRainDepthPassUniformBuffer.UpdateUniformBufferImmediate(RainDepthPassParameters);
		PassUniformBuffer = SceneRenderer->Scene->UniformBuffers.MobileRainDepthPassUniformBuffer;

		RainDepthCommandPass.DispatchDraw(nullptr, RHICmdList);
	}
}

FRainDepthProjectedInfo GRainDepthProjectedInfo;

void FMobileSceneRenderer::InitRainDepthRendering(FGlobalDynamicIndexBuffer& DynamicIndexBuffer, FGlobalDynamicVertexBuffer& DynamicVertexBuffer, FGlobalDynamicReadBuffer& DynamicReadBuffer)
{
	GatherRainDepthPrimitives(GRainDepthProjectedInfo, Scene);

	TArray<const FSceneView*> ReusedViewsArray;
	ReusedViewsArray.AddZeroed(1);
	GRainDepthProjectedInfo.GatherDynamicMeshElements(*this, ReusedViewsArray, DynamicIndexBuffer, DynamicVertexBuffer, DynamicReadBuffer);
}

void FMobileSceneRenderer::RenderRainDepth(FRHICommandListImmediate& RHICmdList)
{
	GRainDepthProjectedInfo.RenderRainDepthInner(RHICmdList, this);
}