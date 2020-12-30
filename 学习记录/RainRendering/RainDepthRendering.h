#pragma once
#include "CoreMinimal.h"
#include "RHI.h"
#include "HitProxies.h"
#include "ShaderBaseClasses.h"
#include "MeshPassProcessor.h"
#include "ShaderParameterMacros.h"
#include "UniformBuffer.h"


BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FMobileRainDepthPassUniformParameters, )
	SHADER_PARAMETER_STRUCT(FSceneTexturesUniformParameters, SceneTextures)
	SHADER_PARAMETER(FMatrix, ProjectionMatrix)
	SHADER_PARAMETER(FMatrix, ViewMatrix)
	SHADER_PARAMETER(float, MaxDistance)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

void SetupRainDepthPassUniformBuffer(const FProjectedShadowInfo* ShadowInfo, FRHICommandListImmediate& RHICmdList, const FViewInfo& View, FMobileRainDepthPassUniformParameters& RainDepthPassParameters);

class FRainDepthProjectedInfo : public FRefCountedObject
{
public:
	FViewInfo* RainDepthView;

	TUniformBufferRef<FMobileRainDepthPassUniformParameters> MobileRainDepthPassUniformBuffer;

	FMatrix ProjectionMatrix;

	FMatrix ViewMatrix;
	
	FBox RainCaptureBound;

	FConvexVolume RainDepthFrustum;

	TRefCountPtr<IPooledRenderTarget> RainDepthRenderTarget;

	FParallelMeshDrawCommandPass RainDepthCommandPass;

	int32 DepthResoultionX;
	int32 DepthResloutionY;

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
	/** dynamic rain peojected elements */
	TArray<const FPrimitiveSceneInfo*,SceneRenderingAllocator> DynamicProjectedPrimitives;
	
	TArray<FMeshBatchAndRelevance, SceneRenderingAllocator> DynamicProjectedMeshElements;
	
	TArray<FMeshBatchAndRelevance, SceneRenderingAllocator> DynamicProjectedTranslucentMeshElements;

	TArray<const FStaticMeshBatch*, SceneRenderingAllocator> ProjectedMeshCommandBuildRequests;
	
	int32 NumDynamicProjectedMeshElements;
	
	int32 NumProjectedMeshCommandBuildRequestElements;

	FMeshCommandOneFrameArray RainDepthPassVisibleCommands;
};


template<bool bUsePositionOnlyStream>
class TRainDepthVS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(TRainDepthVS, MeshMaterial);
protected:

	TRainDepthVS() {}

	TRainDepthVS(const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer) :
		FMeshMaterialShader(Initializer)
	{
		BindSceneTextureUniformBufferDependentOnShadingPath(Initializer, PassUniformBuffer);
	}

public:

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		if (bUsePositionOnlyStream)
		{
			return Parameters.VertexFactoryType->SupportsPositionOnly() && Parameters.MaterialParameters.bIsSpecialEngineMaterial;
		}

		// Only compile for the default material and masked materials
		return (
			Parameters.MaterialParameters.bIsSpecialEngineMaterial ||
			!Parameters.MaterialParameters.bWritesEveryPixel ||
			Parameters.MaterialParameters.bMaterialMayModifyMeshPosition ||
			Parameters.MaterialParameters.bIsTranslucencyWritingCustomDepth);
	}

	void GetShaderBindings(
		const FScene* Scene,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material,
		const FMeshPassProcessorRenderState& DrawRenderState,
		const FDepthOnlyShaderElementData& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings) const
	{
		FMeshMaterialShader::GetShaderBindings(Scene, FeatureLevel, PrimitiveSceneProxy, MaterialRenderProxy, Material, DrawRenderState, ShaderElementData, ShaderBindings);
	}
};

class FRainDepthPS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FRainDepthPS, MeshMaterial);
public:

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return
			// Compile for materials that are masked, avoid generating permutation for other platforms if bUsesMobileColorValue is true
			((!Parameters.MaterialParameters.bWritesEveryPixel || Parameters.MaterialParameters.bHasPixelDepthOffsetConnected || Parameters.MaterialParameters.bIsTranslucencyWritingCustomDepth) && (!bUsesMobileColorValue || IsMobilePlatform(Parameters.Platform)))
			// Mobile uses material pixel shader to write custom stencil to color target
			|| (IsMobilePlatform(Parameters.Platform) && (Parameters.MaterialParameters.bIsDefaultMaterial || Parameters.MaterialParameters.bMaterialMayModifyMeshPosition));
	}

	FRainDepthPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FMeshMaterialShader(Initializer)
	{
		MobileColorValue.Bind(Initializer.ParameterMap, TEXT("MobileColorValue"));
		BindSceneTextureUniformBufferDependentOnShadingPath(Initializer, PassUniformBuffer);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMeshMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		OutEnvironment.SetDefine(TEXT("ALLOW_DEBUG_VIEW_MODES"), AllowDebugViewmodes(Parameters.Platform));
		if (IsMobilePlatform(Parameters.Platform))
		{
			// No access to scene textures during depth rendering on mobile
			OutEnvironment.SetDefine(TEXT("SCENE_TEXTURES_DISABLED"), 1u);
		}
		else
		{
			OutEnvironment.SetDefine(TEXT("OUTPUT_MOBILE_COLOR_VALUE"), 0u);
		}
	}

	FRainDepthPS() {}

	void GetShaderBindings(
		const FScene* Scene,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material,
		const FMeshPassProcessorRenderState& DrawRenderState,
		const FDepthOnlyShaderElementData& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings) const
	{
		FMeshMaterialShader::GetShaderBindings(Scene, FeatureLevel, PrimitiveSceneProxy, MaterialRenderProxy, Material, DrawRenderState, ShaderElementData, ShaderBindings);

		ShaderBindings.Add(MobileColorValue, ShaderElementData.MobileColorValue);
	}

	LAYOUT_FIELD(FShaderParameter, MobileColorValue);
};

template<bool bPositionOnly>
void GetRainDepthPassShaders(
	const FMaterial& Material,
	FVertexFactoryType* VertexFactoryType,
	ERHIFeatureLevel::Type FeatureLevel,
	TShaderRef<FBaseHS>& HullShader,
	TShaderRef<FBaseDS>& DomainShader,
	TShaderRef<TRainDepthVS<bPositionOnly>>& VertexShader,
	TShaderRef<FRainDepthPS>& PixelShader,
	FShaderPipelineRef& ShaderPipeline);

class FRainDepthPassMeshProcessor : public FMeshPassProcessor
{
public:

	FRainDepthPassMeshProcessor(const FScene* Scene,
		const FSceneView* InViewIfDynamicMeshCommand,
		/*const FMeshPassProcessorRenderState& InPassDrawRenderState,*/
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

	const bool bRespectUseAsOccluderFlag;
	const EDepthDrawingMode EarlyZPassMode;
	const bool bEarlyZPassMovable;
};