#pragma once
#include "CoreMinimal.h"
#include "RHI.h"
#include "HitProxies.h"
#include "ShaderBaseClasses.h"
#include "MeshPassProcessor.h"
#include "ShaderParameterMacros.h"
#include "UniformBuffer.h"
#include "MeshDrawCommands.h"


BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FMobileRainDepthPassUniformParameters, )
	SHADER_PARAMETER_STRUCT(FSceneTexturesUniformParameters, SceneTextures)
	SHADER_PARAMETER(FMatrix, ProjectionMatrix)
	SHADER_PARAMETER(FMatrix, ViewMatrix)
	SHADER_PARAMETER(FVector4, DepthTexSizeAndInvSize)
	SHADER_PARAMETER(FVector, ViewPosition)
	SHADER_PARAMETER(float, MaxDepthSize)
	SHADER_PARAMETER(float, MaxDistance)
	SHADER_PARAMETER(float, BiasOffset)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

//inline void SetupRainDepthPassUniformBuffer(FRainDepthProjectedInfo* Info, FRHICommandListImmediate& RHICmdList, const FViewInfo& View, FMobileRainDepthPassUniformParameters& RainDepthPassParameters);

class FRainDepthProjectedInfo
{
public:
	FRainDepthProjectedInfo(){};
	~FRainDepthProjectedInfo();

	FViewInfo* RainDepthView = nullptr;

	TUniformBufferRef<FMobileRainDepthPassUniformParameters> MobileRainDepthPassUniformBuffer;

	FMatrix ProjectionMatrix;

	FMatrix ViewMatrix;
	
	FBox RainCaptureBound;

	FConvexVolume RainDepthFrustum;

	TRefCountPtr<IPooledRenderTarget> RainDepthRenderTarget;

	FParallelMeshDrawCommandPass RainDepthCommandPass;

	int32 DepthResolutionX = 512;
	int32 DepthResolutionY = 512;

	ERHIFeatureLevel::Type FeatureLevel = ERHIFeatureLevel::ES3_1;

	void Init(FSceneRenderer& Renderer, const  FViewInfo* View);

	void Reset();

	void AllocateRainDepthTargets(FRHICommandListImmediate& RHICmdList);

	void GatherDynamicMeshElements(FSceneRenderer& Renderer, TArray<const FSceneView*>& ReusedViewsArray,
		FGlobalDynamicIndexBuffer& DynamicIndexBuffer, FGlobalDynamicVertexBuffer& DynamicVertexBuffer, FGlobalDynamicReadBuffer& DynamicReadBuffer);

	void GatherDynamicMeshElementsArray(
		FViewInfo* View,
		FSceneRenderer& Renderer,
		FGlobalDynamicIndexBuffer& DynamicIndexBuffer,
		FGlobalDynamicVertexBuffer& DynamicVertexBuffer,
		FGlobalDynamicReadBuffer& DynamicReadBuffer,
		const TArray<const FPrimitiveSceneInfo*,SceneRenderingAllocator>& PrimitiveArray,
		const TArray<const FSceneView*>& ReusedViewsArray,
		TArray<FMeshBatchAndRelevance, SceneRenderingAllocator>& OutDynamicMeshElements,
		int32& OutNumDynamicSubjectMeshElements);

	void AddCachedMeshDrawCommandsForPass(int32 PrimitiveIndex,
		const FPrimitiveSceneInfo* InPrimitiveSceneInfo,
		const FStaticMeshBatchRelevance& RESTRICT StaticMeshRelevance,
		const FStaticMeshBatch& StaticMesh,
		const FScene* Scene,
		EMeshPass::Type PassType,
		FMeshCommandOneFrameArray& VisibleMeshCommands,
		TArray<const FStaticMeshBatch*, SceneRenderingAllocator>& MeshCommandBuildRequests,
		int32& NumMeshCommandBuildRequestElements);

	void AddProjectedPrimitive(FPrimitiveSceneInfo* PrimitiveSceneInfo, FViewInfo* View, ERHIFeatureLevel::Type FeatureLevel);

	bool ShouldDrawStaticMeshes(FViewInfo& InView, FPrimitiveSceneInfo* InPrimitiveSceneInfo);

	void SetupMeshDrawCommandsForRainDepth(FSceneRenderer& Renderer, FRHIUniformBuffer* PassUniformBuffer);

	void RenderRainDepthInner(FRHICommandListImmediate& RHICmdList, FSceneRenderer* SceneRenderer);

public:
	TArray<FPrimitiveSceneInfo*, SceneRenderingAllocator> CachedPrimitives;

	/** dynamic rain peojected elements */
	TArray<const FPrimitiveSceneInfo*,SceneRenderingAllocator> DynamicProjectedPrimitives;
	
	TArray<FMeshBatchAndRelevance, SceneRenderingAllocator> DynamicProjectedMeshElements;
	
	TArray<FMeshBatchAndRelevance, SceneRenderingAllocator> DynamicProjectedTranslucentMeshElements;

	TArray<const FStaticMeshBatch*, SceneRenderingAllocator> ProjectedMeshCommandBuildRequests;
	
	int32 NumDynamicProjectedMeshElements = 0;
	
	int32 NumProjectedMeshCommandBuildRequestElements = 0;

	FMeshCommandOneFrameArray RainDepthPassVisibleCommands;
};


class FRainDepthPassMeshProcessor : public FMeshPassProcessor
{
public:

	FRainDepthPassMeshProcessor(const FScene* Scene,
		const FSceneView* InViewIfDynamicMeshCommand,
		const FMeshPassProcessorRenderState& InPassDrawRenderState,
		FMeshPassDrawListContext* InDrawListContext);

	virtual void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) override final;

private:
	template<bool bPositionOnly>
	void Process(
		const FMeshBatch& MeshBatch,
		uint64 BatchElementMask,
		int32 StaticMeshId,
		EBlendMode BlendMode,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
		const FMaterial& RESTRICT MaterialResource,
		ERasterizerFillMode MeshFillMode,
		ERasterizerCullMode MeshCullMode);

	FMeshPassProcessorRenderState PassDrawRenderState;
};