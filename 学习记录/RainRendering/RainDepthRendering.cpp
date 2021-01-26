#include "RainDepthRendering.h"
#include "PrimitiveViewRelevance.h"
#include "PrimitiveSceneInfo.h"
#include "SceneRendering.h"
#include "SceneRenderTargets.h"
#include "ScenePrivate.h"
#include "SceneManagement.h"
#include "MeshPassProcessor.inl"
#include "SceneFilterRendering.h"
#include "RainDepthRenderCommon.h"
#include "Components/RainDepthComponent.h"
#include <RenderResource.h>
#include "SceneCore.h"
#include <DynamicBufferAllocator.h>

static TAutoConsoleVariable<int32> CVarRenderRain(
	TEXT("r.DebugRainDepth"), 0,
	TEXT("Draw rain depth every frame.\n"),
	ECVF_RenderThreadSafe);

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FMobileRainDepthPassUniformParameters, "MobileRainDepthPass");

template<bool bUsePositionOnlyStream>
class TRainDepthVS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(TRainDepthVS, MeshMaterial);
public:

	TRainDepthVS() {}

	TRainDepthVS(const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer) :
		FMeshMaterialShader(Initializer)
	{
		//BindSceneTextureUniformBufferDependentOnShadingPath(Initializer, PassUniformBuffer);

		PassUniformBuffer.Bind(Initializer.ParameterMap, FMobileRainDepthPassUniformParameters::StaticStructMetadata.GetShaderVariableName());
		//RainDepthPassUniformBuffer.Bind(Initializer.ParameterMap, FMobileRainDepthPassUniformParameters::StaticStructMetadata.GetShaderVariableName());
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMeshMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		OutEnvironment.SetDefine(TEXT("RENDER_RAIN_DEPTH"), 1);
	}

public:

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
	#if WITH_EDITOR
		if(!IsMobilePlatform(Parameters.Platform))
			return false;

		if(Parameters.MaterialParameters.TessellationMode != MTM_NoTessellation)
			return false;
		
		if (bUsePositionOnlyStream)
		{
			return Parameters.VertexFactoryType->SupportsPositionOnly();
		}

		// Only compile for the default material and masked materials
		return (
			Parameters.MaterialParameters.bIsSpecialEngineMaterial ||
			!Parameters.MaterialParameters.bWritesEveryPixel ||
			Parameters.MaterialParameters.bMaterialMayModifyMeshPosition ||
			Parameters.MaterialParameters.bIsTranslucencyWritingCustomDepth ||
			Parameters.MaterialParameters.bIsUsedWithRainOccluder);
	#endif
		return false;
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
		//ShaderBindings.Add(PassUniformBuffer, Scene->UniformBuffers.MobileRainDepthPassUniformBuffer);
	}

	//LAYOUT_FIELD(FShaderUniformBufferParameter, RainDepthPassUniformBuffer);
};

class FRainDepthPS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FRainDepthPS, MeshMaterial);
public:

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
	#if WITH_EDITOR
		return
			((!Parameters.MaterialParameters.bWritesEveryPixel || Parameters.MaterialParameters.bHasPixelDepthOffsetConnected || Parameters.MaterialParameters.bIsTranslucencyWritingCustomDepth) && (IsMobilePlatform(Parameters.Platform)))
			|| (IsMobilePlatform(Parameters.Platform) && (Parameters.MaterialParameters.bIsDefaultMaterial || Parameters.MaterialParameters.bMaterialMayModifyMeshPosition)
			|| Parameters.MaterialParameters.bIsUsedWithRainOccluder);
	#endif
	return false;
	}

	FRainDepthPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FMeshMaterialShader(Initializer)
	{
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

		OutEnvironment.SetDefine(TEXT("RENDER_RAIN_DEPTH"), 1);
	}

	FRainDepthPS() {}

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

		//ShaderBindings.Add(MobileColorValue, ShaderElementData.MobileColorValue);
	}
};

class FRainDepthHS : public FBaseHS
{
	DECLARE_SHADER_TYPE(FRainDepthHS, MeshMaterial);
public:

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return FBaseHS::ShouldCompilePermutation(Parameters)
			&& TRainDepthVS<false>::ShouldCompilePermutation(Parameters) && false;
	}

	FRainDepthHS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FBaseHS(Initializer)
	{}

	FRainDepthHS() {}
};

class FRainDepthDS : public FBaseDS
{
	DECLARE_SHADER_TYPE(FRainDepthDS, MeshMaterial);
public:

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return FBaseDS::ShouldCompilePermutation(Parameters)
			&& TRainDepthVS<false>::ShouldCompilePermutation(Parameters) && false;
	}

	FRainDepthDS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FBaseDS(Initializer)
	{}

	FRainDepthDS() {}
};

/** This shader is used to convert reciprocal depth map to height map in world space*/
class FConvertRainDepthPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FConvertRainDepthPS)

