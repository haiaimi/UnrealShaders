#include "PrecomputedRadianceTransfer/PRTLightingBake.h"
#include "SceneRendering.h"
#include "SceneRenderTargets.h"
#include "ScenePrivate.h"
#include "SceneManagement.h"
#include "MeshPassProcessor.inl"
#include "SceneFilterRendering.h"
#include <RenderResource.h>
#include "SceneCore.h"

class FPRTDiffuseBakeVS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FPRTDiffuseBakeVS, MeshMaterial);
protected:

	FPRTDiffuseBakeVS() {}
	FPRTDiffuseBakeVS(const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer) :
		FMeshMaterialShader(Initializer)
	{
		LightMapPolicyType::VertexParametersType::Bind(Initializer.ParameterMap);
		//PassUniformBuffer.Bind(Initializer.ParameterMap, FMobileBasePassUniformParameters::StaticStructMetadata.GetShaderVariableName());
	}

public:

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return true;
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMeshMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	void GetShaderBindings(
		const FScene* Scene,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material,
		const FMeshPassProcessorRenderState& DrawRenderState,
		const FMeshMaterialShaderElementData& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings) const
	{
		FMeshMaterialShader::GetShaderBindings(Scene, FeatureLevel, PrimitiveSceneProxy, MaterialRenderProxy, Material, DrawRenderState, ShaderElementData, ShaderBindings);
	}
};

IMPLEMENT_MATERIAL_SHADER_TYPE(, FPRTDiffuseBakeVS, TEXT("/Engine/Private/PrecomputedRadianceTransfer/PRTDiffuseBakeVertexShader.usf"),TEXT("Main"), SF_Vertex);

class FPRTDiffuseBakePS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FPRTDiffuseBakePS, MeshMaterial);
protected:

	FPRTDiffuseBakePS() {}
	FPRTDiffuseBakePS(const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer) :
		FMeshMaterialShader(Initializer)
	{
		LightMapPolicyType::VertexParametersType::Bind(Initializer.ParameterMap);
		//PassUniformBuffer.Bind(Initializer.ParameterMap, FMobileBasePassUniformParameters::StaticStructMetadata.GetShaderVariableName());
	}

public:

	static bool ShouldCompilePermutation(EShaderPlatform Platform, const FMaterial* Material, const FVertexFactoryType* VertexFactoryType)
	{
		return true;
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMeshMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	void GetShaderBindings(
		const FScene* Scene,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material,
		const FMeshPassProcessorRenderState& DrawRenderState,
		const FMeshMaterialShaderElementData& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings) const
	{
		FMeshMaterialShader::GetShaderBindings(Scene, FeatureLevel, PrimitiveSceneProxy, MaterialRenderProxy, Material, DrawRenderState, ShaderElementData, ShaderBindings);
	}
};

IMPLEMENT_MATERIAL_SHADER_TYPE(, FPRTDiffuseBakePS, TEXT("/Engine/Private/PrecomputedRadianceTransfer/PRTDiffuseBakePixelShader.usf"),TEXT("Main"), SF_Pixel);

void GetPRTDiffuseBakePassShaders(
	const FMaterial& Material,
	FVertexFactoryType* VertexFactoryType,
	ERHIFeatureLevel::Type FeatureLevel,
	TShaderRef<FPRTDiffuseBakeVS>& VertexShader,
	TShaderRef<FPRTDiffuseBakePS>& PixelShader,
	FShaderPipelineRef& ShaderPipeline
)
{
	if(ShaderPipeline.IsValid())
	{
		VertexShader = ShaderPipeline.GetShader<FPRTDiffuseBakeVS>(VertexFactoryType);
		PixelShader = ShaderPipeline->GetShader<FPRTDiffuseBakePS>(VertexFactoryType);
	}
}

void SetupPRTDiffuseBakePassState(FMeshPassProcessorRenderState& DrawRenderState, const FPrimitiveSceneProxy* PrimitiveSceneProxy, const FMaterial& Material)
{
	if (Material.GetBlendMode() == BLEND_Masked && Material.IsUsingAlphaToCoverage())
	{
		DrawRenderState.SetBlendState(TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero,
			CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero,
			CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero,
			CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero,
			CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero,
			CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero,
			CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero,
			CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero,
			true>::GetRHI());
	}
}

FPRTDiffusebakePassMeshProcessor::FPRTDiffusebakePassMeshProcessor(const FScene* Scene, ERHIFeatureLevel::Type InFeatureLevel, const FSceneView* InViewIfDynamicMeshCommand, const FMeshPassProcessorRenderState& InPassDrawRenderState, FMeshPassDrawListContext* InDrawListContext, ERasterizerCullMode CullMode):
	PassDrawRenderState(InPassDrawRenderState),
	TargetCullMode(CullMode)
{
}

void FPRTDiffusebakePassMeshProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId /*= -1*/)
{
	if (!MeshBatch.bUseForMaterial)
	{
		return;
	}

	// Determine the mesh's material and blend mode.
	const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = nullptr;
	const FMaterial& Material = MeshBatch.MaterialRenderProxy->GetMaterialWithFallback(FeatureLevel, FallbackMaterialRenderProxyPtr);
	const FMaterialRenderProxy& MaterialRenderProxy = FallbackMaterialRenderProxyPtr ? *FallbackMaterialRenderProxyPtr : *MeshBatch.MaterialRenderProxy;
	const EBlendMode BlendMode = Material.GetBlendMode();
	const FMaterialShadingModelField ShadingModels = Material.GetShadingModels();
	const bool bIsTranslucent = IsTranslucentBlendMode(BlendMode);
	const bool bUsesWaterMaterial = ShadingModels.HasShadingModel(MSM_SingleLayerWater);
	const bool bCanReceiveCSM = ((Flags & EFlags::CanReceiveCSM) == EFlags::CanReceiveCSM);
	const bool bIsSky = Material.IsSky();
	
	// opaque materials.
	// We have to render the opaque meshes used for mobile pixel projected reflection both in opaque and translucent pass if the quality level is greater than BestPerformance
	if (!bIsTranslucent && !bUsesWaterMaterial && !bIsSky)
	{
		Process(MeshBatch, BatchElementMask, StaticMeshId, BlendMode, PrimitiveSceneProxy, MaterialRenderProxy, Material);
	}
}

void FPRTDiffusebakePassMeshProcessor::Process(const FMeshBatch& MeshBatch, uint64 BatchElementMask, int32 StaticMeshId, EBlendMode BlendMode, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, const FMaterialRenderProxy& RESTRICT MaterialRenderProxy, const FMaterial& RESTRICT MaterialResource)
{
	ERasterizerFillMode MeshFillMode;
	ERasterizerCullMode MeshCullMode;

	const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;

	FMeshProcessorShaders<
		FPRTDiffuseBakeVS,
		FBaseHS,
		FBaseDS,
		FPRTDiffuseBakePS> PRTDiffusebakePassShaders;

	FShaderPipelineRef ShaderPipeline;

	GetPRTDiffuseBakePassShaders(
		MaterialResource,
		VertexFactory->GetType(),
		FeatureLevel,
		PRTDiffusebakePassShaders.VertexShader,
		PRTDiffusebakePassShaders.PixelShader,
		ShaderPipeline);

	FMeshMaterialShaderElementData ShaderElementData(0.f);
	ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, true);
	const FMeshDrawCommandSortKey SortKey = CalculateMeshStaticSortKey(PRTDiffusebakePassShaders.VertexShader.GetShader(), PRTDiffusebakePassShaders.PixelShader.GetShader());

	const FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(MeshBatch);
	ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, MaterialResource, OverrideSettings);
	ERasterizerCullMode MeshCullMode = ComputeMeshCullMode(MeshBatch, MaterialResource, OverrideSettings);

	if (TargetCullMode == CM_CCW && (MeshCullMode == CM_None || MeshCullMode == CM_CCW))
	{
		return;
	}

	if (TargetCullMode == CM_CCW && MeshCullMode == CM_CW)
	{
		MeshCullMode = TargetCullMode;
	}

	SetupPRTDiffuseBakePassState(PassDrawRenderState, PrimitiveSceneProxy, MaterialResource);

	BuildMeshDrawCommands(
		MeshBatch,
		BatchElementMask,
		PrimitiveSceneProxy,
		MaterialRenderProxy,
		MaterialResource,
		PassDrawRenderState,
		PRTDiffusebakePassShaders,
		MeshFillMode,
		MeshCullMode,
		SortKey,
		EMeshPassFeatures::Default,
		ShaderElementData);
}


FMeshPassProcessor* CreatePRTDiffuseBakePassProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
{
	FMeshPassProcessorRenderState PassDrawRenderState(Scene->UniformBuffers.ViewUniformBuffer, Scene->UniformBuffers.MobileOpaqueBasePassUniformBuffer);
	PassDrawRenderState.SetInstancedViewUniformBuffer(Scene->UniformBuffers.InstancedViewUniformBuffer);
	PassDrawRenderState.SetBlendState(TStaticBlendStateWriteMask<CW_RGBA>::GetRHI());
	PassDrawRenderState.SetDepthStencilAccess(Scene->DefaultBasePassDepthStencilAccess);
	PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI());

	return new(FMemStack::Get()) FPRTDiffusebakePassMeshProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, PassDrawRenderState, InDrawListContext, CM_CW);
}

FMeshPassProcessor* CreatePRTDiffuseBakePassReverseCullProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
{
	FMeshPassProcessorRenderState PassDrawRenderState(Scene->UniformBuffers.ViewUniformBuffer, Scene->UniformBuffers.MobileOpaqueBasePassUniformBuffer);
	PassDrawRenderState.SetInstancedViewUniformBuffer(Scene->UniformBuffers.InstancedViewUniformBuffer);
	PassDrawRenderState.SetBlendState(TStaticBlendStateWriteMask<CW_RGBA>::GetRHI());
	PassDrawRenderState.SetDepthStencilAccess(Scene->DefaultBasePassDepthStencilAccess);
	PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI());

	return new(FMemStack::Get()) FPRTDiffusebakePassMeshProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, PassDrawRenderState, InDrawListContext, CM_CCW);
}

FRegisterPassProcessorCreateFunction RegisterPRTDiffuseBakePass(&CreatePRTDiffuseBakePassProcessor, EShadingPath::Mobile, EMeshPass::PRTDiffuseBake, EMeshPassFlags::CachedMeshCommands);
FRegisterPassProcessorCreateFunction RegisterPRTDiffuseBakePassReverseCull(&CreatePRTDiffuseBakePassReverseCullProcessor, EShadingPath::Mobile, EMeshPass::PRTDiffuseBakeReverseCull, EMeshPassFlags::CachedMeshCommands);