public:
	FConvertRainDepthPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		RainDepthPassUniformBuffer.Bind(Initializer.ParameterMap, FMobileRainDepthPassUniformParameters::StaticStructMetadata.GetShaderVariableName());
		RainDepthTexture.Bind(Initializer.ParameterMap, TEXT("RainDepthTexture"));
		DepthTextureSampler.Bind(Initializer.ParameterMap, TEXT("DepthTextureSampler"));
	}
	FConvertRainDepthPS() {}
	
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	static void ModifyCompilationEnvironment(const FShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		OutEnvironment.SetDefine(TEXT("CONVERT_DEPTH_SHADER"), 1);
	}

	void SetParameters(FRHICommandList& RHICmdList, TUniformBufferRef<FMobileRainDepthPassUniformParameters> InRainDepthUniformBuffer, FRHITexture2D* InRainDepth)
	{
		
		FRHIPixelShader* ShaderRHI = RHICmdList.GetBoundPixelShader();
		SetUniformBufferParameter(RHICmdList, ShaderRHI, RainDepthPassUniformBuffer, InRainDepthUniformBuffer);
		SetTextureParameter(RHICmdList, ShaderRHI, RainDepthTexture, DepthTextureSampler, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), InRainDepth);
	}

	LAYOUT_FIELD(FShaderUniformBufferParameter, RainDepthPassUniformBuffer)
	LAYOUT_FIELD(FShaderResourceParameter, RainDepthTexture)
	LAYOUT_FIELD(FShaderResourceParameter, DepthTextureSampler)
};

template<bool bPositionOnly>
void GetRainDepthPassShaders(
	const FMaterial& Material,
	FVertexFactoryType* VertexFactoryType,
	ERHIFeatureLevel::Type FeatureLevel,
	TShaderRef<FRainDepthHS>& HullShader,
	TShaderRef<FRainDepthDS>& DomainShader,
	TShaderRef<TRainDepthVS<bPositionOnly>>& VertexShader,
	TShaderRef<FRainDepthPS>& PixelShader,
	FShaderPipelineRef& ShaderPipeline);

IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, TRainDepthVS<true>, TEXT("/Engine/Private/PositionOnlyRainDepthVertexShader.usf"), TEXT("PositionOnlyMain"), SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, TRainDepthVS<false>, TEXT("/Engine/Private/RainDepthOnlyVertexShader.usf"), TEXT("Main"), SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(, FRainDepthHS, TEXT("/Engine/Private/RainDepthOnlyVertexShader.usf"), TEXT("MainHull"), SF_Hull);
IMPLEMENT_MATERIAL_SHADER_TYPE(, FRainDepthDS, TEXT("/Engine/Private/RainDepthOnlyVertexShader.usf"), TEXT("MainDomain"), SF_Domain);

IMPLEMENT_MATERIAL_SHADER_TYPE(, FRainDepthPS, TEXT("/Engine/Private/RainDepthOnlyPixelShader.usf"),TEXT("Main"), SF_Pixel);

IMPLEMENT_GLOBAL_SHADER(FConvertRainDepthPS, "/Engine/Private/RainDepthOnlyPixelShader.usf", "ConvertRainDepthPS", SF_Pixel);

//IMPLEMENT_SHADERPIPELINE_TYPE_VS(RainDepthNoPixelPipeline, TRainDepthVS<false>, true);
IMPLEMENT_SHADERPIPELINE_TYPE_VS(RainDepthPosOnlyNoPixelPipeline, TRainDepthVS<true>, true);
//IMPLEMENT_SHADERPIPELINE_TYPE_VSPS(RainDepthNoColorOutputPipeline, TRainDepthVS<false>, FRainDepthPS, true);


/** This shader is used to apply motion blur to rain drop texture*/
//class FApplyRainDropNoiseCS : public FGlobalShader
//{
//	DECLARE_GLOBAL_SHADER(FApplyRainDropNoiseCS)
//
//public:
//	FApplyRainDropNoiseCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
//		FGlobalShader(Initializer)
//	{
//		RainDropTexture.Bind(Initializer.ParameterMap, TEXT("RainDropTexture"));
//		NoiseTexture.Bind(Initializer.ParameterMap, TEXT("NoiseTexture"));
//	}
//	FApplyRainDropNoiseCS() {}
//
//	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
//	{
//		return true;
//	}
//
//	static void ModifyCompilationEnvironment(const FShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
//	{
//		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
//
//		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), 8);
//	}
//
//	void SetParameters(FRHICommandList& RHICmdList, FRHITexture2D* InRainDrop, FRHITexture2D* InNoise)
//	{
//		FRHIPixelShader* ShaderRHI = RHICmdList.GetBoundPixelShader();
//		//SetUniformBufferParameter(RHICmdList, ShaderRHI, RainDepthPassUniformBuffer, InRainDepthUniformBuffer);
//		SetTextureParameter(RHICmdList, ShaderRHI, RainDropTexture, InRainDrop);
//		SetTextureParameter(RHICmdList, ShaderRHI, NoiseTexture, InNoise);
//	}
//
//	LAYOUT_FIELD(FShaderResourceParameter, RainDropTexture)
//	LAYOUT_FIELD(FShaderResourceParameter, NoiseTexture)
//};
//
//IMPLEMENT_GLOBAL_SHADER(FApplyRainDropNoiseCS, "/Engine/Private/RainRenderingCommon.usf", "ApplyRainDropNoiseCS", SF_Compute);

void SetupRainDepthPassState(FMeshPassProcessorRenderState& DrawRenderState)
{
	// Disable color writes, enable depth tests and writes.
	DrawRenderState.SetBlendState(TStaticBlendState<CW_NONE>::GetRHI());
	DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_LessEqual>::GetRHI());
}


FRainDepthPassMeshProcessor::FRainDepthPassMeshProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, const FMeshPassProcessorRenderState& InPassDrawRenderState, FMeshPassDrawListContext* InDrawListContext) :
             FMeshPassProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, InDrawListContext)
{
	PassDrawRenderState = InPassDrawRenderState;
	PassDrawRenderState.SetViewUniformBuffer(Scene->UniformBuffers.ViewUniformBuffer);
	PassDrawRenderState.SetInstancedViewUniformBuffer(Scene->UniformBuffers.InstancedViewUniformBuffer);
	// #TODO Need to set custom rain depth uniform buffer
	PassDrawRenderState.SetPassUniformBuffer(Scene->UniformBuffers.MobileRainDepthPassUniformBuffer);
}

void FRainDepthPassMeshProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId /*= -1*/)
{
	//bool bDraw = MeshBatch.bUseForRainDepthPass;
	const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = nullptr;
	const FMaterial& Material = MeshBatch.MaterialRenderProxy->GetMaterialWithFallback(FeatureLevel, FallbackMaterialRenderProxyPtr);
	bool bDraw = Material.IsUsedWithRainOccluder() && Material.GetTessellationMode() == EMaterialTessellationMode::MTM_NoTessellation;
	
	if (bDraw)
	{
		//if (PrimitiveSceneProxy)
		//{
		//	bDraw = PrimitiveSceneProxy->ShouldUseAsOccluder()
		//		// Only render static objects unless movable are requested.
		//		&& (!PrimitiveSceneProxy->IsMovable());

		//	// Filter dynamic mesh commands by screen size.
		//	if (ViewIfDynamicMeshCommand)
		//	{
		//		extern float GMinScreenRadiusForDepthPrepass;
		//		const float LODFactorDistanceSquared = (PrimitiveSceneProxy->GetBounds().Origin - ViewIfDynamicMeshCommand->ViewMatrices.GetViewOrigin()).SizeSquared() * FMath::Square(ViewIfDynamicMeshCommand->LODDistanceFactor);
		//		bDraw = bDraw && FMath::Square(PrimitiveSceneProxy->GetBounds().SphereRadius) > GMinScreenRadiusForDepthPrepass * GMinScreenRadiusForDepthPrepass * LODFactorDistanceSquared;
		//	}
		//}
		//else
		//{
		//	bDraw = false;
		//}
	}
	//bDraw = true;

	if (bDraw)
	{
		// Determine the mesh's material and blend mode.
		const FMaterialRenderProxy& MaterialRenderProxy = FallbackMaterialRenderProxyPtr ? *FallbackMaterialRenderProxyPtr : *MeshBatch.MaterialRenderProxy;
		const EBlendMode BlendMode = Material.GetBlendMode();
		const FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(MeshBatch);
		const ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, Material, OverrideSettings);
		const ERasterizerCullMode MeshCullMode = ComputeMeshCullMode(MeshBatch, Material, OverrideSettings);
		const bool bIsTranslucent = IsTranslucentBlendMode(BlendMode);

		if (!bIsTranslucent
			&& ShouldIncludeDomainInMeshPass(Material.GetMaterialDomain())
			&& ShouldIncludeMaterialInDefaultOpaquePass(Material)
			&& MeshBatch.VertexFactory->GetType()->GetFName() != FName(TEXT("FLandscapeFixedGridVertexFactoryMobile"))
			&& Material.IsUsedWithRainOccluder())
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
				const bool bMaterialMasked = !Material.WritesEveryPixel() || Material.IsTranslucencyWritingCustomDepth() || bDraw;
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
		FRainDepthHS,
		FRainDepthDS,
		FRainDepthPS> RainDepthPassShaders;

	FShaderPipelineRef ShaderPipeline;

	GetRainDepthPassShaders<bPositionOnly>(
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

	FDepthOnlyShaderElementData ShaderElementData(0.0f);
	ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, true);

	const FMeshDrawCommandSortKey SortKey = CalculateMeshStaticSortKey(RainDepthPassShaders.VertexShader.GetShader(), RainDepthPassShaders.PixelShader.GetShader());

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

void SetupRainDepthPassUniformBuffer(const FRainDepthProjectedInfo* RainDepthInfo, FRHICommandListImmediate& RHICmdList, const FViewInfo& View, FMobileRainDepthPassUniformParameters& RainDepthPassParameters)
{
	FSceneRenderTargets& SceneRenderTargets = FSceneRenderTargets::Get(RHICmdList);
	SetupSceneTextureUniformParameters(SceneRenderTargets, View.FeatureLevel, ESceneTextureSetupMode::None, RainDepthPassParameters.SceneTextures);

	/*FOrthoMatrix OrthoProjMatrix(4000, 4000, 1.f, 0.f);
	FMatrix ViewRotationMatrix = FInverseRotationMatrix(FRotator(-90.f, 0.f, 0.f));

	FVector ViewOrigin(0, 0.f, 2000.f);
	const FMatrix ViewMatrix = FTranslationMatrix(-ViewOrigin) * ViewRotationMatrix * FMatrix(
		FPlane(0, 0, 1, 0),
		FPlane(1, 0, 0, 0),
		FPlane(0, 1, 0, 0),
		FPlane(0, 0, 0, 1));*/
	FMatrix ViewProjMatrix = RainDepthInfo->ViewMatrix * RainDepthInfo->ProjectionMatrix;
	// #TODO
	RainDepthPassParameters.ProjectionMatrix = ViewProjMatrix;
	RainDepthPassParameters.ViewMatrix = RainDepthInfo->ViewMatrix;
	RainDepthPassParameters.DepthTexSizeAndInvSize = FVector4(RainDepthInfo->DepthResolutionX, RainDepthInfo->DepthResolutionY, 1.f / RainDepthInfo->DepthResolutionX, 1.f / RainDepthInfo->DepthResolutionY);
	RainDepthPassParameters.ViewPosition = RainDepthInfo->ViewLocation;
	RainDepthPassParameters.MaxDepthSize = RainDepthInfo->MaxDepth;
}

static FORCEINLINE bool UseShaderPipelines(ERHIFeatureLevel::Type InFeatureLevel)
{
	static const auto* CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.ShaderPipelines"));
	return RHISupportsShaderPipelines(GShaderPlatformForFeatureLevel[InFeatureLevel]) && CVar && CVar->GetValueOnAnyThread() != 0;
}

template<bool bPositionOnly>
void GetRainDepthPassShaders(const FMaterial& Material, FVertexFactoryType* VertexFactoryType, ERHIFeatureLevel::Type FeatureLevel, TShaderRef<FRainDepthHS>& HullShader, TShaderRef<FRainDepthDS>& DomainShader, TShaderRef<TRainDepthVS<bPositionOnly>> &VertexShader, TShaderRef<FRainDepthPS>& PixelShader, FShaderPipelineRef& ShaderPipeline)
{
	if (bPositionOnly)
	{
		ShaderPipeline = UseShaderPipelines(FeatureLevel) ? Material.GetShaderPipeline(&RainDepthPosOnlyNoPixelPipeline, VertexFactoryType) : FShaderPipelineRef();
		VertexShader = ShaderPipeline.IsValid()
			? ShaderPipeline.GetShader<TRainDepthVS<bPositionOnly> >()
			: Material.GetShader<TRainDepthVS<bPositionOnly> >(VertexFactoryType);
	}
	else
	{
		const bool bNeedsPixelShader = !Material.WritesEveryPixel() || Material.MaterialUsesPixelDepthOffset() || Material.IsTranslucencyWritingCustomDepth();

		const EMaterialTessellationMode TessellationMode = Material.GetTessellationMode();
		if (RHISupportsTessellation(GShaderPlatformForFeatureLevel[FeatureLevel])
			&& VertexFactoryType->SupportsTessellationShaders()
			&& TessellationMode != MTM_NoTessellation)
		{
			ShaderPipeline = FShaderPipelineRef();
			VertexShader = Material.GetShader<TRainDepthVS<bPositionOnly> >(VertexFactoryType);
			HullShader = Material.GetShader<FRainDepthHS>(VertexFactoryType);
			DomainShader = Material.GetShader<FRainDepthDS>(VertexFactoryType);
			if (bNeedsPixelShader)
			{
				PixelShader = Material.GetShader<FRainDepthPS>(VertexFactoryType);
			}
		}
		else
		{
			HullShader.Reset();
			DomainShader.Reset();
			bool bUseShaderPipelines = UseShaderPipelines(FeatureLevel);
			if (bNeedsPixelShader)
			{
				ShaderPipeline = /*bUseShaderPipelines ? Material.GetShaderPipeline(&RainDepthNoColorOutputPipeline, VertexFactoryType, false) : */FShaderPipelineRef();
			}
			else
			{
				ShaderPipeline = /*bUseShaderPipelines ? Material.GetShaderPipeline(&RainDepthNoPixelPipeline, VertexFactoryType, false) : */FShaderPipelineRef();
			}

			if (ShaderPipeline.IsValid())
			{
				VertexShader = ShaderPipeline.GetShader<TRainDepthVS<bPositionOnly>>();
				if (bNeedsPixelShader)
				{
					PixelShader = ShaderPipeline.GetShader<FRainDepthPS>();
				}
			}
			else
			{
				VertexShader = Material.GetShader<TRainDepthVS<bPositionOnly> >(VertexFactoryType);
				if (bNeedsPixelShader)
				{
					PixelShader = Material.GetShader<FRainDepthPS>(VertexFactoryType);
				}
			}
		}
	}
}

FMeshPassProcessor* CreateRainDepthPassProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
{
	FRHIUniformBuffer* PassUniformBuffer = nullptr;

	FMeshPassProcessorRenderState RainDepthPassState;
	SetupRainDepthPassState(RainDepthPassState);
	EShadingPath ShadingPath = Scene->GetShadingPath();
	//if (ShadingPath == EShadingPath::Mobile)
	{
		PassUniformBuffer = Scene->UniformBuffers.MobileRainDepthPassUniformBuffer;
		RainDepthPassState.SetPassUniformBuffer(PassUniformBuffer);
	}

	return new(FMemStack::Get()) FRainDepthPassMeshProcessor(Scene, InViewIfDynamicMeshCommand, RainDepthPassState, InDrawListContext);
}

#if WITH_EDITOR
FRegisterPassProcessorCreateFunction RegisterRainDepthPass(&CreateRainDepthPassProcessor, EShadingPath::Mobile, EMeshPass::RainDepthPass, EMeshPassFlags::CachedMeshCommands);
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////

void FScene::AddRainDepthCapture(FRainDepthSceneProxy* RainDepthSceneProxy)
{
	check(RainDepthSceneProxy);
	FScene* Scene = this;

	ENQUEUE_RENDER_COMMAND(FAddRainDepthCommand)(
		[Scene, RainDepthSceneProxy](FRHICommandListImmediate& RHICmdList)
	{
		check(!Scene->RainDepthStack.Contains(RainDepthSceneProxy));
		Scene->RainDepthStack.Push(RainDepthSceneProxy);

		RainDepthSceneProxy->RainDepthInfo = new FRainDepthProjectedInfo(*RainDepthSceneProxy);
		
		Scene->RainDepthInfo = RainDepthSceneProxy->RainDepthInfo;
	});
}

void FScene::RemoveRainDepthCapture(FRainDepthSceneProxy* RainDepthSceneProxy)
{
	check(RainDepthSceneProxy);
	FScene* Scene = this;

	ENQUEUE_RENDER_COMMAND(FRemoveRainDepthCommand)(
		[Scene, RainDepthSceneProxy](FRHICommandListImmediate& RHICmdList)
	{

		delete RainDepthSceneProxy->RainDepthInfo;
		Scene->RainDepthStack.RemoveSingle(RainDepthSceneProxy);

		if (Scene->RainDepthStack.Num() > 0)
		{
			Scene->RainDepthInfo = Scene->RainDepthStack.Last()->RainDepthInfo;
		}
		else
		{
			Scene->RainDepthInfo = nullptr;
		}
	});
}

/////////////////////////////////////////////////////////////////////////////////////////////////////

void ConvertRainDepth(FRHICommandList& RHICmdList, FRainDepthProjectedInfo& ProjectedInfo, TUniformBufferRef<FMobileRainDepthPassUniformParameters>& InRainDepthBuffer, FRHITexture2D* RainDepthTexture)
{
	SCOPED_DRAW_EVENTF(RHICmdList, EventConvertRainDepths, TEXT("ConvertRainDepths"));

	TShaderMapRef<FScreenVS> ScreenVertexShader(ProjectedInfo.RainDepthView->ShaderMap);

	const FIntPoint DepthTargetSize = FIntPoint(ProjectedInfo.DepthResolutionX, ProjectedInfo.DepthResolutionY);

	FTextureRHIRef DepthTarget;
	/*if (GRainDepthResource.DepthRenderTarget.IsValid())
	{
		DepthTarget = GRainDepthResource.DepthRenderTarget;
	}
	else*/
	{
		FPooledRenderTargetDesc RainDepthDesc2D = FPooledRenderTargetDesc::Create2DDesc(DepthTargetSize, PF_R16_UINT, FClearValueBinding::Transparent, TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource, false);
		TRefCountPtr<IPooledRenderTarget> ConvertedDepthTarget;
		GRenderTargetPool.FindFreeElement(RHICmdList, RainDepthDesc2D, ConvertedDepthTarget, TEXT("ConvertedDepthTarget"), true, ERenderTargetTransience::Transient);
		DepthTarget = ConvertedDepthTarget->GetRenderTargetItem().TargetableTexture;
	}

	FRHIRenderPassInfo RPInfo(DepthTarget, ERenderTargetActions::DontLoad_Store);
	TransitionRenderPassTargets(RHICmdList, RPInfo);
	RHICmdList.BeginRenderPass(RPInfo, TEXT("ConvertRainDepth"));
	{
		RHICmdList.SetViewport(0, 0, 0.0f, DepthTargetSize.X, DepthTargetSize.Y, 1.0f);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

		TShaderMapRef<FConvertRainDepthPS> ConvertDepthPixelShader(ProjectedInfo.RainDepthView->ShaderMap);

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = ScreenVertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = ConvertDepthPixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		ConvertDepthPixelShader->SetParameters(RHICmdList, ProjectedInfo.MobileRainDepthPassUniformBuffer, RainDepthTexture);

		{
			DrawRectangle(
				RHICmdList,
				0, 0,
				DepthTargetSize.X, DepthTargetSize.Y,
				0, 0,
				DepthTargetSize.X, DepthTargetSize.Y,
				DepthTargetSize, DepthTargetSize,
				ScreenVertexShader,
				EDRF_UseTriangleOptimization);
		}
		RHICmdList.EndRenderPass();
	}

	if (ProjectedInfo.RainDepthSceneProxy.bShouldSaveTexture)
	{
		FRainDepthSceneProxy& SceneProxy = const_cast<FRainDepthSceneProxy&>(ProjectedInfo.RainDepthSceneProxy);
		if (SceneProxy.bShouldSaveTexture)
		{
			SceneProxy.SaveDepthTextureAsAsset(DepthTarget);
		}
	}
}

class FRainFrustumIntersectTask
{
public:
	FRainFrustumIntersectTask(FRainDepthProjectedInfo& Info, const FScenePrimitiveOctree::FNode* PrimitiveNode, FViewInfo* InViewInfo):
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
				//if (RainProjectedInfo.RainDepthFrustum.IntersectBox(NodePrimitiveIt->Bounds.Origin, NodePrimitiveIt->Bounds.BoxExtent))
				{
					RainProjectedInfo.AddProjectedPrimitive(NodePrimitiveIt->PrimitiveSceneInfo, ViewInfo, ViewInfo->GetFeatureLevel());
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
	const FScenePrimitiveOctree::FNode* Node = nullptr;
	FViewInfo* ViewInfo;
};


void GatherRainDepthPrimitives(FRainDepthProjectedInfo& RainProjectedInfo, FScene* Scene)
{
	//FScene* Scene = nullptr;
	TArray<FRainFrustumIntersectTask*, SceneRenderingAllocator> Packets;
	//FRainDepthProjectedInfo RainProjectedInfo;

	Packets.Reserve(100);
	
	for (int32 i = 0; i < Scene->Primitives.Num(); ++i)
	{
		RainProjectedInfo.AddProjectedPrimitive(Scene->Primitives[i], RainProjectedInfo.RainDepthView, Scene->GetFeatureLevel());
	}
	//for (FScenePrimitiveOctree::TConstIterator<SceneRenderingAllocator> PrimitiveOctreeIt(Scene->PrimitiveOctree); PrimitiveOctreeIt.HasPendingNodes(); PrimitiveOctreeIt.Advance())
	//{
	//	const FScenePrimitiveOctree::FNode& PrimitiveOctreeNode = PrimitiveOctreeIt.GetCurrentNode();
	//	const FOctreeNodeContext& PrimitiveOctreeNodeContext = PrimitiveOctreeIt.GetCurrentContext();
	//	
	//	FOREACH_OCTREE_CHILD_NODE(ChildRef)
	//	{
	//		if (PrimitiveOctreeNode.HasChild(ChildRef))
	//		{
	//			const FOctreeNodeContext ChildContext = PrimitiveOctreeNodeContext.GetChildContext(ChildRef);
	//			bool bIsInFrustum = false;
	//			if (RainProjectedInfo.RainDepthFrustum.IntersectBox(ChildContext.Bounds.Center, ChildContext.Bounds.Extent))
	//			{
	//				bIsInFrustum = true;
	//			}
	//			// #TODO Intersect all primitive to debug
	//			bIsInFrustum = true;
	//			if (bIsInFrustum)
	//			{
	//				PrimitiveOctreeIt.PushChild(ChildRef);
	//			}
	//		}
	//	}
	//	
	//	if (PrimitiveOctreeNode.GetElementCount() > 0)
	//	{
	//		FRainFrustumIntersectTask* Packet = new(FMemStack::Get()) FRainFrustumIntersectTask(RainProjectedInfo, &PrimitiveOctreeNode, RainProjectedInfo.RainDepthView);
	//		Packets.Add(Packet);
	//	}
	//}

	//for (auto& Iter : Packets)
	//{
	//	Iter->AnyThreadTask();
	//}
	/*ParallelFor(Packets.Num(),
		[&Packets](int32 Index)
		{
			Packets[Index]->AnyThreadTask();
		},
			false
		);*/
}

void SetDepthRenderTarget(FTextureRenderTargetResource* RenderTarget)
{
	if (RenderTarget)
	{
		const FTexture2DRHIRef& Texture = RenderTarget->GetRenderTargetTexture();
		GRainDepthResource.SetRenderResource(Texture);
	}
}

FRainDepthProjectedInfo::FRainDepthProjectedInfo(const FRainDepthSceneProxy& InProxy) : 
	RainDepthSceneProxy(InProxy)
{
	FOrthoMatrix OrthoProjMatrix(InProxy.CaptureViewWidth, InProxy.CaptureViewHeight, 1.f, 0.f);
	FMatrix ViewRotationMatrix = FInverseRotationMatrix(InProxy.DepthCaptureRotation);

	FVector ViewOrigin = InProxy.DepthCapturePosition;
	const FMatrix TmpViewMatrix = FTranslationMatrix(-ViewOrigin) * ViewRotationMatrix * FMatrix(
		FPlane(0, 0, 1, 0),
		FPlane(1, 0, 0, 0),
		FPlane(0, 1, 0, 0),
		FPlane(0, 0, 0, 1));
	FMatrix ViewProjMatrix = TmpViewMatrix * OrthoProjMatrix;

	ProjectionMatrix = OrthoProjMatrix;
	ViewMatrix = TmpViewMatrix;
	DepthResolutionX = InProxy.DepthResolution.X;
	DepthResolutionY = InProxy.DepthResolution.Y;
	ViewLocation = InProxy.DepthCapturePosition;
	ViewRotation = InProxy.DepthCaptureRotation;
	MaxDepth = InProxy.MaxDepth;
}

FRainDepthProjectedInfo::~FRainDepthProjectedInfo()
{
	RainDepthCommandPass.WaitForTasksAndEmpty();
	RainDepthRenderTarget.SafeRelease();
}

void FRainDepthProjectedInfo::Init(FSceneRenderer& Renderer, const FViewInfo* View)
{
	RainDepthView = const_cast<FViewInfo*>(View);
	/*ViewProjectionMatrix = FMatrix::Identity;
	ViewMatrix = FMatrix::Identity;*/
	FeatureLevel = Renderer.FeatureLevel;
}

void FRainDepthProjectedInfo::Reset()
{
	CachedPrimitives.Empty();
	DynamicProjectedPrimitives.Empty();
	DynamicProjectedMeshElements.Empty();
	DynamicProjectedTranslucentMeshElements.Empty();
	ProjectedMeshCommandBuildRequests.Empty();
	RainDepthPassVisibleCommands.Empty();

	NumDynamicProjectedMeshElements = 0;
	NumProjectedMeshCommandBuildRequestElements = 0;

	/*FParallelMeshDrawCommandPass NewCommandPass;
	FMemory::Memmove(&RainDepthCommandPass, &NewCommandPass, sizeof(FParallelMeshDrawCommandPass));*/
}

void FRainDepthProjectedInfo::AllocateRainDepthTargets(FRHICommandListImmediate& RHICmdList)
{
	const FIntPoint DepthTargetSize = FIntPoint(DepthResolutionX, DepthResolutionY);
	FPooledRenderTargetDesc RainDepthDesc2D = FPooledRenderTargetDesc::Create2DDesc(DepthTargetSize, PF_DepthStencil, FClearValueBinding::DepthOne, TexCreate_None, TexCreate_DepthStencilTargetable | TexCreate_ShaderResource, false);
	GRenderTargetPool.FindFreeElement(RHICmdList, RainDepthDesc2D, RainDepthRenderTarget, TEXT("RainDepthZ"), true, ERenderTargetTransience::Transient);
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

	//if (ShadingPath == EShadingPath::Mobile)
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

		FPrimitiveViewRelevance ViewRelevance = PrimitiveSceneProxy->GetViewRelevance(View);
		if (!ViewRelevance.bInitializedThisFrame)
		{
			ViewRelevance = PrimitiveSceneInfo->Proxy->GetViewRelevance(View);
		}
		//const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = nullptr;
		//const FMaterial& Material = PrimitiveSceneInfo->GetMeshBatch(0)->MaterialRenderProxy->GetMaterialWithFallback(FeatureLevel, FallbackMaterialRenderProxyPtr);

		// #TODO Add Rain depth specified relevance 1
		if (ViewRelevance.bDynamicRelevance && !ViewRelevance.bEditorPrimitiveRelevance/* && Material.IsUsedWithRainOccluder()*/)
		{
			Renderer.MeshCollector.SetPrimitive(PrimitiveSceneProxy, PrimitiveSceneInfo->DefaultDynamicHitProxyId);

			// Get dynamic mesh batch
			PrimitiveSceneInfo->Proxy->GetDynamicMeshElements_RainDepthPass(ReusedViewsArray, Renderer.ViewFamily, 0x1, Renderer.MeshCollector);
			//PrimitiveSceneInfo->Proxy->GetDynamicMeshElements(ReusedViewsArray, Renderer.ViewFamily, 0x1, Renderer.MeshCollector);
		}
	}
	OutNumDynamicSubjectMeshElements = Renderer.MeshCollector.GetMeshElementCount(0);
}

bool FRainDepthProjectedInfo::AddCachedMeshDrawCommandsForPass(int32 PrimitiveIndex, const FPrimitiveSceneInfo* InPrimitiveSceneInfo, const FStaticMeshBatchRelevance& RESTRICT StaticMeshRelevance, const FStaticMeshBatch& StaticMesh, const FScene* Scene, EMeshPass::Type PassType, FMeshCommandOneFrameArray& VisibleMeshCommands, TArray<const FStaticMeshBatch*, SceneRenderingAllocator>& MeshCommandBuildRequests, int32& NumMeshCommandBuildRequestElements)
{
	if (!StaticMesh.MaterialRenderProxy->GetMaterial(FeatureLevel)->IsUsedWithRainOccluder())
		return false;
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

	return bUseCachedMeshCommand;
}

void FRainDepthProjectedInfo::AddProjectedPrimitive(FPrimitiveSceneInfo* PrimitiveSceneInfo, FViewInfo* View, ERHIFeatureLevel::Type InFeatureLevel)
{
	if (PrimitiveSceneInfo && View && CachedPrimitives.Find(PrimitiveSceneInfo) == INDEX_NONE)
	{
		CachedPrimitives.Add(PrimitiveSceneInfo);
		int32 PrimitiveId = PrimitiveSceneInfo->GetIndex();
		const FPrimitiveSceneProxy* Proxy = PrimitiveSceneInfo->Proxy;

		const FPrimitiveViewRelevance& ViewRelevance = Proxy->GetViewRelevance(View);
		
		bool bOpaque = false;
		bool bTranslucentRelevance = false;
		// #TODO
		bool bShouldReceiveRain = false; 

		bOpaque |= ViewRelevance.bOpaque || ViewRelevance.bMasked;
		bTranslucentRelevance |= ViewRelevance.HasTranslucency() && !ViewRelevance.bMasked;

		if (bOpaque)
		{
			bool bDrawingStaticMeshes = false;
			bool bUsedTesselation = false;
			if (ViewRelevance.bStaticRelevance)
			{
				bDrawingStaticMeshes |= ShouldDrawStaticMeshes(*View, PrimitiveSceneInfo, bUsedTesselation);
			}

			if (!bDrawingStaticMeshes && !bUsedTesselation)
			{
				DynamicProjectedPrimitives.Add(PrimitiveSceneInfo);
			}
		}
	}
}

bool FRainDepthProjectedInfo::ShouldDrawStaticMeshes(FViewInfo& InView, FPrimitiveSceneInfo* InPrimitiveSceneInfo, bool& bUsedTesselation)
{
	bool bDrawingStaticMeshes = false;
	int32 PrimitiveId = InPrimitiveSceneInfo->GetIndex();

	const int32 ForcedLOD = InView.Family->EngineShowFlags.LOD ? GetCVarForceLOD() : -1;
	const FLODMask* VisiblePrimitiveLODMask = nullptr;

	// #TODO Use highest LOD for rendering
	int8 LODToRenderScan = 0;
	FLODMask LODToRender;

	LODToRender.SetLOD(LODToRenderScan);

	const bool bCanCache = !InPrimitiveSceneInfo->NeedsUpdateStaticMeshes();

	for (int32 MeshIndex = 0; MeshIndex < InPrimitiveSceneInfo->StaticMeshRelevances.Num(); MeshIndex++)
	{
		const FStaticMeshBatchRelevance& StaticMeshRelevance = InPrimitiveSceneInfo->StaticMeshRelevances[MeshIndex];
		const FStaticMeshBatch& StaticMesh = InPrimitiveSceneInfo->StaticMeshes[MeshIndex];

		const FMaterial* Material = StaticMesh.MaterialRenderProxy->GetMaterial(FeatureLevel);

		bUsedTesselation |= (Material->GetTessellationMode() > EMaterialTessellationMode::MTM_NoTessellation);
		const bool bUsedWithRainOccluder = Material->IsUsedWithRainOccluder() && Material->GetTessellationMode() == EMaterialTessellationMode::MTM_NoTessellation;

		// #TODO
		if (bUsedWithRainOccluder && LODToRender.ContainsLOD(StaticMeshRelevance.LODIndex))
		{
			if (bCanCache)
			{
			#if WITH_EDITOR
				bool bAddSuccessful = AddCachedMeshDrawCommandsForPass(
					PrimitiveId,
					InPrimitiveSceneInfo,
					StaticMeshRelevance,
					StaticMesh,
					InPrimitiveSceneInfo->Scene,
					EMeshPass::RainDepthPass,
					RainDepthPassVisibleCommands,
					ProjectedMeshCommandBuildRequests,
					NumProjectedMeshCommandBuildRequestElements);

				if (!bAddSuccessful)
				{
					NumProjectedMeshCommandBuildRequestElements += StaticMeshRelevance.NumElements;
					ProjectedMeshCommandBuildRequests.Add(&StaticMesh);
					bDrawingStaticMeshes = true;
				}
			#endif
			}
		}
	}

	return bDrawingStaticMeshes;
}

void FRainDepthProjectedInfo::SetupMeshDrawCommandsForRainDepth(FSceneRenderer& Renderer, FRHIUniformBuffer* PassUniformBuffer)
{
	FMeshPassProcessorRenderState RainDepthPassState;
	SetupRainDepthPassState(RainDepthPassState);
	FRainDepthPassMeshProcessor* MeshPassProcessor = new(FMemStack::Get()) FRainDepthPassMeshProcessor(Renderer.Scene, RainDepthView, RainDepthPassState, nullptr);
	
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

	Renderer.DispatchedShadowDepthPasses.Add(&RainDepthCommandPass);
}

void FRainDepthProjectedInfo::RenderRainDepthInner(FRHICommandListImmediate& RHICmdList, FSceneRenderer* SceneRenderer)
{
	if (FSceneInterface::GetShadingPath(FeatureLevel) == EShadingPath::Mobile)
	{
		FRHIUniformBuffer* PassUniformBuffer = MobileRainDepthPassUniformBuffer;
		FMobileRainDepthPassUniformParameters RainDepthPassParameters;
 		SetupRainDepthPassUniformBuffer(this, RHICmdList, *RainDepthView, RainDepthPassParameters);
		SceneRenderer->Scene->UniformBuffers.MobileRainDepthPassUniformBuffer.UpdateUniformBufferImmediate(RainDepthPassParameters);
		MobileRainDepthPassUniformBuffer.UpdateUniformBufferImmediate(RainDepthPassParameters);
		PassUniformBuffer = SceneRenderer->Scene->UniformBuffers.MobileRainDepthPassUniformBuffer;

		RainDepthCommandPass.DispatchDraw(nullptr, RHICmdList);
	}
}

void FMobileSceneRenderer::InitRainDepthRendering(FGlobalDynamicIndexBuffer& InDynamicIndexBuffer, FGlobalDynamicVertexBuffer& InDynamicVertexBuffer, FGlobalDynamicReadBuffer& InDynamicReadBuffer)
{
#if WITH_EDITOR
	if(!Scene->RainDepthInfo || CVarRenderRain.GetValueOnRenderThread() <= 0)
 		if(!Scene->RainDepthInfo || !Scene->RainDepthInfo->RainDepthSceneProxy.bShouldSaveTexture) return;
	Scene->RainDepthInfo->Reset();
	Scene->RainDepthInfo->Init(*this, &Views[0]);
	//RainDepthProjectedInfo.Init(*this, &Views[0]);
	GatherRainDepthPrimitives(*Scene->RainDepthInfo, Scene);

	TArray<const FSceneView*> ReusedViewsArray;
	ReusedViewsArray.AddZeroed(1);
	Scene->RainDepthInfo->GatherDynamicMeshElements(*this, ReusedViewsArray, InDynamicIndexBuffer, InDynamicVertexBuffer, InDynamicReadBuffer);
#endif
}

void FMobileSceneRenderer::RenderRainDepth(FRHICommandListImmediate& RHICmdList)
{
#if WITH_EDITOR
	if(!Scene->RainDepthInfo || CVarRenderRain.GetValueOnRenderThread() <= 0)
		if(!Scene->RainDepthInfo || !Scene->RainDepthInfo->RainDepthSceneProxy.bShouldSaveTexture) return;
	Scene->RainDepthInfo->AllocateRainDepthTargets(RHICmdList);

	ERenderTargetLoadAction DepthLoadAction = ERenderTargetLoadAction::EClear;

	SCOPED_DRAW_EVENTF(RHICmdList, EventRainDepths, TEXT("RainDepths"));
	FRHIRenderPassInfo RPInfo(Scene->RainDepthInfo->RainDepthRenderTarget->GetRenderTargetItem().TargetableTexture, MakeDepthStencilTargetActions(MakeRenderTargetActions(DepthLoadAction, ERenderTargetStoreAction::EStore), ERenderTargetActions::Load_Store), nullptr, FExclusiveDepthStencil::DepthWrite_StencilWrite);
	RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, Scene->RainDepthInfo->RainDepthRenderTarget->GetRenderTargetItem().TargetableTexture->GetTexture2D());
	RHICmdList.BeginRenderPass(RPInfo, TEXT("RainDepth"));

	RHICmdList.SetViewport(0, 0, 0, Scene->RainDepthInfo->DepthResolutionX, Scene->RainDepthInfo->DepthResolutionY, 1);

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

	Scene->RainDepthInfo->RenderRainDepthInner(RHICmdList, this);

	RHICmdList.EndRenderPass();

	ConvertRainDepth(RHICmdList, *Scene->RainDepthInfo, Scene->RainDepthInfo->MobileRainDepthPassUniformBuffer, Scene->RainDepthInfo->RainDepthRenderTarget->GetRenderTargetItem().TargetableTexture->GetTexture2D());
#endif
}