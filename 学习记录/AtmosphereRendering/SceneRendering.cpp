// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SceneRendering.cpp: Scene rendering.
=============================================================================*/

#include "SceneRendering.h"
#include "ProfilingDebugging/ProfilingHelpers.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "EngineGlobals.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Components/ReflectionCaptureComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SceneCaptureComponentCube.h"
#include "DeferredShadingRenderer.h"
#include "DynamicPrimitiveDrawing.h"
#include "RenderTargetTemp.h"
#include "RendererModule.h"
#include "ScenePrivate.h"
#include "PostProcess/SceneFilterRendering.h"
#include "PostProcess/RenderingCompositionGraph.h"
#include "PostProcess/PostProcessEyeAdaptation.h"
#include "PostProcess/PostProcessSubsurface.h"
#include "PostProcess/PostProcessing.h"
#include "CompositionLighting/CompositionLighting.h"
#include "LegacyScreenPercentageDriver.h"
#include "SceneViewExtension.h"
#include "PostProcess/PostProcessBusyWait.h"
#include "AtmosphereRendering.h"
#include "Matinee/MatineeActor.h"
#include "ComponentRecreateRenderStateContext.h"
#include "PostProcess/PostProcessSubsurface.h"
#include "HdrCustomResolveShaders.h"
#include "WideCustomResolveShaders.h"
#include "PipelineStateCache.h"
#include "GPUSkinCache.h"
#include "PrecomputedVolumetricLightmap.h"
#include "RenderUtils.h"
#include "SceneUtils.h"
#include "DeviceProfiles/DeviceProfileManager.h"
#include "DeviceProfiles/DeviceProfile.h"
#include "PostProcess/PostProcessing.h"
#include "SceneSoftwareOcclusion.h"
#include "VirtualTexturing.h"
#include "VisualizeTexturePresent.h"
#include "GPUScene.h"
#include "TranslucentRendering.h"
#include "VisualizeTexture.h"
#include "VisualizeTexturePresent.h"
#include "MeshDrawCommands.h"
#include "VT/VirtualTextureSystem.h"
#include "HAL/LowLevelMemTracker.h"
#include "IXRTrackingSystem.h"
#include "IXRCamera.h"
#include "IXRCamera.h"
#include "IHeadMountedDisplay.h"
#include "DiaphragmDOF.h" 
#include "SingleLayerWaterRendering.h"
#include "HairStrands/HairStrandsVisibility.h"

/*-----------------------------------------------------------------------------
	Globals
-----------------------------------------------------------------------------*/

static TAutoConsoleVariable<int32> CVarCachedMeshDrawCommands(
	TEXT("r.MeshDrawCommands.UseCachedCommands"),
	1,
	TEXT("Whether to render from cached mesh draw commands (on vertex factories that support it), or to generate draw commands every frame."),
	ECVF_RenderThreadSafe);

bool UseCachedMeshDrawCommands()
{
	return CVarCachedMeshDrawCommands.GetValueOnRenderThread() > 0;
}

static TAutoConsoleVariable<int32> CVarMeshDrawCommandsDynamicInstancing(
	TEXT("r.MeshDrawCommands.DynamicInstancing"),
	1,
	TEXT("Whether to dynamically combine multiple compatible visible Mesh Draw Commands into one instanced draw on vertex factories that support it."),
	ECVF_RenderThreadSafe);

bool IsDynamicInstancingEnabled(ERHIFeatureLevel::Type FeatureLevel)
{
	return CVarMeshDrawCommandsDynamicInstancing.GetValueOnRenderThread() > 0
		&& UseGPUScene(GMaxRHIShaderPlatform, FeatureLevel);
}

int32 GDumpInstancingStats = 0;
FAutoConsoleVariableRef CVarDumpInstancingStats(
	TEXT("r.MeshDrawCommands.LogDynamicInstancingStats"),
	GDumpInstancingStats,
	TEXT("Whether to log dynamic instancing stats on the next frame"),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GDumpMeshDrawCommandMemoryStats = 0;
FAutoConsoleVariableRef CVarDumpMeshDrawCommandMemoryStats(
	TEXT("r.MeshDrawCommands.LogMeshDrawCommandMemoryStats"),
	GDumpMeshDrawCommandMemoryStats,
	TEXT("Whether to log mesh draw command memory stats on the next frame"),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

DECLARE_GPU_STAT_NAMED(CustomDepth, TEXT("Custom Depth"));

static TAutoConsoleVariable<int32> CVarCustomDepthTemporalAAJitter(
	TEXT("r.CustomDepthTemporalAAJitter"),
	1,
	TEXT("If disabled the Engine will remove the TemporalAA Jitter from the Custom Depth Pass. Only has effect when TemporalAA is used."),
	ECVF_RenderThreadSafe
);


/**
 * Console variable controlling whether or not occlusion queries are allowed.
 */
static TAutoConsoleVariable<int32> CVarAllowOcclusionQueries(
	TEXT("r.AllowOcclusionQueries"),
	1,
	TEXT("If zero, occlusion queries will not be used to cull primitives."),
	ECVF_RenderThreadSafe
	);

static TAutoConsoleVariable<float> CVarDemosaicVposOffset(
	TEXT("r.DemosaicVposOffset"),
	0.0f,
	TEXT("This offset is added to the rasterized position used for demosaic in the ES2 tonemapping shader. It exists to workaround driver bugs on some Android devices that have a half-pixel offset."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarRefractionQuality(
	TEXT("r.RefractionQuality"),
	2,
	TEXT("Defines the distorion/refraction quality which allows to adjust for quality or performance.\n")
	TEXT("<=0: off (fastest)\n")
	TEXT("  1: low quality (not yet implemented)\n")
	TEXT("  2: normal quality (default)\n")
	TEXT("  3: high quality (e.g. color fringe, not yet implemented)"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarInstancedStereo(
	TEXT("vr.InstancedStereo"),
	0,
	TEXT("0 to disable instanced stereo (default), 1 to enable."),
	ECVF_ReadOnly | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarMobileMultiView(
	TEXT("vr.MobileMultiView"),
	0,
	TEXT("0 to disable mobile multi-view, 1 to enable.\n"),
	ECVF_ReadOnly | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarRoundRobinOcclusion(
	TEXT("vr.RoundRobinOcclusion"),
	0,
	TEXT("0 to disable round-robin occlusion queries for stereo rendering (default), 1 to enable."),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarUsePreExposure(
	TEXT("r.UsePreExposure"),
	1,
	TEXT("0 to disable pre-exposure, 1 to enable it (default).\n")
	TEXT("Pre-exposure allows the engine to apply the last frame exposure to luminance values before writing them in rendertargets.\n")
	TEXT("It avoids rendertarget overflow when using low precision formats like fp16.\n")
	TEXT("The pre-exposure value can be overriden through r.EyeAdaptation.PreExposureOverride\n"),
	ECVF_ReadOnly | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarODSCapture(
	TEXT("vr.ODSCapture"),
	0,
	TEXT("Experimental")
	TEXT("0 to disable Omni-directional stereo capture (default), 1 to enable."),
	ECVF_ReadOnly | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarViewRectUseScreenBottom(
	TEXT("r.ViewRectUseScreenBottom"),
	0,
	TEXT("WARNING: This is an experimental, unsupported feature and does not work with all postprocesses (e.g DOF and DFAO)\n")
	TEXT("If enabled, the view rectangle will use the bottom left corner instead of top left"),
	ECVF_RenderThreadSafe
);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
static TAutoConsoleVariable<float> CVarGeneralPurposeTweak(
	TEXT("r.GeneralPurposeTweak"),
	1.0f,
	TEXT("Useful for low level shader development to get quick iteration time without having to change any c++ code.\n")
	TEXT("Value maps to Frame.GeneralPurposeTweak inside the shaders.\n")
	TEXT("Example usage: Multiplier on some value to tweak, toggle to switch between different algorithms (Default: 1.0)\n")
	TEXT("DON'T USE THIS FOR ANYTHING THAT IS CHECKED IN. Compiled out in SHIPPING to make cheating a bit harder."),
	ECVF_RenderThreadSafe);


static TAutoConsoleVariable<int32> CVarDisplayInternals(
	TEXT("r.DisplayInternals"),
	0,
	TEXT("Allows to enable screen printouts that show the internals on the engine/renderer\n")
	TEXT("This is mostly useful to be able to reason why a screenshots looks different.\n")
	TEXT(" 0: off (default)\n")
	TEXT(" 1: enabled"),
	ECVF_RenderThreadSafe | ECVF_Cheat);
#endif

/**
 * Console variable controlling the maximum number of shadow cascades to render with.
 *   DO NOT READ ON THE RENDERING THREAD. Use FSceneView::MaxShadowCascades.
 */
static TAutoConsoleVariable<int32> CVarMaxShadowCascades(
	TEXT("r.Shadow.CSM.MaxCascades"),
	10,
	TEXT("The maximum number of cascades with which to render dynamic directional light shadows."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

static TAutoConsoleVariable<int32> CVarMaxMobileShadowCascades(
	TEXT("r.Shadow.CSM.MaxMobileCascades"),
	2,
	TEXT("The maximum number of cascades with which to render dynamic directional light shadows when using the mobile renderer."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarSupportSimpleForwardShading(
	TEXT("r.SupportSimpleForwardShading"),
	0,
	TEXT("Whether to compile the shaders to support r.SimpleForwardShading being enabled (PC only)."),
	ECVF_RenderThreadSafe | ECVF_ReadOnly);

static TAutoConsoleVariable<int32> CVarSimpleForwardShading(
	TEXT("r.SimpleForwardShading"),
	0,
	TEXT("Whether to use the simple forward shading base pass shaders which only support lightmaps + stationary directional light + stationary skylight\n")
	TEXT("All other lighting features are disabled when true.  This is useful for supporting very low end hardware, and is only supported on PC platforms.\n")
	TEXT("0:off, 1:on"),
	ECVF_RenderThreadSafe | ECVF_Scalability);

// Keep track of the previous value for CVarSimpleForwardShading so we can avoid costly updates when it hasn't actually changed
static int32 CVarSimpleForwardShading_PreviousValue = 0;

static TAutoConsoleVariable<float> CVarNormalCurvatureToRoughnessBias(
	TEXT("r.NormalCurvatureToRoughnessBias"),
	0.0f,
	TEXT("Biases the roughness resulting from screen space normal changes for materials with NormalCurvatureToRoughness enabled.  Valid range [-1, 1]"),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarNormalCurvatureToRoughnessExponent(
	TEXT("r.NormalCurvatureToRoughnessExponent"),
	0.333f,
	TEXT("Exponent on the roughness resulting from screen space normal changes for materials with NormalCurvatureToRoughness enabled."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarNormalCurvatureToRoughnessScale(
	TEXT("r.NormalCurvatureToRoughnessScale"),
	1.0f,
	TEXT("Scales the roughness resulting from screen space normal changes for materials with NormalCurvatureToRoughness enabled.  Valid range [0, 2]"),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarEnableMultiGPUForkAndJoin(
	TEXT("r.EnableMultiGPUForkAndJoin"),
	1,
	TEXT("Whether to allow unused GPUs to speedup rendering by sharing work.\n"),
	ECVF_Default
	);

/*-----------------------------------------------------------------------------
	FParallelCommandListSet
-----------------------------------------------------------------------------*/


static TAutoConsoleVariable<int32> CVarRHICmdSpewParallelListBalance(
	TEXT("r.RHICmdSpewParallelListBalance"),
	0,
	TEXT("For debugging, spews the size of the parallel command lists. This stalls and otherwise wrecks performance.\n")
	TEXT(" 0: off (default)\n")
	TEXT(" 1: enabled (default)"));

static TAutoConsoleVariable<int32> CVarRHICmdBalanceParallelLists(
	TEXT("r.RHICmdBalanceParallelLists"),
	2,
	TEXT("Allows to enable a preprocess of the drawlists to try to balance the load equally among the command lists.\n")
	TEXT(" 0: off \n")
	TEXT(" 1: enabled")
	TEXT(" 2: experiemental, uses previous frame results (does not do anything in split screen etc)"));

static TAutoConsoleVariable<int32> CVarRHICmdMinCmdlistForParallelSubmit(
	TEXT("r.RHICmdMinCmdlistForParallelSubmit"),
	1,
	TEXT("Minimum number of parallel translate command lists to submit. If there are fewer than this number, they just run on the RHI thread and immediate context."));

static TAutoConsoleVariable<int32> CVarRHICmdMinDrawsPerParallelCmdList(
	TEXT("r.RHICmdMinDrawsPerParallelCmdList"),
	64,
	TEXT("The minimum number of draws per cmdlist. If the total number of draws is less than this, then no parallel work will be done at all. This can't always be honored or done correctly. More effective with RHICmdBalanceParallelLists."));

static TAutoConsoleVariable<int32> CVarWideCustomResolve(
	TEXT("r.WideCustomResolve"),
	0,
	TEXT("Use a wide custom resolve filter when MSAA is enabled")
	TEXT("0: Disabled [hardware box filter]")
	TEXT("1: Wide (r=1.25, 12 samples)")
	TEXT("2: Wider (r=1.4, 16 samples)")
	TEXT("3: Widest (r=1.5, 20 samples)"),
	ECVF_RenderThreadSafe | ECVF_Scalability
	);

TAutoConsoleVariable<int32> CVarTransientResourceAliasing_Buffers(
	TEXT("r.TransientResourceAliasing.Buffers"),
	1,
	TEXT("Enables transient resource aliasing for specified buffers. Used only if GSupportsTransientResourceAliasing is true.\n"),
	ECVF_ReadOnly);

#if !UE_BUILD_SHIPPING

static TAutoConsoleVariable<int32> CVarTestInternalViewRectOffset(
	TEXT("r.Test.ViewRectOffset"),
	0,
	TEXT("Moves the view rect within the renderer's internal render target.\n")
	TEXT(" 0: disabled (default);"));

static TAutoConsoleVariable<int32> CVarTestCameraCut(
	TEXT("r.Test.CameraCut"),
	0,
	TEXT("Force enabling camera cut for testing purposes.\n")
	TEXT(" 0: disabled (default); 1: enabled."));

static TAutoConsoleVariable<int32> CVarTestScreenPercentageInterface(
	TEXT("r.Test.DynamicResolutionHell"),
	0,
	TEXT("Override the screen percentage interface for all view family with dynamic resolution hell.\n")
	TEXT(" 0: off (default);\n")
	TEXT(" 1: Dynamic resolution hell."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarTestPrimaryScreenPercentageMethodOverride(
	TEXT("r.Test.PrimaryScreenPercentageMethodOverride"),
	0,
	TEXT("Override the screen percentage method for all view family.\n")
	TEXT(" 0: view family's screen percentage interface choose; (default)\n")
	TEXT(" 1: old fashion upscaling pass at the very end right before before UI;\n")
	TEXT(" 2: TemporalAA upsample."));

static TAutoConsoleVariable<int32> CVarTestSecondaryUpscaleOverride(
	TEXT("r.Test.SecondaryUpscaleOverride"),
	0,
	TEXT("Override the secondary upscale.\n")
	TEXT(" 0: disabled; (default)\n")
	TEXT(" 1: use secondary view fraction = 0.5 with nearest secondary upscale."));

#endif

static FParallelCommandListSet* GOutstandingParallelCommandListSet = nullptr;
FGraphEventRef FSceneRenderer::OcclusionSubmittedFence[FOcclusionQueryHelpers::MaxBufferedOcclusionFrames];


DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer ViewExtensionPostRenderView"), STAT_FDeferredShadingSceneRenderer_ViewExtensionPostRenderView, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer ViewExtensionPreRenderView"), STAT_FDeferredShadingSceneRenderer_ViewExtensionPreRenderView, STATGROUP_SceneRendering);

#define FASTVRAM_CVAR(Name,DefaultValue) static TAutoConsoleVariable<int32> CVarFastVRam_##Name(TEXT("r.FastVRam."#Name), DefaultValue, TEXT(""))

FASTVRAM_CVAR(GBufferA, 0);
FASTVRAM_CVAR(GBufferB, 1);
FASTVRAM_CVAR(GBufferC, 0);
FASTVRAM_CVAR(GBufferD, 0);
FASTVRAM_CVAR(GBufferE, 0);
FASTVRAM_CVAR(GBufferF, 0);
FASTVRAM_CVAR(GBufferVelocity, 0);
FASTVRAM_CVAR(HZB, 1);
FASTVRAM_CVAR(SceneDepth, 1);
FASTVRAM_CVAR(SceneColor, 1);
FASTVRAM_CVAR(LPV, 1);
FASTVRAM_CVAR(BokehDOF, 1);
FASTVRAM_CVAR(CircleDOF, 1);
FASTVRAM_CVAR(CombineLUTs, 1);
FASTVRAM_CVAR(Downsample, 1);
FASTVRAM_CVAR(EyeAdaptation, 1);
FASTVRAM_CVAR(Histogram, 1);
FASTVRAM_CVAR(HistogramReduce, 1);
FASTVRAM_CVAR(VelocityFlat, 1);
FASTVRAM_CVAR(VelocityMax, 1);
FASTVRAM_CVAR(MotionBlur, 1);
FASTVRAM_CVAR(Tonemap, 1);
FASTVRAM_CVAR(Upscale, 1);
FASTVRAM_CVAR(DistanceFieldNormal, 1);
FASTVRAM_CVAR(DistanceFieldAOHistory, 1);
FASTVRAM_CVAR(DistanceFieldAODownsampledBentNormal, 1); 
FASTVRAM_CVAR(DistanceFieldAOBentNormal, 0); 
FASTVRAM_CVAR(DistanceFieldIrradiance, 0); 
FASTVRAM_CVAR(DistanceFieldShadows, 1);
FASTVRAM_CVAR(Distortion, 1);
FASTVRAM_CVAR(ScreenSpaceShadowMask, 1);
FASTVRAM_CVAR(VolumetricFog, 1);
FASTVRAM_CVAR(SeparateTranslucency, 0); 
FASTVRAM_CVAR(SeparateTranslucencyModulate, 0); 
FASTVRAM_CVAR(LightAccumulation, 0); 
FASTVRAM_CVAR(LightAttenuation, 0); 
FASTVRAM_CVAR(ScreenSpaceAO,0);
FASTVRAM_CVAR(SSR, 0);
FASTVRAM_CVAR(DBufferA, 0);
FASTVRAM_CVAR(DBufferB, 0);
FASTVRAM_CVAR(DBufferC, 0); 
FASTVRAM_CVAR(DBufferMask, 0);
FASTVRAM_CVAR(DOFSetup, 1);
FASTVRAM_CVAR(DOFReduce, 1);
FASTVRAM_CVAR(DOFPostfilter, 1);
FASTVRAM_CVAR(PostProcessMaterial, 1);

FASTVRAM_CVAR(CustomDepth, 0);
FASTVRAM_CVAR(ShadowPointLight, 0);
FASTVRAM_CVAR(ShadowPerObject, 0);
FASTVRAM_CVAR(ShadowCSM, 0);

FASTVRAM_CVAR(DistanceFieldCulledObjectBuffers, 1);
FASTVRAM_CVAR(DistanceFieldTileIntersectionResources, 1);
FASTVRAM_CVAR(DistanceFieldAOScreenGridResources, 1);
FASTVRAM_CVAR(ForwardLightingCullingResources, 1);
FASTVRAM_CVAR(GlobalDistanceFieldCullGridBuffers, 1);


#if !UE_BUILD_SHIPPING
namespace
{

/*
 * Screen percentage interface that is just constantly changing res to test resolution changes.
 */
class FScreenPercentageHellDriver : public ISceneViewFamilyScreenPercentage
{
public:

	FScreenPercentageHellDriver(const FSceneViewFamily& InViewFamily)
		: ViewFamily(InViewFamily)
	{ }

	virtual float GetPrimaryResolutionFractionUpperBound() const override
	{
		return 1.0f;
	}

	virtual ISceneViewFamilyScreenPercentage* Fork_GameThread(const class FSceneViewFamily& ForkedViewFamily) const override
	{
		check(IsInGameThread());

		if (ForkedViewFamily.Views[0]->State)
		{
			return new FScreenPercentageHellDriver(ForkedViewFamily);
		}

		return new FLegacyScreenPercentageDriver(
			ForkedViewFamily, /* GlobalResolutionFraction = */ 1.0f, /* AllowPostProcessSettingsScreenPercentage = */ false);
	}

	virtual void ComputePrimaryResolutionFractions_RenderThread(TArray<FSceneViewScreenPercentageConfig>& OutViewScreenPercentageConfigs) const override
	{
		check(IsInRenderingThread());

		// Early return if no screen percentage should be done.
		if (!ViewFamily.EngineShowFlags.ScreenPercentage)
		{
			return;
		}

		uint32 FrameId = 0;

		const FSceneViewState* ViewState = static_cast<const FSceneViewState*>(ViewFamily.Views[0]->State);
		if (ViewState)
		{
			FrameId = ViewState->GetFrameIndex(8);
		}

		float ResolutionFraction = FrameId == 0 ? 1.f : (FMath::Cos((FrameId + 0.25) * PI / 8) * 0.25f + 0.75f);

		for (int32 i = 0; i < ViewFamily.Views.Num(); i++)
		{
			OutViewScreenPercentageConfigs[i].PrimaryResolutionFraction = ResolutionFraction;
		}
	}

private:
	// View family to take care of.
	const FSceneViewFamily& ViewFamily;

};

} // namespace
#endif // !UE_BUILD_SHIPPING


FFastVramConfig::FFastVramConfig()
{
	FMemory::Memset(*this, 0);
}

void FFastVramConfig::Update()
{
	bDirty = false;
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_GBufferA, GBufferA);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_GBufferB, GBufferB);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_GBufferC, GBufferC);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_GBufferD, GBufferD);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_GBufferE, GBufferE);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_GBufferF, GBufferF);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_GBufferVelocity, GBufferVelocity);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_HZB, HZB);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_SceneDepth, SceneDepth);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_SceneColor, SceneColor);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_LPV, LPV);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_BokehDOF, BokehDOF);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_CircleDOF, CircleDOF);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_CombineLUTs, CombineLUTs);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_Downsample, Downsample);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_EyeAdaptation, EyeAdaptation);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_Histogram, Histogram);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_HistogramReduce, HistogramReduce);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_VelocityFlat, VelocityFlat);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_VelocityMax, VelocityMax);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_MotionBlur, MotionBlur);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_Tonemap, Tonemap);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_Upscale, Upscale);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_DistanceFieldNormal, DistanceFieldNormal);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_DistanceFieldAOHistory, DistanceFieldAOHistory);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_DistanceFieldAODownsampledBentNormal, DistanceFieldAODownsampledBentNormal);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_DistanceFieldAOBentNormal, DistanceFieldAOBentNormal);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_DistanceFieldIrradiance, DistanceFieldIrradiance);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_DistanceFieldShadows, DistanceFieldShadows);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_Distortion, Distortion);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_ScreenSpaceShadowMask, ScreenSpaceShadowMask);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_VolumetricFog, VolumetricFog);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_SeparateTranslucency, SeparateTranslucency);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_SeparateTranslucencyModulate, SeparateTranslucencyModulate);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_LightAccumulation, LightAccumulation);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_LightAttenuation, LightAttenuation);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_ScreenSpaceAO, ScreenSpaceAO);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_SSR, SSR);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_DBufferA, DBufferA);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_DBufferB, DBufferB);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_DBufferC, DBufferC);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_DBufferMask, DBufferMask);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_DOFSetup, DOFSetup);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_DOFReduce, DOFReduce);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_DOFPostfilter, DOFPostfilter);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_CustomDepth, CustomDepth);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_ShadowPointLight, ShadowPointLight);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_ShadowPerObject, ShadowPerObject);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_ShadowCSM, ShadowCSM);
	bDirty |= UpdateTextureFlagFromCVar(CVarFastVRam_PostProcessMaterial, PostProcessMaterial);

	bDirty |= UpdateBufferFlagFromCVar(CVarFastVRam_DistanceFieldCulledObjectBuffers, DistanceFieldCulledObjectBuffers);
	bDirty |= UpdateBufferFlagFromCVar(CVarFastVRam_DistanceFieldTileIntersectionResources, DistanceFieldTileIntersectionResources);
	bDirty |= UpdateBufferFlagFromCVar(CVarFastVRam_DistanceFieldAOScreenGridResources, DistanceFieldAOScreenGridResources);
	bDirty |= UpdateBufferFlagFromCVar(CVarFastVRam_ForwardLightingCullingResources, ForwardLightingCullingResources);
	bDirty |= UpdateBufferFlagFromCVar(CVarFastVRam_GlobalDistanceFieldCullGridBuffers, GlobalDistanceFieldCullGridBuffers);
}

bool FFastVramConfig::UpdateTextureFlagFromCVar(TAutoConsoleVariable<int32>& CVar, uint32& InOutValue)
{
	uint32 OldValue = InOutValue;
	int32 CVarValue = CVar.GetValueOnRenderThread();
	InOutValue = TexCreate_None;
	if (CVarValue == 1)
	{
		InOutValue = TexCreate_FastVRAM;
	}
	else if (CVarValue == 2)
	{
		InOutValue = TexCreate_FastVRAM | TexCreate_FastVRAMPartialAlloc;
	}
	return OldValue != InOutValue;
}

bool FFastVramConfig::UpdateBufferFlagFromCVar(TAutoConsoleVariable<int32>& CVar, uint32& InOutValue)
{
	uint32 OldValue = InOutValue;
	InOutValue = CVar.GetValueOnRenderThread() ? ( BUF_FastVRAM ) : BUF_None;
	return OldValue != InOutValue;
}

FFastVramConfig GFastVRamConfig;


FParallelCommandListSet::FParallelCommandListSet(
	TStatId InExecuteStat, 
	const FViewInfo& InView, 
	const FSceneRenderer* InSceneRenderer, 
	FRHICommandListImmediate& InParentCmdList, 
	bool bInParallelExecute, 
	bool bInCreateSceneContext,
	const FMeshPassProcessorRenderState& InDrawRenderState)
	: View(InView)
	, SceneRenderer(InSceneRenderer)
	, DrawRenderState(InDrawRenderState)
	, ParentCmdList(InParentCmdList)
	, Snapshot(nullptr)
	, ExecuteStat(InExecuteStat)
	, NumAlloc(0)
	, bParallelExecute(GRHISupportsParallelRHIExecute && bInParallelExecute)
	, bCreateSceneContext(bInCreateSceneContext)
{
	Width = CVarRHICmdWidth.GetValueOnRenderThread();
	MinDrawsPerCommandList = CVarRHICmdMinDrawsPerParallelCmdList.GetValueOnRenderThread();
	bSpewBalance = !!CVarRHICmdSpewParallelListBalance.GetValueOnRenderThread();
	int32 IntBalance = CVarRHICmdBalanceParallelLists.GetValueOnRenderThread();
	bBalanceCommands = !!IntBalance;
	CommandLists.Reserve(Width * 8);
	Events.Reserve(Width * 8);
	NumDrawsIfKnown.Reserve(Width * 8);
	check(!GOutstandingParallelCommandListSet);
	GOutstandingParallelCommandListSet = this;
}

FRHICommandList* FParallelCommandListSet::AllocCommandList()
{
	NumAlloc++;
	return new FRHICommandList(ParentCmdList.GetGPUMask());
}

void FParallelCommandListSet::Dispatch(bool bHighPriority)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FParallelCommandListSet_Dispatch);
	check(IsInRenderingThread() && FMemStack::Get().GetNumMarks() == 1); // we do not want this popped before the end of the scene and it better be the scene allocator
	check(CommandLists.Num() == Events.Num());
	check(CommandLists.Num() == NumAlloc);

	// We should not be submitting work off a parent command list if it's still in the middle of a renderpass.
	// This is a bit weird since we will (likely) end up opening one in the parallel translate case but until we have
	// a cleaner way for the RHI to specify parallel passes this is what we've got.
	check(ParentCmdList.IsOutsideRenderPass());

	ENamedThreads::Type RenderThread_Local = ENamedThreads::GetRenderThread_Local();
	if (bSpewBalance)
	{
		// finish them all
		for (auto& Event : Events)
		{
			FTaskGraphInterface::Get().WaitUntilTaskCompletes(Event, RenderThread_Local);
		}
		// spew sizes
		int32 Index = 0;
		for (auto CmdList : CommandLists)
		{
			UE_LOG(LogTemp, Display, TEXT("CmdList %2d/%2d  : %8dKB"), Index, CommandLists.Num(), (CmdList->GetUsedMemory() + 1023) / 1024);
			Index++;
		}
	}
	bool bActuallyDoParallelTranslate = bParallelExecute && CommandLists.Num() >= CVarRHICmdMinCmdlistForParallelSubmit.GetValueOnRenderThread();
	if (bActuallyDoParallelTranslate)
	{
		int32 Total = 0;
		bool bIndeterminate = false;
		for (int32 Count : NumDrawsIfKnown)
		{
			if (Count < 0)
			{
				bIndeterminate = true;
				break; // can't determine how many are in this one; assume we should run parallel translate
			}
			Total += Count;
		}
		if (!bIndeterminate && Total < MinDrawsPerCommandList)
		{
			UE_CLOG(bSpewBalance, LogTemp, Display, TEXT("Disabling parallel translate because the number of draws is known to be small."));
			bActuallyDoParallelTranslate = false;
		}
	}

	if (bActuallyDoParallelTranslate)
	{
		UE_CLOG(bSpewBalance, LogTemp, Display, TEXT("%d cmdlists for parallel translate"), CommandLists.Num());
		check(GRHISupportsParallelRHIExecute);
		NumAlloc -= CommandLists.Num();
		ParentCmdList.QueueParallelAsyncCommandListSubmit(&Events[0], bHighPriority, &CommandLists[0], &NumDrawsIfKnown[0], CommandLists.Num(), (MinDrawsPerCommandList * 4) / 3, bSpewBalance);
		// #todo-renderpasses PS4 breaks if this isn't here. Why?
		SetStateOnCommandList(ParentCmdList);
		ParentCmdList.EndRenderPass();
	}
	else
	{
		UE_CLOG(bSpewBalance, LogTemp, Display, TEXT("%d cmdlists (no parallel translate desired)"), CommandLists.Num());
		for (int32 Index = 0; Index < CommandLists.Num(); Index++)
		{
			ParentCmdList.QueueAsyncCommandListSubmit(Events[Index], CommandLists[Index]);
			NumAlloc--;
		}
	}
	CommandLists.Reset();
	Snapshot = nullptr;
	Events.Reset();
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FParallelCommandListSet_Dispatch_ServiceLocalQueue);
	FTaskGraphInterface::Get().ProcessThreadUntilIdle(RenderThread_Local);
}

FParallelCommandListSet::~FParallelCommandListSet()
{
	check(GOutstandingParallelCommandListSet == this);
	GOutstandingParallelCommandListSet = nullptr;

	check(IsInRenderingThread() && FMemStack::Get().GetNumMarks() == 1); // we do not want this popped before the end of the scene and it better be the scene allocator
	checkf(CommandLists.Num() == 0, TEXT("Derived class of FParallelCommandListSet did not call Dispatch in virtual destructor"));
	checkf(NumAlloc == 0, TEXT("Derived class of FParallelCommandListSet did not call Dispatch in virtual destructor"));
}

FRHICommandList* FParallelCommandListSet::NewParallelCommandList()
{
	FRHICommandList* Result = AllocCommandList();
	Result->ExecuteStat = ExecuteStat;
	SetStateOnCommandList(*Result);
	if (bCreateSceneContext)
	{
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(ParentCmdList);
		check(&SceneContext == &FSceneRenderTargets::Get_FrameConstantsOnly()); // the immediate should not have an overridden context
		if (!Snapshot)
		{
			Snapshot = SceneContext.CreateSnapshot(View);
		}
		Snapshot->SetSnapshotOnCmdList(*Result);
		check(&SceneContext != &FSceneRenderTargets::Get(*Result)); // the new commandlist should have a snapshot
	}
	return Result;
}

void FParallelCommandListSet::AddParallelCommandList(FRHICommandList* CmdList, FGraphEventRef& CompletionEvent, int32 InNumDrawsIfKnown)
{
	check(IsInRenderingThread() && FMemStack::Get().GetNumMarks() == 1); // we do not want this popped before the end of the scene and it better be the scene allocator
	check(CommandLists.Num() == Events.Num());
	CommandLists.Add(CmdList);
	Events.Add(CompletionEvent);
	NumDrawsIfKnown.Add(InNumDrawsIfKnown);
}

void FParallelCommandListSet::WaitForTasks()
{
	if (GOutstandingParallelCommandListSet)
	{
		GOutstandingParallelCommandListSet->WaitForTasksInternal();
	}
}

void FParallelCommandListSet::WaitForTasksInternal()
{
	check(IsInRenderingThread());
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FParallelCommandListSet_WaitForTasks);
	FGraphEventArray WaitOutstandingTasks;
	for (int32 Index = 0; Index < Events.Num(); Index++)
	{
		if (!Events[Index]->IsComplete())
		{
			WaitOutstandingTasks.Add(Events[Index]);
		}
	}
	if (WaitOutstandingTasks.Num())
	{
		ENamedThreads::Type RenderThread_Local = ENamedThreads::GetRenderThread_Local();
		check(!FTaskGraphInterface::Get().IsThreadProcessingTasks(RenderThread_Local));
		FTaskGraphInterface::Get().WaitUntilTasksComplete(WaitOutstandingTasks, RenderThread_Local);
	}
}

bool IsHMDHiddenAreaMaskActive()
{
	// Query if we have a custom HMD post process mesh to use
	static const auto* const HiddenAreaMaskCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("vr.HiddenAreaMask"));

	return
		HiddenAreaMaskCVar != nullptr &&
		// Any thread is used due to FViewInfo initialization.
		HiddenAreaMaskCVar->GetValueOnAnyThread() == 1 &&
		GEngine &&
		GEngine->XRSystem.IsValid() && GEngine->XRSystem->GetHMDDevice() &&
		GEngine->XRSystem->GetHMDDevice()->HasVisibleAreaMesh();
}

/*-----------------------------------------------------------------------------
	FViewInfo
-----------------------------------------------------------------------------*/

/** 
 * Initialization constructor. Passes all parameters to FSceneView constructor
 */
FViewInfo::FViewInfo(const FSceneViewInitOptions& InitOptions)
	:	FSceneView(InitOptions)
	,	IndividualOcclusionQueries((FSceneViewState*)InitOptions.SceneViewStateInterface, 1)	
	,	GroupedOcclusionQueries((FSceneViewState*)InitOptions.SceneViewStateInterface, FOcclusionQueryBatcher::OccludedPrimitiveQueryBatchSize)
{
	Init();
}

/** 
 * Initialization constructor. 
 * @param InView - copy to init with
 */
FViewInfo::FViewInfo(const FSceneView* InView)
	:	FSceneView(*InView)
	,	IndividualOcclusionQueries((FSceneViewState*)InView->State,1)	
	,	GroupedOcclusionQueries((FSceneViewState*)InView->State,FOcclusionQueryBatcher::OccludedPrimitiveQueryBatchSize)
	,	CustomVisibilityQuery(nullptr)
{
	Init();
}

void FViewInfo::Init()
{
	ViewRect = FIntRect(0, 0, 0, 0);

	CachedViewUniformShaderParameters = nullptr;
	bHasNoVisiblePrimitive = false;
	bHasTranslucentViewMeshElements = 0;
	bPrevTransformsReset = false;
	bIgnoreExistingQueries = false;
	bDisableQuerySubmissions = false;
	bDisableDistanceBasedFadeTransitions = false;	
	ShadingModelMaskInView = 0;
	bSceneHasSkyMaterial = 0;
	bHasSingleLayerWaterMaterial = 0;
	bHasTranslucencySeparateModulation = 0;

	NumVisibleStaticMeshElements = 0;
	PrecomputedVisibilityData = 0;
	bSceneHasDecals = 0;

	bIsViewInfo = true;
	
	bStatePrevViewInfoIsReadOnly = true;
	bUsesGlobalDistanceField = false;
	bUsesLightingChannels = false;
	bTranslucentSurfaceLighting = false;
	bUsesSceneDepth = false;
	bFogOnlyOnRenderedOpaque = false;

	ExponentialFogParameters = FVector4(0,1,1,0);
	ExponentialFogParameters2 = FVector4(0, 1, 0, 0);
	ExponentialFogColor = FVector::ZeroVector;
	FogMaxOpacity = 1;
	ExponentialFogParameters3 = FVector4(0, 0, 0, 0);
	SinCosInscatteringColorCubemapRotation = FVector2D(0, 0);
	FogInscatteringColorCubemap = NULL;
	FogInscatteringTextureParameters = FVector::ZeroVector;

	SkyAtmosphereCameraAerialPerspectiveVolume = nullptr;
	SkyAtmosphereUniformShaderParameters = nullptr;

	bUseDirectionalInscattering = false;
	DirectionalInscatteringExponent = 0;
	DirectionalInscatteringStartDistance = 0;
	InscatteringLightDirection = FVector(0);
	DirectionalInscatteringColor = FLinearColor(ForceInit);

	for (int32 CascadeIndex = 0; CascadeIndex < TVC_MAX; CascadeIndex++)
	{
		TranslucencyLightingVolumeMin[CascadeIndex] = FVector(0);
		TranslucencyVolumeVoxelSize[CascadeIndex] = 0;
		TranslucencyLightingVolumeSize[CascadeIndex] = FVector(0);
	}

	const int32 MaxMobileShadowCascadeCount = FMath::Clamp(CVarMaxMobileShadowCascades.GetValueOnAnyThread(), 0, MAX_MOBILE_SHADOWCASCADES);
	const int32 MaxShadowCascadeCountUpperBound = GetFeatureLevel() >= ERHIFeatureLevel::SM5 ? 10 : MaxMobileShadowCascadeCount;

	MaxShadowCascades = FMath::Clamp<int32>(CVarMaxShadowCascades.GetValueOnAnyThread(), 0, MaxShadowCascadeCountUpperBound);

	ShaderMap = GetGlobalShaderMap(FeatureLevel);

	ViewState = (FSceneViewState*)State;
	bIsSnapshot = false;
	bHMDHiddenAreaMaskActive = IsHMDHiddenAreaMaskActive();
	bUseComputePasses = IsPostProcessingWithComputeEnabled(FeatureLevel);
	bHasCustomDepthPrimitives = false;
	bHasDistortionPrimitives = false;
	bAllowStencilDither = false;
	bCustomDepthStencilValid = false;
	bUsesCustomDepthStencilInTranslucentMaterials = false;

	ForwardLightingResources = nullptr;

	NumBoxReflectionCaptures = 0;
	NumSphereReflectionCaptures = 0;
	FurthestReflectionCaptureDistance = 0;

	// Disable HDR encoding for editor elements.
	EditorSimpleElementCollector.BatchedElements.EnableMobileHDREncoding(false);
	
	TemporalJitterSequenceLength = 1;
	TemporalJitterIndex = 0;
	TemporalJitterPixels = FVector2D::ZeroVector;

	PreExposure = 1.0f;

	// Cache TEXTUREGROUP_World's for the render thread to create the material textures' shared sampler.
	if (IsInGameThread())
	{
		WorldTextureGroupSamplerFilter = (ESamplerFilter)UDeviceProfileManager::Get().GetActiveProfile()->GetTextureLODSettings()->GetSamplerFilter(TEXTUREGROUP_World);
		bIsValidWorldTextureGroupSamplerFilter = true;
	}
	else
	{
		bIsValidWorldTextureGroupSamplerFilter = false;
	}

	PrimitiveSceneDataOverrideSRV = nullptr;
	PrimitiveSceneDataTextureOverrideRHI = nullptr;
	LightmapSceneDataOverrideSRV = nullptr;

	DitherFadeInUniformBuffer = nullptr;
	DitherFadeOutUniformBuffer = nullptr;

	for (int32 PassIndex = 0; PassIndex < EMeshPass::Num; ++PassIndex)
	{
		NumVisibleDynamicMeshElements[PassIndex] = 0;
	}

	NumVisibleDynamicPrimitives = 0;
	NumVisibleDynamicEditorPrimitives = 0;
}

FViewInfo::~FViewInfo()
{
	for(int32 ResourceIndex = 0;ResourceIndex < DynamicResources.Num();ResourceIndex++)
	{
		DynamicResources[ResourceIndex]->ReleasePrimitiveResource();
	}
	if (CustomVisibilityQuery)
	{
		CustomVisibilityQuery->Release();
	}

	for (int32 MeshDrawIndex = 0; MeshDrawIndex < EMeshPass::Num; MeshDrawIndex++)
	{
		ParallelMeshDrawCommandPasses[MeshDrawIndex].WaitForTasksAndEmpty();
	}

	//this uses memstack allocation for strongrefs, so we need to manually empty to get the destructor called to not leak the uniformbuffers stored here.
	TranslucentSelfShadowUniformBufferMap.Empty();
}

#if DO_CHECK
bool FViewInfo::VerifyMembersChecks() const
{
	FSceneView::VerifyMembersChecks();

	check(ViewState == State);

	return true;
}
#endif

void FViewInfo::SetupSkyIrradianceEnvironmentMapConstants(FVector4* OutSkyIrradianceEnvironmentMap) const
{
	FScene* Scene = nullptr;

	if (Family->Scene)
	{
		Scene = Family->Scene->GetRenderScene();
	}

	if (Scene 
		&& Scene->SkyLight 
		// Skylights with static lighting already had their diffuse contribution baked into lightmaps
		&& !Scene->SkyLight->bHasStaticLighting
		&& Family->EngineShowFlags.SkyLighting)
	{
		const FSHVectorRGB3& SkyIrradiance = Scene->SkyLight->IrradianceEnvironmentMap;

		const float SqrtPI = FMath::Sqrt(PI);
		const float Coefficient0 = 1.0f / (2 * SqrtPI);
		const float Coefficient1 = FMath::Sqrt(3) / (3 * SqrtPI);
		const float Coefficient2 = FMath::Sqrt(15) / (8 * SqrtPI);
		const float Coefficient3 = FMath::Sqrt(5) / (16 * SqrtPI);
		const float Coefficient4 = .5f * Coefficient2;

		// Pack the SH coefficients in a way that makes applying the lighting use the least shader instructions
		// This has the diffuse convolution coefficients baked in
		// See "Stupid Spherical Harmonics (SH) Tricks"
		OutSkyIrradianceEnvironmentMap[0].X = -Coefficient1 * SkyIrradiance.R.V[3];
		OutSkyIrradianceEnvironmentMap[0].Y = -Coefficient1 * SkyIrradiance.R.V[1];
		OutSkyIrradianceEnvironmentMap[0].Z = Coefficient1 * SkyIrradiance.R.V[2];
		OutSkyIrradianceEnvironmentMap[0].W = Coefficient0 * SkyIrradiance.R.V[0] - Coefficient3 * SkyIrradiance.R.V[6];

		OutSkyIrradianceEnvironmentMap[1].X = -Coefficient1 * SkyIrradiance.G.V[3];
		OutSkyIrradianceEnvironmentMap[1].Y = -Coefficient1 * SkyIrradiance.G.V[1];
		OutSkyIrradianceEnvironmentMap[1].Z = Coefficient1 * SkyIrradiance.G.V[2];
		OutSkyIrradianceEnvironmentMap[1].W = Coefficient0 * SkyIrradiance.G.V[0] - Coefficient3 * SkyIrradiance.G.V[6];

		OutSkyIrradianceEnvironmentMap[2].X = -Coefficient1 * SkyIrradiance.B.V[3];
		OutSkyIrradianceEnvironmentMap[2].Y = -Coefficient1 * SkyIrradiance.B.V[1];
		OutSkyIrradianceEnvironmentMap[2].Z = Coefficient1 * SkyIrradiance.B.V[2];
		OutSkyIrradianceEnvironmentMap[2].W = Coefficient0 * SkyIrradiance.B.V[0] - Coefficient3 * SkyIrradiance.B.V[6];

		OutSkyIrradianceEnvironmentMap[3].X = Coefficient2 * SkyIrradiance.R.V[4];
		OutSkyIrradianceEnvironmentMap[3].Y = -Coefficient2 * SkyIrradiance.R.V[5];
		OutSkyIrradianceEnvironmentMap[3].Z = 3 * Coefficient3 * SkyIrradiance.R.V[6];
		OutSkyIrradianceEnvironmentMap[3].W = -Coefficient2 * SkyIrradiance.R.V[7];

		OutSkyIrradianceEnvironmentMap[4].X = Coefficient2 * SkyIrradiance.G.V[4];
		OutSkyIrradianceEnvironmentMap[4].Y = -Coefficient2 * SkyIrradiance.G.V[5];
		OutSkyIrradianceEnvironmentMap[4].Z = 3 * Coefficient3 * SkyIrradiance.G.V[6];
		OutSkyIrradianceEnvironmentMap[4].W = -Coefficient2 * SkyIrradiance.G.V[7];

		OutSkyIrradianceEnvironmentMap[5].X = Coefficient2 * SkyIrradiance.B.V[4];
		OutSkyIrradianceEnvironmentMap[5].Y = -Coefficient2 * SkyIrradiance.B.V[5];
		OutSkyIrradianceEnvironmentMap[5].Z = 3 * Coefficient3 * SkyIrradiance.B.V[6];
		OutSkyIrradianceEnvironmentMap[5].W = -Coefficient2 * SkyIrradiance.B.V[7];

		OutSkyIrradianceEnvironmentMap[6].X = Coefficient4 * SkyIrradiance.R.V[8];
		OutSkyIrradianceEnvironmentMap[6].Y = Coefficient4 * SkyIrradiance.G.V[8];
		OutSkyIrradianceEnvironmentMap[6].Z = Coefficient4 * SkyIrradiance.B.V[8];
		OutSkyIrradianceEnvironmentMap[6].W = 1;
	}
	else
	{
		FMemory::Memzero(OutSkyIrradianceEnvironmentMap, sizeof(FVector4) * 7);
	}
}

void UpdateNoiseTextureParameters(FViewUniformShaderParameters& ViewUniformShaderParameters)
{
	if (GSystemTextures.PerlinNoiseGradient.GetReference())
	{
		ViewUniformShaderParameters.PerlinNoiseGradientTexture = (FTexture2DRHIRef&)GSystemTextures.PerlinNoiseGradient->GetRenderTargetItem().ShaderResourceTexture;
		SetBlack2DIfNull(ViewUniformShaderParameters.PerlinNoiseGradientTexture);
	}
	check(ViewUniformShaderParameters.PerlinNoiseGradientTexture);
	ViewUniformShaderParameters.PerlinNoiseGradientTextureSampler = TStaticSamplerState<SF_Point, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();

	if (GSystemTextures.PerlinNoise3D.GetReference())
	{
		ViewUniformShaderParameters.PerlinNoise3DTexture = (FTexture3DRHIRef&)GSystemTextures.PerlinNoise3D->GetRenderTargetItem().ShaderResourceTexture;
		SetBlack3DIfNull(ViewUniformShaderParameters.PerlinNoise3DTexture);
	}
	check(ViewUniformShaderParameters.PerlinNoise3DTexture);
	ViewUniformShaderParameters.PerlinNoise3DTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();

	if (GSystemTextures.SobolSampling.GetReference())
	{
		ViewUniformShaderParameters.SobolSamplingTexture = (FTexture2DRHIRef&)GSystemTextures.SobolSampling->GetRenderTargetItem().ShaderResourceTexture;
		SetBlack2DIfNull(ViewUniformShaderParameters.SobolSamplingTexture);
	}
	check(ViewUniformShaderParameters.SobolSamplingTexture);
}

/*************************************************************************************************************
 * Content copied from PrecomputedVolumetricLightmap.h for 4.25 hotfix since we can not touch public headers
 * Please delete when merged back to Dev-Rendering
 *************************************************************************************************************/
struct FVolumetricLightmapBrickTextureSet
{
	FIntVector BrickDataDimensions;

	FVolumetricLightmapDataLayer AmbientVector;
	FVolumetricLightmapDataLayer SHCoefficients[6];
	FVolumetricLightmapDataLayer SkyBentNormal;
	FVolumetricLightmapDataLayer DirectionalLightShadowing;

	template<class VolumetricLightmapBrickDataType> // Can be either FVolumetricLightmapBrickData or FVolumetricLightmapBrickTextureSet
	void Initialize(FIntVector InBrickDataDimensions, VolumetricLightmapBrickDataType& BrickData)
	{
		BrickDataDimensions = InBrickDataDimensions;

		AmbientVector.Format = BrickData.AmbientVector.Format;
		SkyBentNormal.Format = BrickData.SkyBentNormal.Format;
		DirectionalLightShadowing.Format = BrickData.DirectionalLightShadowing.Format;

		for (int32 i = 0; i < UE_ARRAY_COUNT(SHCoefficients); i++)
		{
			SHCoefficients[i].Format = BrickData.SHCoefficients[i].Format;
		}

		AmbientVector.CreateTargetTexture(BrickDataDimensions);
		AmbientVector.CreateUAV();

		for (int32 i = 0; i < UE_ARRAY_COUNT(SHCoefficients); i++)
		{
			SHCoefficients[i].CreateTargetTexture(BrickDataDimensions);
			SHCoefficients[i].CreateUAV();
		}

		if (BrickData.SkyBentNormal.Texture.IsValid())
		{
			SkyBentNormal.CreateTargetTexture(BrickDataDimensions);
			SkyBentNormal.CreateUAV();
		}

		DirectionalLightShadowing.CreateTargetTexture(BrickDataDimensions);
		DirectionalLightShadowing.CreateUAV();
	}

	void Release()
	{
		AmbientVector.Texture.SafeRelease();
		for (int32 i = 0; i < UE_ARRAY_COUNT(SHCoefficients); i++)
		{
			SHCoefficients[i].Texture.SafeRelease();
		}
		SkyBentNormal.Texture.SafeRelease();
		DirectionalLightShadowing.Texture.SafeRelease();

		AmbientVector.UAV.SafeRelease();
		for (int32 i = 0; i < UE_ARRAY_COUNT(SHCoefficients); i++)
		{
			SHCoefficients[i].UAV.SafeRelease();
		}
		SkyBentNormal.UAV.SafeRelease();
		DirectionalLightShadowing.UAV.SafeRelease();
	}
};

class ENGINE_API FVolumetricLightmapBrickAtlas : public FRenderResource
{
public:
	FVolumetricLightmapBrickAtlas();

	FVolumetricLightmapBrickTextureSet TextureSet;

	virtual void ReleaseRHI() override;

	struct Allocation
	{
		// The data being allocated, as an identifier for the entry
		class FPrecomputedVolumetricLightmapData* Data = nullptr;

		int32 Size = 0;
		int32 StartOffset = 0;
	};

	TArray<Allocation> Allocations;

	void Insert(int32 Index, FPrecomputedVolumetricLightmapData* Data);
	void Remove(FPrecomputedVolumetricLightmapData* Data);

private:
	bool bInitialized;
	int32 PaddedBrickSize;
};

extern ENGINE_API TGlobalResource<FVolumetricLightmapBrickAtlas> GVolumetricLightmapBrickAtlas;
/*************************************************************************************************************
 * Content copied from PrecomputedVolumetricLightmap.h end
 *************************************************************************************************************/

void SetupPrecomputedVolumetricLightmapUniformBufferParameters(const FScene* Scene, FEngineShowFlags EngineShowFlags, FViewUniformShaderParameters& ViewUniformShaderParameters)
{
	if (Scene && Scene->VolumetricLightmapSceneData.GetLevelVolumetricLightmap() && EngineShowFlags.VolumetricLightmap)
	{
		const FPrecomputedVolumetricLightmapData* VolumetricLightmapData = Scene->VolumetricLightmapSceneData.GetLevelVolumetricLightmap()->Data;

		ViewUniformShaderParameters.VolumetricLightmapIndirectionTexture = OrBlack3DUintIfNull(VolumetricLightmapData->IndirectionTexture.Texture);
		ViewUniformShaderParameters.VolumetricLightmapBrickAmbientVector = OrBlack3DIfNull(GVolumetricLightmapBrickAtlas.TextureSet.AmbientVector.Texture);
		ViewUniformShaderParameters.VolumetricLightmapBrickSHCoefficients0 = OrBlack3DIfNull(GVolumetricLightmapBrickAtlas.TextureSet.SHCoefficients[0].Texture);
		ViewUniformShaderParameters.VolumetricLightmapBrickSHCoefficients1 = OrBlack3DIfNull(GVolumetricLightmapBrickAtlas.TextureSet.SHCoefficients[1].Texture);
		ViewUniformShaderParameters.VolumetricLightmapBrickSHCoefficients2 = OrBlack3DIfNull(GVolumetricLightmapBrickAtlas.TextureSet.SHCoefficients[2].Texture);
		ViewUniformShaderParameters.VolumetricLightmapBrickSHCoefficients3 = OrBlack3DIfNull(GVolumetricLightmapBrickAtlas.TextureSet.SHCoefficients[3].Texture);
		ViewUniformShaderParameters.VolumetricLightmapBrickSHCoefficients4 = OrBlack3DIfNull(GVolumetricLightmapBrickAtlas.TextureSet.SHCoefficients[4].Texture);
		ViewUniformShaderParameters.VolumetricLightmapBrickSHCoefficients5 = OrBlack3DIfNull(GVolumetricLightmapBrickAtlas.TextureSet.SHCoefficients[5].Texture);
		ViewUniformShaderParameters.SkyBentNormalBrickTexture = OrBlack3DIfNull(GVolumetricLightmapBrickAtlas.TextureSet.SkyBentNormal.Texture);
		ViewUniformShaderParameters.DirectionalLightShadowingBrickTexture = OrBlack3DIfNull(GVolumetricLightmapBrickAtlas.TextureSet.DirectionalLightShadowing.Texture);

		const FBox VolumeBounds = VolumetricLightmapData->GetBounds();
		const FVector VolumeSize = VolumeBounds.GetSize();
		const FVector InvVolumeSize = VolumeSize.Reciprocal();

		const FVector BrickDimensions(GVolumetricLightmapBrickAtlas.TextureSet.BrickDataDimensions);
		const FVector InvBrickDimensions = BrickDimensions.Reciprocal();

		ViewUniformShaderParameters.VolumetricLightmapWorldToUVScale = InvVolumeSize;
		ViewUniformShaderParameters.VolumetricLightmapWorldToUVAdd = -VolumeBounds.Min * InvVolumeSize;
		ViewUniformShaderParameters.VolumetricLightmapIndirectionTextureSize = FVector(VolumetricLightmapData->IndirectionTextureDimensions);
		ViewUniformShaderParameters.VolumetricLightmapBrickSize = VolumetricLightmapData->BrickSize;
		ViewUniformShaderParameters.VolumetricLightmapBrickTexelSize = InvBrickDimensions;
	}
	else
	{
		// Resources are initialized in FViewUniformShaderParameters ctor, only need to set defaults for non-resource types

		ViewUniformShaderParameters.VolumetricLightmapWorldToUVScale = FVector::ZeroVector;
		ViewUniformShaderParameters.VolumetricLightmapWorldToUVAdd = FVector::ZeroVector;
		ViewUniformShaderParameters.VolumetricLightmapIndirectionTextureSize = FVector::ZeroVector;
		ViewUniformShaderParameters.VolumetricLightmapBrickSize = 0;
		ViewUniformShaderParameters.VolumetricLightmapBrickTexelSize = FVector::ZeroVector;
	}
}

FIntPoint FViewInfo::GetSecondaryViewRectSize() const
{
	return FIntPoint(
		FMath::CeilToInt(UnscaledViewRect.Width() * Family->SecondaryViewFraction),
		FMath::CeilToInt(UnscaledViewRect.Height() * Family->SecondaryViewFraction));
}

/** Creates the view's uniform buffers given a set of view transforms. */
void FViewInfo::SetupUniformBufferParameters(
	FSceneRenderTargets& SceneContext,
	const FViewMatrices& InViewMatrices,
	const FViewMatrices& InPrevViewMatrices,
	FBox* OutTranslucentCascadeBoundsArray,
	int32 NumTranslucentCascades,
	FViewUniformShaderParameters& ViewUniformShaderParameters) const
{
	check(Family);

	// Create the view's uniform buffer.

	// Mobile multi-view is not side by side
	const FIntRect EffectiveViewRect = (bIsMobileMultiViewEnabled) ? FIntRect(0, 0, ViewRect.Width(), ViewRect.Height()) : ViewRect;

	// Scene render targets may not be created yet; avoids NaNs.
	FIntPoint EffectiveBufferSize = SceneContext.GetBufferSizeXY();
	EffectiveBufferSize.X = FMath::Max(EffectiveBufferSize.X, 1);
	EffectiveBufferSize.Y = FMath::Max(EffectiveBufferSize.Y, 1);

	// TODO: We should use a view and previous view uniform buffer to avoid code duplication and keep consistency
	SetupCommonViewUniformBufferParameters(
		ViewUniformShaderParameters,
		EffectiveBufferSize,
		SceneContext.GetMSAACount(),
		EffectiveViewRect,
		InViewMatrices,
		InPrevViewMatrices
	);

	const bool bCheckerboardSubsurfaceRendering = IsSubsurfaceCheckerboardFormat(SceneContext.GetSceneColorFormat());
	ViewUniformShaderParameters.bCheckerboardSubsurfaceProfileRendering = bCheckerboardSubsurfaceRendering ? 1.0f : 0.0f;

	ViewUniformShaderParameters.IndirectLightingCacheShowFlag = Family->EngineShowFlags.IndirectLightingCache;

	FScene* Scene = nullptr;

	if (Family->Scene)
	{
		Scene = Family->Scene->GetRenderScene();
	}

	const FVector DefaultSunDirection(0.0f, 0.0f, 1.0f); // Up vector so that the AtmosphericLightVector node always output a valid direction.
	auto ClearAtmosphereLightData = [&](uint32 Index)
	{
		check(Index < NUM_ATMOSPHERE_LIGHTS);
		ViewUniformShaderParameters.AtmosphereLightDiscCosHalfApexAngle[Index] = FVector4(1.0f);
		ViewUniformShaderParameters.AtmosphereLightDiscLuminance[Index] = FLinearColor::Black;
		ViewUniformShaderParameters.AtmosphereLightColor[Index] = FLinearColor::Black;
		ViewUniformShaderParameters.AtmosphereLightColor[Index].A = 0.0f;
		ViewUniformShaderParameters.AtmosphereLightColorGlobalPostTransmittance[Index] = FLinearColor::Black;
		ViewUniformShaderParameters.AtmosphereLightColorGlobalPostTransmittance[Index].A = 0.0f;
		ViewUniformShaderParameters.AtmosphereLightDirection[Index] = DefaultSunDirection;
	};

	uint32 AtmosphereLightDataClearStartIndex = 0;
	if (Scene)
	{
		if (Scene->SimpleDirectionalLight)
		{
			
			ViewUniformShaderParameters.DirectionalLightColor = Scene->SimpleDirectionalLight->Proxy->GetTransmittanceFactor() * Scene->SimpleDirectionalLight->Proxy->GetColor() / PI;
			ViewUniformShaderParameters.DirectionalLightDirection = -Scene->SimpleDirectionalLight->Proxy->GetDirection();
		}
		else
		{
			ViewUniformShaderParameters.DirectionalLightColor = FLinearColor::Black;
			ViewUniformShaderParameters.DirectionalLightDirection = FVector::ZeroVector;
		}

		// Atmospheric fog parameters
		FLightSceneInfo* SunLight = Scene->AtmosphereLights[0];	// Atmospheric fog only takes into account the a single sun light with index 0.
		const float SunLightDiskHalfApexAngleRadian = SunLight ? SunLight->Proxy->GetSunLightHalfApexAngleRadian() : FLightSceneProxy::GetSunOnEarthHalfApexAngleRadian();
		if (ShouldRenderAtmosphere(*Family) && Scene->AtmosphericFog)
		{
			ViewUniformShaderParameters.AtmosphericFogSunPower = Scene->AtmosphericFog->SunMultiplier;
			ViewUniformShaderParameters.AtmosphericFogPower = Scene->AtmosphericFog->FogMultiplier;
			ViewUniformShaderParameters.AtmosphericFogDensityScale = Scene->AtmosphericFog->InvDensityMultiplier;
			ViewUniformShaderParameters.AtmosphericFogDensityOffset = Scene->AtmosphericFog->DensityOffset;
			ViewUniformShaderParameters.AtmosphericFogGroundOffset = Scene->AtmosphericFog->GroundOffset;
			ViewUniformShaderParameters.AtmosphericFogDistanceScale = Scene->AtmosphericFog->DistanceScale;
			ViewUniformShaderParameters.AtmosphericFogAltitudeScale = Scene->AtmosphericFog->AltitudeScale;
			ViewUniformShaderParameters.AtmosphericFogHeightScaleRayleigh = Scene->AtmosphericFog->RHeight;
			ViewUniformShaderParameters.AtmosphericFogStartDistance = Scene->AtmosphericFog->StartDistance;
			ViewUniformShaderParameters.AtmosphericFogDistanceOffset = Scene->AtmosphericFog->DistanceOffset;
			ViewUniformShaderParameters.AtmosphericFogSunDiscScale = Scene->AtmosphericFog->SunDiscScale;
			ViewUniformShaderParameters.AtmosphericFogRenderMask = Scene->AtmosphericFog->RenderFlag & (EAtmosphereRenderFlag::E_DisableGroundScattering | EAtmosphereRenderFlag::E_DisableSunDisk);
			ViewUniformShaderParameters.AtmosphericFogInscatterAltitudeSampleNum = Scene->AtmosphericFog->InscatterAltitudeSampleNum;
			ViewUniformShaderParameters.AtmosphereLightDiscCosHalfApexAngle[0] = FVector4(FMath::Cos(Scene->AtmosphericFog->SunDiscScale * SunLightDiskHalfApexAngleRadian));
			ViewUniformShaderParameters.AtmosphereLightDiscLuminance[0] = SunLight ? SunLight->Proxy->GetOuterSpaceLuminance() : FLinearColor::White;
			ViewUniformShaderParameters.AtmosphereLightColor[0] = SunLight ? SunLight->Proxy->GetColor() : Scene->AtmosphericFog->DefaultSunColor; // Sun light color unaffected by atmosphere transmittance
			ViewUniformShaderParameters.AtmosphereLightColor[0].A = 1.0f;
			ViewUniformShaderParameters.AtmosphereLightColorGlobalPostTransmittance[0] = FLinearColor::Black;
			ViewUniformShaderParameters.AtmosphereLightColorGlobalPostTransmittance[0].A = 0.0f;
			ViewUniformShaderParameters.AtmosphereLightDirection[0] = SunLight ? -SunLight->Proxy->GetDirection() : -Scene->AtmosphericFog->DefaultSunDirection;
		}
		else
		{
			ViewUniformShaderParameters.AtmosphericFogSunPower = 0.f;
			ViewUniformShaderParameters.AtmosphericFogPower = 0.f;
			ViewUniformShaderParameters.AtmosphericFogDensityScale = 0.f;
			ViewUniformShaderParameters.AtmosphericFogDensityOffset = 0.f;
			ViewUniformShaderParameters.AtmosphericFogGroundOffset = 0.f;
			ViewUniformShaderParameters.AtmosphericFogDistanceScale = 0.f;
			ViewUniformShaderParameters.AtmosphericFogAltitudeScale = 0.f;
			ViewUniformShaderParameters.AtmosphericFogHeightScaleRayleigh = 0.f;
			ViewUniformShaderParameters.AtmosphericFogStartDistance = FLT_MAX;
			ViewUniformShaderParameters.AtmosphericFogDistanceOffset = 0.f;
			ViewUniformShaderParameters.AtmosphericFogSunDiscScale = 1.f;
			ViewUniformShaderParameters.AtmosphericFogRenderMask = EAtmosphereRenderFlag::E_EnableAll;
			ViewUniformShaderParameters.AtmosphericFogInscatterAltitudeSampleNum = 0;
			ViewUniformShaderParameters.AtmosphereLightDiscCosHalfApexAngle[0] = FVector4(FMath::Cos(SunLightDiskHalfApexAngleRadian));
			//Added check so atmospheric light color and vector can use a directional light without needing an atmospheric fog actor in the scene
			ViewUniformShaderParameters.AtmosphereLightDiscLuminance[0] = SunLight ? SunLight->Proxy->GetOuterSpaceLuminance() : FLinearColor::Black;
			ViewUniformShaderParameters.AtmosphereLightColor[0] = SunLight ? SunLight->Proxy->GetColor() : FLinearColor::Black;
			ViewUniformShaderParameters.AtmosphereLightColor[0].A = 1.0f;
			ViewUniformShaderParameters.AtmosphereLightColorGlobalPostTransmittance[0] = FLinearColor::Black;
			ViewUniformShaderParameters.AtmosphereLightColorGlobalPostTransmittance[0].A = 0.0f;
			ViewUniformShaderParameters.AtmosphereLightDirection[0] = SunLight ? -SunLight->Proxy->GetDirection() : DefaultSunDirection;
		}
		AtmosphereLightDataClearStartIndex = 1; // Do not clear the first atmosphere light data
	}
	else
	{
		// Atmospheric fog parameters
		ViewUniformShaderParameters.AtmosphericFogSunPower = 0.f;
		ViewUniformShaderParameters.AtmosphericFogPower = 0.f;
		ViewUniformShaderParameters.AtmosphericFogDensityScale = 0.f;
		ViewUniformShaderParameters.AtmosphericFogDensityOffset = 0.f;
		ViewUniformShaderParameters.AtmosphericFogGroundOffset = 0.f;
		ViewUniformShaderParameters.AtmosphericFogDistanceScale = 0.f;
		ViewUniformShaderParameters.AtmosphericFogAltitudeScale = 0.f;
		ViewUniformShaderParameters.AtmosphericFogHeightScaleRayleigh = 0.f;
		ViewUniformShaderParameters.AtmosphericFogStartDistance = FLT_MAX;
		ViewUniformShaderParameters.AtmosphericFogDistanceOffset = 0.f;
		ViewUniformShaderParameters.AtmosphericFogSunDiscScale = 1.f;
		ViewUniformShaderParameters.AtmosphericFogRenderMask = EAtmosphereRenderFlag::E_EnableAll;
		ViewUniformShaderParameters.AtmosphericFogInscatterAltitudeSampleNum = 0;
		AtmosphereLightDataClearStartIndex = 0; // Clear every atmosphere light data
	}

	FRHITexture* TransmittanceLutTextureFound = nullptr;
	FRHITexture* SkyViewLutTextureFound = nullptr;
	FRHITexture* CameraAerialPerspectiveVolumeFound = nullptr;
	FRHITexture* DistantSkyLightLutTextureFound = nullptr;
	if (ShouldRenderSkyAtmosphere(Scene, Family->EngineShowFlags))
	{
		FSkyAtmosphereRenderSceneInfo* SkyAtmosphere = Scene->SkyAtmosphere;
		const FSkyAtmosphereSceneProxy& SkyAtmosphereSceneProxy = SkyAtmosphere->GetSkyAtmosphereSceneProxy();

		// Get access to texture resource if we have valid pointer.
		// (Valid pointer checks are needed because some resources might not have been initialized when coming from FCanvasTileRendererItem or FCanvasTriangleRendererItem)

		const TRefCountPtr<IPooledRenderTarget>& PooledTransmittanceLutTexture = SkyAtmosphere->GetTransmittanceLutTexture();
		if (PooledTransmittanceLutTexture.IsValid())
		{
			TransmittanceLutTextureFound = PooledTransmittanceLutTexture->GetRenderTargetItem().ShaderResourceTexture;

			//@StarLight code - BEGIN Precomputed Multi Scattering on mobile, edit by wanghai
			static const auto CVarsEnablePrecomputedScattering = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.SkyAtmosphere.EnablePrecomputedMultiScattering"));
			if (CVarsEnablePrecomputedScattering->GetValueOnRenderThread() > 0 && SkyAtmosphere->GetSkyAtmosphereSceneProxy().GetAtmosphereSetup().bUsePrecomputedAtmpsphereLuts)
			{
				TransmittanceLutTextureFound = SkyAtmosphere->GetPrecomputedTranmisttanceLut().GetReference();
			}
			//@StarLight code - END Precomputed Multi Scattering on mobile, edit by wanghai
			
		}
		const TRefCountPtr<IPooledRenderTarget>& PooledDistantSkyLightLutTexture = SkyAtmosphere->GetDistantSkyLightLutTexture();
		if (PooledDistantSkyLightLutTexture.IsValid())
		{
			DistantSkyLightLutTextureFound = PooledDistantSkyLightLutTexture->GetRenderTargetItem().ShaderResourceTexture;
		}

		if (this->SkyAtmosphereCameraAerialPerspectiveVolume.IsValid())
		{
			CameraAerialPerspectiveVolumeFound = this->SkyAtmosphereCameraAerialPerspectiveVolume->GetRenderTargetItem().ShaderResourceTexture;
		}

		float SkyViewLutWidth = 1.0f;
		float SkyViewLutHeight = 1.0f;
		if (this->SkyAtmosphereViewLutTexture.IsValid())
		{
			SkyViewLutTextureFound = this->SkyAtmosphereViewLutTexture->GetRenderTargetItem().ShaderResourceTexture;
			SkyViewLutWidth = float(this->SkyAtmosphereViewLutTexture->GetDesc().GetSize().X);
			SkyViewLutHeight = float(this->SkyAtmosphereViewLutTexture->GetDesc().GetSize().Y);
		}
		ViewUniformShaderParameters.SkyViewLutSizeAndInvSize = FVector4(SkyViewLutWidth, SkyViewLutHeight, 1.0f / SkyViewLutWidth, 1.0f / SkyViewLutHeight);

		// Now initialize remaining view parameters.

		const FAtmosphereSetup& AtmosphereSetup = SkyAtmosphereSceneProxy.GetAtmosphereSetup();
		ViewUniformShaderParameters.SkyAtmosphereBottomRadiusKm = AtmosphereSetup.BottomRadiusKm;
		ViewUniformShaderParameters.SkyAtmosphereTopRadiusKm = AtmosphereSetup.TopRadiusKm;

		FSkyAtmosphereViewSharedUniformShaderParameters OutParameters;
		SetupSkyAtmosphereViewSharedUniformShaderParameters(*this, OutParameters);
		ViewUniformShaderParameters.SkyAtmosphereAerialPerspectiveStartDepthKm = OutParameters.AerialPerspectiveStartDepthKm;
		ViewUniformShaderParameters.SkyAtmosphereCameraAerialPerspectiveVolumeDepthResolution = OutParameters.CameraAerialPerspectiveVolumeDepthResolution;
		ViewUniformShaderParameters.SkyAtmosphereCameraAerialPerspectiveVolumeDepthResolutionInv = OutParameters.CameraAerialPerspectiveVolumeDepthResolutionInv;
		ViewUniformShaderParameters.SkyAtmosphereCameraAerialPerspectiveVolumeDepthSliceLengthKm = OutParameters.CameraAerialPerspectiveVolumeDepthSliceLengthKm;
		ViewUniformShaderParameters.SkyAtmosphereCameraAerialPerspectiveVolumeDepthSliceLengthKmInv = OutParameters.CameraAerialPerspectiveVolumeDepthSliceLengthKmInv;
		ViewUniformShaderParameters.SkyAtmosphereApplyCameraAerialPerspectiveVolume = OutParameters.ApplyCameraAerialPerspectiveVolume;
		ViewUniformShaderParameters.SkyAtmosphereSkyLuminanceFactor = SkyAtmosphereSceneProxy.GetSkyLuminanceFactor();
		ViewUniformShaderParameters.SkyAtmosphereHeightFogContribution = SkyAtmosphereSceneProxy.GetHeightFogContribution();

		// Fill atmosphere lights shader parameters
		for (uint8 Index = 0; Index < NUM_ATMOSPHERE_LIGHTS; ++Index)
		{
			FLightSceneInfo* Light = Scene->AtmosphereLights[Index];
			if (Light)
			{
				ViewUniformShaderParameters.AtmosphereLightDiscCosHalfApexAngle[Index] = FVector4(FMath::Cos(Light->Proxy->GetSunLightHalfApexAngleRadian()));
				ViewUniformShaderParameters.AtmosphereLightDiscLuminance[Index] = Light->Proxy->GetOuterSpaceLuminance();
				ViewUniformShaderParameters.AtmosphereLightColor[Index] = Light->Proxy->GetColor();
				ViewUniformShaderParameters.AtmosphereLightColor[Index].A = 1.0f;
				ViewUniformShaderParameters.AtmosphereLightColorGlobalPostTransmittance[Index] = Light->Proxy->GetColor() * Light->Proxy->GetTransmittanceFactor();
				ViewUniformShaderParameters.AtmosphereLightColorGlobalPostTransmittance[Index].A = 1.0f;
				ViewUniformShaderParameters.AtmosphereLightDirection[Index] = SkyAtmosphereSceneProxy.GetAtmosphereLightDirection(Index, -Light->Proxy->GetDirection());
			}
			else
			{
				ClearAtmosphereLightData(Index);
			}
		}
		AtmosphereLightDataClearStartIndex = NUM_ATMOSPHERE_LIGHTS;	// Do not clear any atmosphere light data, this component sets everything it needs

		// The constants below should match the one in SkyAtmosphereCommon.ush
		const float PlanetRadiusOffset = 0.01f;		// Always force to be 10 meters above the ground/sea level (to always see the sky and not be under the virtual planet occluding ray tracing)

		const float Offset = PlanetRadiusOffset * FAtmosphereSetup::SkyUnitToCm;
		const float BottomRadiusWorld = AtmosphereSetup.BottomRadiusKm * FAtmosphereSetup::SkyUnitToCm;
		const FVector PlanetCenterWorld = AtmosphereSetup.PlanetCenterKm * FAtmosphereSetup::SkyUnitToCm;
		const FVector PlanetCenterToCameraWorld = ViewUniformShaderParameters.WorldCameraOrigin - PlanetCenterWorld;
		const float DistanceToPlanetCenterWorld = PlanetCenterToCameraWorld.Size();

		// If the camera is below the planet surface, we snap it back onto the surface.
		// This is to make sure the sky is always visible even if the camera is inside the virtual planet.
		ViewUniformShaderParameters.SkyWorldCameraOrigin = DistanceToPlanetCenterWorld < (BottomRadiusWorld + Offset) ? PlanetCenterWorld + (BottomRadiusWorld + Offset) * (PlanetCenterToCameraWorld / DistanceToPlanetCenterWorld) : ViewUniformShaderParameters.WorldCameraOrigin;
		ViewUniformShaderParameters.SkyPlanetCenterAndViewHeight = FVector4(PlanetCenterWorld, (ViewUniformShaderParameters.SkyWorldCameraOrigin - PlanetCenterWorld).Size());
	}
	else
	{
		ViewUniformShaderParameters.SkyAtmosphereHeightFogContribution = 0.0f;
		ViewUniformShaderParameters.SkyViewLutSizeAndInvSize = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
		ViewUniformShaderParameters.SkyAtmosphereBottomRadiusKm = 1.0f;
		ViewUniformShaderParameters.SkyAtmosphereTopRadiusKm = 1.0f;
		ViewUniformShaderParameters.SkyAtmosphereSkyLuminanceFactor = FLinearColor::White;
		ViewUniformShaderParameters.SkyAtmosphereAerialPerspectiveStartDepthKm = 1.0f;
		ViewUniformShaderParameters.SkyAtmosphereCameraAerialPerspectiveVolumeDepthResolution = 1.0f;
		ViewUniformShaderParameters.SkyAtmosphereCameraAerialPerspectiveVolumeDepthResolutionInv = 1.0f;
		ViewUniformShaderParameters.SkyAtmosphereCameraAerialPerspectiveVolumeDepthSliceLengthKm = 1.0f;
		ViewUniformShaderParameters.SkyAtmosphereCameraAerialPerspectiveVolumeDepthSliceLengthKmInv = 1.0f;
		ViewUniformShaderParameters.SkyAtmosphereApplyCameraAerialPerspectiveVolume = 1.0f;
		ViewUniformShaderParameters.SkyWorldCameraOrigin = ViewUniformShaderParameters.WorldCameraOrigin;
		ViewUniformShaderParameters.SkyPlanetCenterAndViewHeight = FVector4(ForceInitToZero);
	}

	for (uint8 Index = AtmosphereLightDataClearStartIndex; Index < NUM_ATMOSPHERE_LIGHTS; ++Index)
	{
		ClearAtmosphereLightData(Index);
	}

	ViewUniformShaderParameters.TransmittanceLutTexture = OrWhite2DIfNull(TransmittanceLutTextureFound);
	ViewUniformShaderParameters.TransmittanceLutTextureSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
	ViewUniformShaderParameters.DistantSkyLightLutTexture = OrBlack2DIfNull(DistantSkyLightLutTextureFound);
	ViewUniformShaderParameters.DistantSkyLightLutTextureSampler = TStaticSamplerState<SF_Point, AM_Wrap, AM_Wrap>::GetRHI();
	ViewUniformShaderParameters.SkyViewLutTexture = OrBlack2DIfNull(SkyViewLutTextureFound);
	ViewUniformShaderParameters.SkyViewLutTextureSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
	ViewUniformShaderParameters.CameraAerialPerspectiveVolume = OrBlack3DAlpha1IfNull(CameraAerialPerspectiveVolumeFound);
	ViewUniformShaderParameters.CameraAerialPerspectiveVolumeSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();

	ViewUniformShaderParameters.AtmosphereTransmittanceTexture = OrBlack2DIfNull(AtmosphereTransmittanceTexture);
	ViewUniformShaderParameters.AtmosphereIrradianceTexture = OrBlack2DIfNull(AtmosphereIrradianceTexture);
	ViewUniformShaderParameters.AtmosphereInscatterTexture = OrBlack3DIfNull(AtmosphereInscatterTexture);

	ViewUniformShaderParameters.AtmosphereTransmittanceTextureSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
	ViewUniformShaderParameters.AtmosphereIrradianceTextureSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
	ViewUniformShaderParameters.AtmosphereInscatterTextureSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();

	// This should probably be in SetupCommonViewUniformBufferParameters, but drags in too many dependencies
	UpdateNoiseTextureParameters(ViewUniformShaderParameters);

	SetupDefaultGlobalDistanceFieldUniformBufferParameters(ViewUniformShaderParameters);

	SetupVolumetricFogUniformBufferParameters(ViewUniformShaderParameters);

	SetupPrecomputedVolumetricLightmapUniformBufferParameters(Scene, Family->EngineShowFlags, ViewUniformShaderParameters);

	// Setup view's shared sampler for material texture sampling.
	{
		const float GlobalMipBias = UTexture2D::GetGlobalMipMapLODBias();

		float FinalMaterialTextureMipBias = GlobalMipBias;

		if (bIsValidWorldTextureGroupSamplerFilter && !FMath::IsNearlyZero(MaterialTextureMipBias))
		{
			ViewUniformShaderParameters.MaterialTextureMipBias = MaterialTextureMipBias;
			ViewUniformShaderParameters.MaterialTextureDerivativeMultiply = FMath::Pow(2.0f, MaterialTextureMipBias);

			FinalMaterialTextureMipBias += MaterialTextureMipBias;
		}

		FSamplerStateRHIRef WrappedSampler = nullptr;
		FSamplerStateRHIRef ClampedSampler = nullptr;

		if (FMath::Abs(FinalMaterialTextureMipBias - GlobalMipBias) < KINDA_SMALL_NUMBER)
		{
			WrappedSampler = Wrap_WorldGroupSettings->SamplerStateRHI;
			ClampedSampler = Clamp_WorldGroupSettings->SamplerStateRHI;
		}
		else if (ViewState && FMath::Abs(ViewState->MaterialTextureCachedMipBias - FinalMaterialTextureMipBias) < KINDA_SMALL_NUMBER)
		{
			WrappedSampler = ViewState->MaterialTextureBilinearWrapedSamplerCache;
			ClampedSampler = ViewState->MaterialTextureBilinearClampedSamplerCache;
		}
		else
		{
			check(bIsValidWorldTextureGroupSamplerFilter);

			WrappedSampler = RHICreateSamplerState(FSamplerStateInitializerRHI(WorldTextureGroupSamplerFilter, AM_Wrap,  AM_Wrap,  AM_Wrap,  FinalMaterialTextureMipBias));
			ClampedSampler = RHICreateSamplerState(FSamplerStateInitializerRHI(WorldTextureGroupSamplerFilter, AM_Clamp, AM_Clamp, AM_Clamp, FinalMaterialTextureMipBias));
		}

		// At this point, a sampler must be set.
		check(WrappedSampler.IsValid());
		check(ClampedSampler.IsValid());

		ViewUniformShaderParameters.MaterialTextureBilinearWrapedSampler = WrappedSampler;
		ViewUniformShaderParameters.MaterialTextureBilinearClampedSampler = ClampedSampler;

		// Update view state's cached sampler.
		if (ViewState && ViewState->MaterialTextureBilinearWrapedSamplerCache != WrappedSampler)
		{
			ViewState->MaterialTextureCachedMipBias = FinalMaterialTextureMipBias;
			ViewState->MaterialTextureBilinearWrapedSamplerCache = WrappedSampler;
			ViewState->MaterialTextureBilinearClampedSamplerCache = ClampedSampler;
		}
	}

	{
		ensureMsgf(TemporalJitterSequenceLength == 1 || AntiAliasingMethod == AAM_TemporalAA,
			TEXT("TemporalJitterSequenceLength = %i is invalid"), TemporalJitterSequenceLength);
		ensureMsgf(TemporalJitterIndex >= 0 && TemporalJitterIndex < TemporalJitterSequenceLength,
			TEXT("TemporalJitterIndex = %i is invalid (TemporalJitterSequenceLength = %i)"), TemporalJitterIndex, TemporalJitterSequenceLength);
		ViewUniformShaderParameters.TemporalAAParams = FVector4(
			TemporalJitterIndex, 
			TemporalJitterSequenceLength,
			TemporalJitterPixels.X,
			TemporalJitterPixels.Y);
	}
		
	uint32 FrameIndex = 0;
	if (ViewState)
	{
		FrameIndex = ViewState->GetFrameIndex();
	}

	// TODO(GA): kill StateFrameIndexMod8 because this is only a scalar bit mask with StateFrameIndex anyway.
	ViewUniformShaderParameters.StateFrameIndexMod8 = FrameIndex % 8;
	ViewUniformShaderParameters.StateFrameIndex = FrameIndex;

	{
		// If rendering in stereo, the other stereo passes uses the left eye's translucency lighting volume.
		const FViewInfo* PrimaryView = this;
		if (IStereoRendering::IsASecondaryView(*this))
		{
			if (Family->Views.IsValidIndex(0))
			{
				const FSceneView* LeftEyeView = Family->Views[0];
				if (LeftEyeView->bIsViewInfo && IStereoRendering::IsAPrimaryView(*LeftEyeView))
				{
					PrimaryView = static_cast<const FViewInfo*>(LeftEyeView);
				}
			}
		}
		PrimaryView->CalcTranslucencyLightingVolumeBounds(OutTranslucentCascadeBoundsArray, NumTranslucentCascades);
	}

	const int32 TranslucencyLightingVolumeDim = GetTranslucencyLightingVolumeDim();

	for (int32 CascadeIndex = 0; CascadeIndex < NumTranslucentCascades; CascadeIndex++)
	{
		const float VolumeVoxelSize = (OutTranslucentCascadeBoundsArray[CascadeIndex].Max.X - OutTranslucentCascadeBoundsArray[CascadeIndex].Min.X) / TranslucencyLightingVolumeDim;
		const FVector VolumeSize = OutTranslucentCascadeBoundsArray[CascadeIndex].Max - OutTranslucentCascadeBoundsArray[CascadeIndex].Min;
		ViewUniformShaderParameters.TranslucencyLightingVolumeMin[CascadeIndex] = FVector4(OutTranslucentCascadeBoundsArray[CascadeIndex].Min, 1.0f / TranslucencyLightingVolumeDim);
		ViewUniformShaderParameters.TranslucencyLightingVolumeInvSize[CascadeIndex] = FVector4(FVector(1.0f) / VolumeSize, VolumeVoxelSize);
	}
	
	ViewUniformShaderParameters.PreExposure = PreExposure;
	ViewUniformShaderParameters.OneOverPreExposure = 1.f / PreExposure;

	ViewUniformShaderParameters.DepthOfFieldFocalDistance = FinalPostProcessSettings.DepthOfFieldFocalDistance;
	ViewUniformShaderParameters.DepthOfFieldSensorWidth = FinalPostProcessSettings.DepthOfFieldSensorWidth;
	ViewUniformShaderParameters.DepthOfFieldFocalRegion = FinalPostProcessSettings.DepthOfFieldFocalRegion;
	// clamped to avoid div by 0 in shader
	ViewUniformShaderParameters.DepthOfFieldNearTransitionRegion = FMath::Max(0.01f, FinalPostProcessSettings.DepthOfFieldNearTransitionRegion);
	// clamped to avoid div by 0 in shader
	ViewUniformShaderParameters.DepthOfFieldFarTransitionRegion = FMath::Max(0.01f, FinalPostProcessSettings.DepthOfFieldFarTransitionRegion);
	ViewUniformShaderParameters.DepthOfFieldScale = FinalPostProcessSettings.DepthOfFieldScale;
	ViewUniformShaderParameters.DepthOfFieldFocalLength = 50.0f;

	ViewUniformShaderParameters.bSubsurfacePostprocessEnabled = IsSubsurfaceEnabled() ? 1.0f : 0.0f;

	{
		// This is the CVar default
		float Value = 1.0f;

		// Compiled out in SHIPPING to make cheating a bit harder.
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		Value = CVarGeneralPurposeTweak.GetValueOnRenderThread();
#endif

		ViewUniformShaderParameters.GeneralPurposeTweak = Value;
	}

	ViewUniformShaderParameters.DemosaicVposOffset = 0.0f;
	{
		ViewUniformShaderParameters.DemosaicVposOffset = CVarDemosaicVposOffset.GetValueOnRenderThread();
	}

	ViewUniformShaderParameters.IndirectLightingColorScale = FVector(FinalPostProcessSettings.IndirectLightingColor.R * FinalPostProcessSettings.IndirectLightingIntensity,
		FinalPostProcessSettings.IndirectLightingColor.G * FinalPostProcessSettings.IndirectLightingIntensity,
		FinalPostProcessSettings.IndirectLightingColor.B * FinalPostProcessSettings.IndirectLightingIntensity);

	ViewUniformShaderParameters.NormalCurvatureToRoughnessScaleBias.X = FMath::Clamp(CVarNormalCurvatureToRoughnessScale.GetValueOnAnyThread(), 0.0f, 2.0f);
	ViewUniformShaderParameters.NormalCurvatureToRoughnessScaleBias.Y = FMath::Clamp(CVarNormalCurvatureToRoughnessBias.GetValueOnAnyThread(), -1.0f, 1.0f);
	ViewUniformShaderParameters.NormalCurvatureToRoughnessScaleBias.Z = FMath::Clamp(CVarNormalCurvatureToRoughnessExponent.GetValueOnAnyThread(), .05f, 20.0f);

	ViewUniformShaderParameters.RenderingReflectionCaptureMask = bIsReflectionCapture ? 1.0f : 0.0f;

	ViewUniformShaderParameters.AmbientCubemapTint = FinalPostProcessSettings.AmbientCubemapTint;
	ViewUniformShaderParameters.AmbientCubemapIntensity = FinalPostProcessSettings.AmbientCubemapIntensity;

	ViewUniformShaderParameters.CircleDOFParams = DiaphragmDOF::CircleDofHalfCoc(*this);

	ERHIFeatureLevel::Type RHIFeatureLevel = Scene == nullptr ? GMaxRHIFeatureLevel : Scene->GetFeatureLevel();

	if (Scene && Scene->SkyLight)
	{
		FSkyLightSceneProxy* SkyLight = Scene->SkyLight;

		ViewUniformShaderParameters.SkyLightColor = SkyLight->GetEffectiveLightColor();

		bool bApplyPrecomputedBentNormalShadowing = 
			SkyLight->bCastShadows 
			&& SkyLight->bWantsStaticShadowing;

		ViewUniformShaderParameters.SkyLightApplyPrecomputedBentNormalShadowingFlag = bApplyPrecomputedBentNormalShadowing ? 1.0f : 0.0f;
		ViewUniformShaderParameters.SkyLightAffectReflectionFlag = SkyLight->bAffectReflection ? 1.0f : 0.0f;
		ViewUniformShaderParameters.SkyLightAffectGlobalIlluminationFlag = SkyLight->bAffectGlobalIllumination ? 1.0f : 0.0f;
	}
	else
	{
		ViewUniformShaderParameters.SkyLightColor = FLinearColor::Black;
		ViewUniformShaderParameters.SkyLightApplyPrecomputedBentNormalShadowingFlag = 0.0f;
		ViewUniformShaderParameters.SkyLightAffectReflectionFlag = 0.0f;
		ViewUniformShaderParameters.SkyLightAffectGlobalIlluminationFlag = 0.0f;
	}

	// Make sure there's no padding since we're going to cast to FVector4*
	checkSlow(sizeof(ViewUniformShaderParameters.SkyIrradianceEnvironmentMap) == sizeof(FVector4)* 7);
	SetupSkyIrradianceEnvironmentMapConstants((FVector4*)&ViewUniformShaderParameters.SkyIrradianceEnvironmentMap);
	
	ViewUniformShaderParameters.MobilePreviewMode =
		(GIsEditor &&
		(RHIFeatureLevel == ERHIFeatureLevel::ES3_1) &&
		GMaxRHIFeatureLevel > ERHIFeatureLevel::ES3_1) ? 1.0f : 0.0f;

	// Padding between the left and right eye may be introduced by an HMD, which instanced stereo needs to account for.
	if ((IStereoRendering::IsStereoEyePass(StereoPass)) && (Family->Views.Num() > 1))
	{
		check(Family->Views.Num() >= 2);

		// The static_cast<const FViewInfo*> is fine because when executing this method, we know that
		// Family::Views point to multiple FViewInfo, since of them is <this>.
		const float StereoViewportWidth = float(
			static_cast<const FViewInfo*>(Family->Views[1])->ViewRect.Max.X - 
			static_cast<const FViewInfo*>(Family->Views[0])->ViewRect.Min.X);
		const float EyePaddingSize = float(
			static_cast<const FViewInfo*>(Family->Views[1])->ViewRect.Min.X -
			static_cast<const FViewInfo*>(Family->Views[0])->ViewRect.Max.X);

		ViewUniformShaderParameters.HMDEyePaddingOffset = (StereoViewportWidth - EyePaddingSize) / StereoViewportWidth;
	}
	else
	{
		ViewUniformShaderParameters.HMDEyePaddingOffset = 1.0f;
	}

	ViewUniformShaderParameters.ReflectionCubemapMaxMip = FMath::FloorLog2(UReflectionCaptureComponent::GetReflectionCaptureSize());

	ViewUniformShaderParameters.ShowDecalsMask = Family->EngineShowFlags.Decals ? 1.0f : 0.0f;

	extern int32 GDistanceFieldAOSpecularOcclusionMode;
	ViewUniformShaderParameters.DistanceFieldAOSpecularOcclusionMode = GDistanceFieldAOSpecularOcclusionMode;

	ViewUniformShaderParameters.IndirectCapsuleSelfShadowingIntensity = Scene ? Scene->DynamicIndirectShadowsSelfShadowingIntensity : 1.0f;

	extern FVector GetReflectionEnvironmentRoughnessMixingScaleBiasAndLargestWeight();
	ViewUniformShaderParameters.ReflectionEnvironmentRoughnessMixingScaleBiasAndLargestWeight = GetReflectionEnvironmentRoughnessMixingScaleBiasAndLargestWeight();

	ViewUniformShaderParameters.StereoPassIndex = GEngine->StereoRenderingDevice ? GEngine->StereoRenderingDevice->GetViewIndexForPass(StereoPass) : 0;
	ViewUniformShaderParameters.StereoIPD = StereoIPD;

	{
		auto XRCamera = GEngine->XRSystem ? GEngine->XRSystem->GetXRCamera() : nullptr;
		TArray<FVector2D> CameraUVs;
		if (XRCamera.IsValid() && XRCamera->GetPassthroughCameraUVs_RenderThread(CameraUVs) && CameraUVs.Num() == 4)
		{
			ViewUniformShaderParameters.XRPassthroughCameraUVs[0] = FVector4(CameraUVs[0], CameraUVs[1]);
			ViewUniformShaderParameters.XRPassthroughCameraUVs[1] = FVector4(CameraUVs[2], CameraUVs[3]);
		}
		else
		{
			ViewUniformShaderParameters.XRPassthroughCameraUVs[0] = FVector4(0, 0, 0, 1);
			ViewUniformShaderParameters.XRPassthroughCameraUVs[1] = FVector4(1, 0, 1, 1);
		}
	}

	if (DrawDynamicFlags & EDrawDynamicFlags::FarShadowCascade)
	{
		extern ENGINE_API int32 GFarShadowStaticMeshLODBias;
		ViewUniformShaderParameters.FarShadowStaticMeshLODBias = GFarShadowStaticMeshLODBias;
	}
	else
	{
		ViewUniformShaderParameters.FarShadowStaticMeshLODBias = 0;
	}

	ViewUniformShaderParameters.PreIntegratedBRDF = GEngine->PreIntegratedSkinBRDFTexture->Resource->TextureRHI;

	ViewUniformShaderParameters.VirtualTextureFeedbackStride = SceneContext.VirtualTextureFeedback.GetFeedbackStride();
	ViewUniformShaderParameters.RuntimeVirtualTextureMipLevel = FVector4(ForceInitToZero);
	ViewUniformShaderParameters.RuntimeVirtualTexturePackHeight = FVector2D(ForceInitToZero);
	ViewUniformShaderParameters.RuntimeVirtualTextureDebugParams = FVector4(ForceInitToZero);
	
	if (UseGPUScene(GMaxRHIShaderPlatform, RHIFeatureLevel))
	{
		if (PrimitiveSceneDataOverrideSRV)
		{
			ViewUniformShaderParameters.PrimitiveSceneData = PrimitiveSceneDataOverrideSRV;
		}
		else
		{
			const FRWBufferStructured& ViewPrimitiveShaderDataBuffer = ViewState ? ViewState->PrimitiveShaderDataBuffer : OneFramePrimitiveShaderDataBuffer;

			if (ViewPrimitiveShaderDataBuffer.SRV)
			{
				ViewUniformShaderParameters.PrimitiveSceneData = ViewPrimitiveShaderDataBuffer.SRV;
			}
		}

		if (PrimitiveSceneDataTextureOverrideRHI)
		{
			ViewUniformShaderParameters.PrimitiveSceneDataTexture = PrimitiveSceneDataTextureOverrideRHI;
		}
		else
		{
			const FTextureRWBuffer2D& ViewPrimitiveShaderDataTexture = ViewState ? ViewState->PrimitiveShaderDataTexture : OneFramePrimitiveShaderDataTexture;
			ViewUniformShaderParameters.PrimitiveSceneDataTexture = OrBlack2DIfNull(ViewPrimitiveShaderDataTexture.Buffer);
		}
		
		if (LightmapSceneDataOverrideSRV)
		{
			ViewUniformShaderParameters.LightmapSceneData = LightmapSceneDataOverrideSRV;
		}
		else if (Scene && Scene->GPUScene.LightmapDataBuffer.SRV)
		{
			ViewUniformShaderParameters.LightmapSceneData = Scene->GPUScene.LightmapDataBuffer.SRV;
		}
	}

	// Default values
	SetUpViewHairRenderInfo(*this, ViewUniformShaderParameters.HairRenderInfo, ViewUniformShaderParameters.HairRenderInfoBits);

	ViewUniformShaderParameters.VTFeedbackBuffer = SceneContext.GetVirtualTextureFeedbackUAV();
	ViewUniformShaderParameters.QuadOverdraw = SceneContext.GetQuadOverdrawBufferUAV();
}

void FViewInfo::InitRHIResources()
{
	FBox VolumeBounds[TVC_MAX];

	check(IsInRenderingThread());

	CachedViewUniformShaderParameters = MakeUnique<FViewUniformShaderParameters>();

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(FRHICommandListExecutor::GetImmediateCommandList());

	SetupUniformBufferParameters(
		SceneContext,
		VolumeBounds,
		TVC_MAX,
		*CachedViewUniformShaderParameters);

	ViewUniformBuffer = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(*CachedViewUniformShaderParameters, UniformBuffer_SingleFrame);

	const int32 TranslucencyLightingVolumeDim = GetTranslucencyLightingVolumeDim();

	// Reset CachedView when CachedViewUniformShaderParameters change.
	FScene* Scene = Family->Scene ? Family->Scene->GetRenderScene() : nullptr;
	if (Scene)
	{
		Scene->UniformBuffers.CachedView = nullptr;
	}

	for (int32 CascadeIndex = 0; CascadeIndex < TVC_MAX; CascadeIndex++)
	{
		TranslucencyLightingVolumeMin[CascadeIndex] = VolumeBounds[CascadeIndex].Min;
		TranslucencyVolumeVoxelSize[CascadeIndex] = (VolumeBounds[CascadeIndex].Max.X - VolumeBounds[CascadeIndex].Min.X) / TranslucencyLightingVolumeDim;
		TranslucencyLightingVolumeSize[CascadeIndex] = VolumeBounds[CascadeIndex].Max - VolumeBounds[CascadeIndex].Min;
	}
}

// These are not real view infos, just dumb memory blocks
static TArray<FViewInfo*> ViewInfoSnapshots;
// these are never freed, even at program shutdown
static TArray<FViewInfo*> FreeViewInfoSnapshots;

extern TUniformBufferRef<FMobileDirectionalLightShaderParameters>& GetNullMobileDirectionalLightShaderParameters();

FViewInfo* FViewInfo::CreateSnapshot() const
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FViewInfo_CreateSnapshot);

	check(IsInRenderingThread()); // we do not want this popped before the end of the scene and it better be the scene allocator
	FViewInfo* Result;
	if (FreeViewInfoSnapshots.Num())
	{
		Result = FreeViewInfoSnapshots.Pop(false);
	}
	else
	{
		Result = (FViewInfo*)FMemory::Malloc(sizeof(FViewInfo), alignof(FViewInfo));
	}
	FMemory::Memcpy(*Result, *this);

	// we want these to start null without a reference count, since we clear a ref later
	TUniformBufferRef<FViewUniformShaderParameters> NullViewUniformBuffer;
	FMemory::Memcpy(Result->ViewUniformBuffer, NullViewUniformBuffer); 
	TUniformBufferRef<FMobileDirectionalLightShaderParameters> NullMobileDirectionalLightUniformBuffer;
	for (size_t i = 0; i < UE_ARRAY_COUNT(Result->MobileDirectionalLightUniformBuffers); i++)
	{
		// This memcpy is necessary to clear the reference from the memcpy of the whole of this -> Result without releasing the pointer
		// But what we really want is the null buffer.
		FMemory::Memcpy(
			&Result->MobileDirectionalLightUniformBuffers[i],
			&GetNullMobileDirectionalLightShaderParameters(),
			sizeof(TUniformBufferRef<FMobileDirectionalLightShaderParameters>));
	}

	TUniquePtr<FViewUniformShaderParameters> NullViewParameters;
	FMemory::Memcpy(Result->CachedViewUniformShaderParameters, NullViewParameters); 

	TArray<FPrimitiveUniformShaderParameters> NullDynamicPrimitiveShaderData;
	FMemory::Memcpy(Result->DynamicPrimitiveShaderData, NullDynamicPrimitiveShaderData);
	Result->DynamicPrimitiveShaderData = DynamicPrimitiveShaderData;

	FRWBufferStructured NullOneFramePrimitiveShaderDataBuffer;
	FMemory::Memcpy(Result->OneFramePrimitiveShaderDataBuffer, NullOneFramePrimitiveShaderDataBuffer);
	
	FTextureRWBuffer2D NullOneFramePrimitiveShaderDataTexture;
	FMemory::Memcpy(Result->OneFramePrimitiveShaderDataTexture, NullOneFramePrimitiveShaderDataTexture);

	TStaticArray<FParallelMeshDrawCommandPass, EMeshPass::Num> NullParallelMeshDrawCommandPasses;
	FMemory::Memcpy(Result->ParallelMeshDrawCommandPasses, NullParallelMeshDrawCommandPasses);

	for (int i = 0; i < EMeshPass::Num; i++)
	{
		Result->ParallelMeshDrawCommandPasses[i].InitCreateSnapshot();
	}
	
	Result->bIsSnapshot = true;
	ViewInfoSnapshots.Add(Result);
	return Result;
}

void FViewInfo::DestroyAllSnapshots()
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FViewInfo_DestroyAllSnapshots);

	check(IsInRenderingThread());
	// we will only keep double the number actually used, plus a few
	int32 NumToRemove = FreeViewInfoSnapshots.Num() - (ViewInfoSnapshots.Num() + 2);
	if (NumToRemove > 0)
	{
		for (int32 Index = 0; Index < NumToRemove; Index++)
		{
			FMemory::Free(FreeViewInfoSnapshots[Index]);
		}
		FreeViewInfoSnapshots.RemoveAt(0, NumToRemove, false);
	}
	for (FViewInfo* Snapshot : ViewInfoSnapshots)
	{
		Snapshot->ViewUniformBuffer.SafeRelease();
		Snapshot->CachedViewUniformShaderParameters.Reset();
		Snapshot->DynamicPrimitiveShaderData.Empty();
		Snapshot->OneFramePrimitiveShaderDataBuffer.Release();
		Snapshot->OneFramePrimitiveShaderDataTexture.Release();

		for (int32 Index = 0; Index < Snapshot->ParallelMeshDrawCommandPasses.Num(); ++Index)
		{
			Snapshot->ParallelMeshDrawCommandPasses[Index].WaitForTasksAndEmpty();
		}

		for (int i = 0; i < EMeshPass::Num; i++)
		{
			Snapshot->ParallelMeshDrawCommandPasses[i].FreeCreateSnapshot();
		}

		FreeViewInfoSnapshots.Add(Snapshot);
	}
	ViewInfoSnapshots.Reset();
}

FInt32Range FViewInfo::GetDynamicMeshElementRange(uint32 PrimitiveIndex) const
{
	int32 Start = 0;	// inclusive
	int32 AfterEnd = 0;	// exclusive

	// DynamicMeshEndIndices contains valid values only for visible primitives with bDynamicRelevance.
	if (PrimitiveVisibilityMap[PrimitiveIndex])
	{
		const FPrimitiveViewRelevance& ViewRelevance = PrimitiveViewRelevanceMap[PrimitiveIndex];
		if (ViewRelevance.bDynamicRelevance)
		{
			Start = (PrimitiveIndex == 0) ? 0 : DynamicMeshEndIndices[PrimitiveIndex - 1];
			AfterEnd = DynamicMeshEndIndices[PrimitiveIndex];
		}
	}

	return FInt32Range(Start, AfterEnd);
}

FSceneViewState* FViewInfo::GetEffectiveViewState() const
{
	FSceneViewState* EffectiveViewState = ViewState;

	// When rendering in stereo we want to use the same exposure for both eyes.
	if (IStereoRendering::IsASecondaryView(*this))
	{
		int32 ViewIndex = Family->Views.Find(this);
		if (Family->Views.IsValidIndex(ViewIndex))
		{
			// The left eye is always added before other eye views.
			ViewIndex = 0;
			if (Family->Views.IsValidIndex(ViewIndex))
			{
				const FSceneView* PrimaryView = Family->Views[ViewIndex];
				if (IStereoRendering::IsAPrimaryView(*PrimaryView))
				{
					EffectiveViewState = (FSceneViewState*)PrimaryView->State;
				}
			}
		}
	}

	return EffectiveViewState;
}

IPooledRenderTarget* FViewInfo::GetEyeAdaptation(FRHICommandList& RHICmdList) const
{
	return GetEyeAdaptationRT(RHICmdList);
}

IPooledRenderTarget* FViewInfo::GetEyeAdaptationRT(FRHICommandList& RHICmdList) const
{
	checkf(FeatureLevel > ERHIFeatureLevel::ES3_1, TEXT("SM5 and above use RenderTarget for read back"));

	FSceneViewState* EffectiveViewState = GetEffectiveViewState();
	IPooledRenderTarget* Result = NULL;
	if (EffectiveViewState)
	{
		Result = EffectiveViewState->GetCurrentEyeAdaptationRT(RHICmdList);
	}
	return Result;
}

IPooledRenderTarget* FViewInfo::GetEyeAdaptationRT() const
{
	checkf(FeatureLevel > ERHIFeatureLevel::ES3_1, TEXT("SM5 and above use RenderTarget for read back"));

	FSceneViewState* EffectiveViewState = GetEffectiveViewState();
	IPooledRenderTarget* Result = NULL;
	if (EffectiveViewState)
	{
		Result = EffectiveViewState->GetCurrentEyeAdaptationRT();
	}
	return Result;
}

IPooledRenderTarget* FViewInfo::GetLastEyeAdaptationRT(FRHICommandList& RHICmdList) const
{
	checkf(FeatureLevel > ERHIFeatureLevel::ES3_1, TEXT("SM5 and above use RenderTarget for read back"));

	FSceneViewState* EffectiveViewState = GetEffectiveViewState();
	IPooledRenderTarget* Result = NULL;
	if (EffectiveViewState)
	{
		Result = EffectiveViewState->GetLastEyeAdaptationRT(RHICmdList);
	}
	return Result;
}

void FViewInfo::SwapEyeAdaptationRTs(FRHICommandList& RHICmdList) const
{
	checkf(FeatureLevel > ERHIFeatureLevel::ES3_1, TEXT("SM5 and above use RenderTarget for read back"));

	FSceneViewState* EffectiveViewState = GetEffectiveViewState();
	if (EffectiveViewState)
	{
		EffectiveViewState->SwapEyeAdaptationRTs();
	}
}

const FExposureBufferData* FViewInfo::GetEyeAdaptationBuffer() const
{
	checkf(FeatureLevel == ERHIFeatureLevel::ES3_1, TEXT("ES3_1 use RWBuffer for read back"));

	FSceneViewState* EffectiveViewState = GetEffectiveViewState();
	const FExposureBufferData* Result = NULL;
	if (EffectiveViewState)
	{
		Result = EffectiveViewState->GetCurrentEyeAdaptationBuffer();
	}
	return Result;
}

const FExposureBufferData* FViewInfo::GetLastEyeAdaptationBuffer() const
{
	checkf(FeatureLevel == ERHIFeatureLevel::ES3_1, TEXT("ES3_1 use RWBuffer for read back"));

	FSceneViewState* EffectiveViewState = GetEffectiveViewState();
	const FExposureBufferData* Result = NULL;
	if (EffectiveViewState)
	{
		Result = EffectiveViewState->GetLastEyeAdaptationBuffer();
	}
	return Result;
}

void FViewInfo::SwapEyeAdaptationBuffers() const
{
	checkf(FeatureLevel == ERHIFeatureLevel::ES3_1, TEXT("ES3_1 use RWBuffer for read back"));

	FSceneViewState* EffectiveViewState = GetEffectiveViewState();
	if (EffectiveViewState)
	{
		EffectiveViewState->SwapEyeAdaptationBuffers();
	}
}

bool FViewInfo::HasValidEyeAdaptation() const
{
	FSceneViewState* EffectiveViewState = GetEffectiveViewState();	

	// Because eye adapation also contains pre-exposure, make sure it isn't used in scene captures.
	if (EffectiveViewState && Family && Family->bResolveScene && Family->EngineShowFlags.PostProcessing)
	{
		return EffectiveViewState->HasValidEyeAdaptation();
	}
	return false;
}

void FViewInfo::SetValidEyeAdaptation() const
{
	FSceneViewState* EffectiveViewState = GetEffectiveViewState();	

	if (EffectiveViewState)
	{
		EffectiveViewState->SetValidEyeAdaptation();
	}
}

float FViewInfo::GetLastEyeAdaptationExposure() const
{
	const FSceneViewState* EffectiveViewState = GetEffectiveViewState();	
	if (EffectiveViewState)
	{
		return EffectiveViewState->GetLastEyeAdaptationExposure();
	}
	return 0.f; // Invalid exposure
}

float FViewInfo::GetLastAverageSceneLuminance() const
{
	const FSceneViewState* EffectiveViewState = GetEffectiveViewState();
	if (EffectiveViewState)
	{
		return EffectiveViewState->GetLastAverageSceneLuminance();
	}
	return 0.f; // Invalid scene luminance
}

ERenderTargetLoadAction FViewInfo::GetOverwriteLoadAction() const
{
	return bHMDHiddenAreaMaskActive ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ENoAction;
}

void FViewInfo::SetValidTonemappingLUT() const
{
	if (FSceneViewState* EffectiveViewState = GetEffectiveViewState())
	{
		EffectiveViewState->SetValidTonemappingLUT();
	}
}

IPooledRenderTarget* FViewInfo::GetTonemappingLUT() const
{
	FSceneViewState* EffectiveViewState = GetEffectiveViewState();
	if (EffectiveViewState && EffectiveViewState->HasValidTonemappingLUT())
	{
		return EffectiveViewState->GetTonemappingLUT();
	}
	return nullptr;
};

IPooledRenderTarget* FViewInfo::GetTonemappingLUT(FRHICommandList& RHICmdList, const int32 LUTSize, const bool bUseVolumeLUT, const bool bNeedUAV, const bool bNeedFloatOutput) const 
{
	FSceneViewState* EffectiveViewState = GetEffectiveViewState();
	if (EffectiveViewState)
	{
		return EffectiveViewState->GetTonemappingLUT(RHICmdList, LUTSize, bUseVolumeLUT, bNeedUAV, bNeedFloatOutput);
	}
	return nullptr;
}

void FViewInfo::SetCustomData(const FPrimitiveSceneInfo* InPrimitiveSceneInfo, void* InCustomData)
{
	check(InPrimitiveSceneInfo != nullptr);

	if (InCustomData != nullptr && PrimitivesCustomData[InPrimitiveSceneInfo->GetIndex()] != InCustomData)
	{
		check(PrimitivesCustomData.IsValidIndex(InPrimitiveSceneInfo->GetIndex()));
		PrimitivesCustomData[InPrimitiveSceneInfo->GetIndex()] = InCustomData;
	}
}

void FDisplayInternalsData::Setup(UWorld *World)
{
	DisplayInternalsCVarValue = 0;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	DisplayInternalsCVarValue = CVarDisplayInternals.GetValueOnGameThread();

	if(IsValid())
	{
		MatineeTime = -1.0f;
		uint32 Count = 0;

		for (TObjectIterator<AMatineeActor> It; It; ++It)
		{
			AMatineeActor* MatineeActor = *It;

			if(MatineeActor->GetWorld() == World && MatineeActor->bIsPlaying)
			{
				MatineeTime = MatineeActor->InterpPosition;
				++Count;
			}
		}

		if(Count > 1)
		{
			MatineeTime = -2;
		}

		check(IsValid());
		
		extern ENGINE_API uint32 GStreamAllResourcesStillInFlight;
		NumPendingStreamingRequests = GStreamAllResourcesStillInFlight;
	}
#endif
}

void FSortedShadowMaps::Release()
{
	for (int32 AtlasIndex = 0; AtlasIndex < ShadowMapAtlases.Num(); AtlasIndex++)
	{
		ShadowMapAtlases[AtlasIndex].RenderTargets.Release();
	}

	for (int32 AtlasIndex = 0; AtlasIndex < RSMAtlases.Num(); AtlasIndex++)
	{
		RSMAtlases[AtlasIndex].RenderTargets.Release();
	}

	for (int32 AtlasIndex = 0; AtlasIndex < ShadowMapCubemaps.Num(); AtlasIndex++)
	{
		ShadowMapCubemaps[AtlasIndex].RenderTargets.Release();
	}

	PreshadowCache.RenderTargets.Release();
}

/*-----------------------------------------------------------------------------
	FSceneRenderer
-----------------------------------------------------------------------------*/

FSceneRenderer::FSceneRenderer(const FSceneViewFamily* InViewFamily,FHitProxyConsumer* HitProxyConsumer)
:	Scene(InViewFamily->Scene ? InViewFamily->Scene->GetRenderScene() : NULL)
,	ViewFamily(*InViewFamily)
,	MeshCollector(InViewFamily->GetFeatureLevel())
,	RayTracingCollector(InViewFamily->GetFeatureLevel())
,	bUsedPrecomputedVisibility(false)
,	InstancedStereoWidth(0)
,	RootMark(nullptr)
,	FamilySize(0, 0)
{
	check(Scene != NULL);

	check(IsInGameThread());
	ViewFamily.FrameNumber = Scene ? Scene->GetFrameNumber() : GFrameNumber;

	// Copy the individual views.
	bool bAnyViewIsLocked = false;
	Views.Empty(InViewFamily->Views.Num());
	for(int32 ViewIndex = 0;ViewIndex < InViewFamily->Views.Num();ViewIndex++)
	{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		for(int32 ViewIndex2 = 0;ViewIndex2 < InViewFamily->Views.Num();ViewIndex2++)
		{
			if (ViewIndex != ViewIndex2 && InViewFamily->Views[ViewIndex]->State != NULL)
			{
				// Verify that each view has a unique view state, as the occlusion query mechanism depends on it.
				check(InViewFamily->Views[ViewIndex]->State != InViewFamily->Views[ViewIndex2]->State);
			}
		}
#endif
		// Construct a FViewInfo with the FSceneView properties.
		FViewInfo* ViewInfo = new(Views) FViewInfo(InViewFamily->Views[ViewIndex]);
		ViewFamily.Views[ViewIndex] = ViewInfo;
		ViewInfo->Family = &ViewFamily;
		bAnyViewIsLocked |= ViewInfo->bIsLocked;

		check(ViewInfo->ViewRect.Area() == 0);

#if WITH_EDITOR
		// Should we allow the user to select translucent primitives?
		ViewInfo->bAllowTranslucentPrimitivesInHitProxy =
			GEngine->AllowSelectTranslucent() ||		// User preference enabled?
			!ViewInfo->IsPerspectiveProjection();		// Is orthographic view?
#endif

		// Batch the view's elements for later rendering.
		if(ViewInfo->Drawer)
		{
			FViewElementPDI ViewElementPDI(ViewInfo,HitProxyConsumer,&ViewInfo->DynamicPrimitiveShaderData);
			ViewInfo->Drawer->Draw(ViewInfo,&ViewElementPDI);
		}

		#if !UE_BUILD_SHIPPING
		if (CVarTestCameraCut.GetValueOnGameThread())
		{
			ViewInfo->bCameraCut = true;
		}
		#endif
	}

	// Catches inconsistency one engine show flags for screen percentage and whether it is supported or not.
	ensureMsgf(!(ViewFamily.EngineShowFlags.ScreenPercentage && !ViewFamily.SupportsScreenPercentage()),
		TEXT("Screen percentage is not supported, but show flag was incorectly set to true."));

	// Fork the view family.
	{
		check(InViewFamily->ScreenPercentageInterface);
		ViewFamily.ScreenPercentageInterface = nullptr;
		ViewFamily.SetScreenPercentageInterface(InViewFamily->ScreenPercentageInterface->Fork_GameThread(ViewFamily));
	}

	#if !UE_BUILD_SHIPPING
	// Override screen percentage interface.
	if (int32 OverrideId = CVarTestScreenPercentageInterface.GetValueOnGameThread())
	{
		check(ViewFamily.ScreenPercentageInterface);

		// Replaces screen percentage interface with dynamic resolution hell's driver.
		if (OverrideId == 1 && ViewFamily.Views[0]->State)
		{
			delete ViewFamily.ScreenPercentageInterface;
			ViewFamily.ScreenPercentageInterface = nullptr;
			ViewFamily.EngineShowFlags.ScreenPercentage = true;
			ViewFamily.SetScreenPercentageInterface(new FScreenPercentageHellDriver(ViewFamily));
		}
	}

	// Override secondary screen percentage for testing purpose.
	if (CVarTestSecondaryUpscaleOverride.GetValueOnGameThread() > 0 && !Views[0].bIsReflectionCapture)
	{
		ViewFamily.SecondaryViewFraction = 1.0 / float(CVarTestSecondaryUpscaleOverride.GetValueOnGameThread());
		ViewFamily.SecondaryScreenPercentageMethod = ESecondaryScreenPercentageMethod::NearestSpatialUpscale;
	}
	#endif

	// If any viewpoint has been locked, set time to zero to avoid time-based
	// rendering differences in materials.
	if (bAnyViewIsLocked)
	{
		ViewFamily.CurrentRealTime = 0.0f;
		ViewFamily.CurrentWorldTime = 0.0f;
	}
	
	if(HitProxyConsumer)
	{
		// Set the hit proxies show flag.
		ViewFamily.EngineShowFlags.SetHitProxies(1);
	}

	// launch custom visibility queries for views
	if (GCustomCullingImpl)
	{
		for(int32 ViewIndex = 0;ViewIndex < Views.Num();ViewIndex++)
		{
			FViewInfo& ViewInfo = Views[ViewIndex];
			ViewInfo.CustomVisibilityQuery = GCustomCullingImpl->CreateQuery(ViewInfo);
		}
	}

	// copy off the requests
	// (I apologize for the const_cast, but didn't seem worth refactoring just for the freezerendering command)
	bHasRequestedToggleFreeze = const_cast<FRenderTarget*>(InViewFamily->RenderTarget)->HasToggleFreezeCommand();

	FeatureLevel = Scene->GetFeatureLevel();
	ShaderPlatform = Scene->GetShaderPlatform();

	bDumpMeshDrawCommandInstancingStats = !!GDumpInstancingStats;
	GDumpInstancingStats = 0;
}

// static
FIntPoint FSceneRenderer::ApplyResolutionFraction(const FSceneViewFamily& ViewFamily, const FIntPoint& UnscaledViewSize, float ResolutionFraction)
{
	FIntPoint ViewSize;

	// CeilToInt so tha view size is at least 1x1 if ResolutionFraction == FSceneViewScreenPercentageConfig::kMinResolutionFraction.
	ViewSize.X = FMath::CeilToInt(UnscaledViewSize.X * ResolutionFraction);
	ViewSize.Y = FMath::CeilToInt(UnscaledViewSize.Y * ResolutionFraction);

	check(ViewSize.GetMin() > 0);

	return ViewSize;
}

// static
FIntPoint FSceneRenderer::QuantizeViewRectMin(const FIntPoint& ViewRectMin)
{
	FIntPoint Out;
	QuantizeSceneBufferSize(ViewRectMin, Out);
	return Out;
}

// static
FIntPoint FSceneRenderer::GetDesiredInternalBufferSize(const FSceneViewFamily& ViewFamily)
{
	// If not supporting screen percentage, bypass all computation.
	if (!ViewFamily.SupportsScreenPercentage())
	{
		FIntPoint FamilySizeUpperBound(0, 0);

		for (const FSceneView* View : ViewFamily.Views)
		{
			FamilySizeUpperBound.X = FMath::Max(FamilySizeUpperBound.X, View->UnscaledViewRect.Max.X);
			FamilySizeUpperBound.Y = FMath::Max(FamilySizeUpperBound.Y, View->UnscaledViewRect.Max.Y);
		}

		FIntPoint DesiredBufferSize;
		QuantizeSceneBufferSize(FamilySizeUpperBound, DesiredBufferSize);
		return DesiredBufferSize;
	}

	float PrimaryResolutionFractionUpperBound = ViewFamily.GetPrimaryResolutionFractionUpperBound();

	// Compute final resolution fraction.
	float ResolutionFractionUpperBound = PrimaryResolutionFractionUpperBound * ViewFamily.SecondaryViewFraction;

	FIntPoint FamilySizeUpperBound(0, 0);

	for (const FSceneView* View : ViewFamily.Views)
	{
		FIntPoint ViewSize = ApplyResolutionFraction(ViewFamily, View->UnconstrainedViewRect.Size(), ResolutionFractionUpperBound);
		FIntPoint ViewRectMin = QuantizeViewRectMin(FIntPoint(
			FMath::CeilToInt(View->UnconstrainedViewRect.Min.X * ResolutionFractionUpperBound),
			FMath::CeilToInt(View->UnconstrainedViewRect.Min.Y * ResolutionFractionUpperBound)));

		FamilySizeUpperBound.X = FMath::Max(FamilySizeUpperBound.X, ViewRectMin.X + ViewSize.X);
		FamilySizeUpperBound.Y = FMath::Max(FamilySizeUpperBound.Y, ViewRectMin.Y + ViewSize.Y);
	}

	check(FamilySizeUpperBound.GetMin() > 0);

	FIntPoint DesiredBufferSize;
	QuantizeSceneBufferSize(FamilySizeUpperBound, DesiredBufferSize);

#if !UE_BUILD_SHIPPING
	{
		// Increase the size of desired buffer size by 2 when testing for view rectangle offset.
		static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Test.ViewRectOffset"));
		if (CVar->GetValueOnAnyThread() > 0)
		{
			DesiredBufferSize *= 2;
		}
	}
#endif

	return DesiredBufferSize;
}


void FSceneRenderer::PrepareViewRectsForRendering()
{
	check(IsInRenderingThread());

	// If not supporting screen percentage, bypass all computation.
	if (!ViewFamily.SupportsScreenPercentage())
	{
		// The base pass have to respect FSceneView::UnscaledViewRect.
		for (FViewInfo& View : Views)
		{
			View.ViewRect = View.UnscaledViewRect;
		}

		ComputeFamilySize();
		return;
	}

	TArray<FSceneViewScreenPercentageConfig> ViewScreenPercentageConfigs;
	ViewScreenPercentageConfigs.Reserve(Views.Num());

	// Checks that view rects were still not initialized.
	for (FViewInfo& View : Views)
	{
		// Make sure there was no attempt to configure ViewRect and screen percentage method before.
		check(View.ViewRect.Area() == 0);

		// Fallback to no anti aliasing.
		{
			const bool bWillApplyTemporalAA = IsPostProcessingEnabled(View) || (View.bIsPlanarReflection && FeatureLevel >= ERHIFeatureLevel::SM5);

			if (!bWillApplyTemporalAA)
			{
				// Disable anti-aliasing if we are not going to be able to apply final post process effects
				View.AntiAliasingMethod = AAM_None;
			}
		}

		FSceneViewScreenPercentageConfig Config;
		ViewScreenPercentageConfigs.Add(FSceneViewScreenPercentageConfig());
	}

	check(ViewFamily.ScreenPercentageInterface);
	ViewFamily.ScreenPercentageInterface->ComputePrimaryResolutionFractions_RenderThread(ViewScreenPercentageConfigs);

	check(ViewScreenPercentageConfigs.Num() == Views.Num());

	// Checks that view rects are correctly initialized.
	for (int32 i = 0; i < Views.Num(); i++)
	{
		FViewInfo& View = Views[i];
		float PrimaryResolutionFraction = ViewScreenPercentageConfigs[i].PrimaryResolutionFraction;

		// Ensure screen percentage show flag is respected. Prefer to check() rather rendering at a differen screen percentage
		// to make sure the renderer does not lie how a frame as been rendering to a dynamic resolution heuristic.
		if (!ViewFamily.EngineShowFlags.ScreenPercentage)
		{
			checkf(PrimaryResolutionFraction == 1.0f, TEXT("It is illegal to set ResolutionFraction != 1 if screen percentage show flag is disabled."));
		}

		// Make sure the screen percentage interface has not lied to the renderer about the upper bound.
		checkf(PrimaryResolutionFraction <= ViewFamily.GetPrimaryResolutionFractionUpperBound(),
			TEXT("ISceneViewFamilyScreenPercentage::GetPrimaryResolutionFractionUpperBound() should not lie to the renderer."));
		
		check(FSceneViewScreenPercentageConfig::IsValidResolutionFraction(PrimaryResolutionFraction));

		// Compute final resolution fraction.
		float ResolutionFraction = PrimaryResolutionFraction * ViewFamily.SecondaryViewFraction;

		FIntPoint ViewSize = ApplyResolutionFraction(ViewFamily, View.UnscaledViewRect.Size(), ResolutionFraction);
		FIntPoint ViewRectMin = QuantizeViewRectMin(FIntPoint(
			FMath::CeilToInt(View.UnscaledViewRect.Min.X * ResolutionFraction),
			FMath::CeilToInt(View.UnscaledViewRect.Min.Y * ResolutionFraction)));

		// Use the bottom-left view rect if requested, instead of top-left
		if (CVarViewRectUseScreenBottom.GetValueOnRenderThread())
		{
			ViewRectMin.Y = FMath::CeilToInt( View.UnscaledViewRect.Max.Y * ViewFamily.SecondaryViewFraction ) - ViewSize.Y;
		}

		View.ViewRect.Min = ViewRectMin;
		View.ViewRect.Max = ViewRectMin + ViewSize;

		#if !UE_BUILD_SHIPPING
		// For testing purpose, override the screen percentage method.
		{
			switch (CVarTestPrimaryScreenPercentageMethodOverride.GetValueOnRenderThread())
			{
			case 1: View.PrimaryScreenPercentageMethod = EPrimaryScreenPercentageMethod::SpatialUpscale; break;
			case 2: View.PrimaryScreenPercentageMethod = EPrimaryScreenPercentageMethod::TemporalUpscale; break;
			case 3: View.PrimaryScreenPercentageMethod = EPrimaryScreenPercentageMethod::RawOutput; break;
			}
		}
		#endif

		// Automatic screen percentage fallback.
		{
			// Tenmporal upsample is supported on SM5 only if TAA is turned on.
			if (View.PrimaryScreenPercentageMethod == EPrimaryScreenPercentageMethod::TemporalUpscale &&
				(View.AntiAliasingMethod != AAM_TemporalAA ||
				 FeatureLevel < ERHIFeatureLevel::SM5 ||
				 ViewFamily.EngineShowFlags.VisualizeBuffer))
			{
				View.PrimaryScreenPercentageMethod = EPrimaryScreenPercentageMethod::SpatialUpscale;
			}
		}

		check(View.ViewRect.Area() != 0);
		check(View.VerifyMembersChecks());
	}

	// Shifts all view rects layout to the top left corner of the buffers, since post processing will just output the final
	// views in FSceneViewFamily::RenderTarget whereever it was requested with FSceneView::UnscaledViewRect.
	{
		FIntPoint TopLeftShift = Views[0].ViewRect.Min;
		for (int32 i = 1; i < Views.Num(); i++)
		{
			TopLeftShift.X = FMath::Min(TopLeftShift.X, Views[i].ViewRect.Min.X);
			TopLeftShift.Y = FMath::Min(TopLeftShift.Y, Views[i].ViewRect.Min.Y);
		}
		for (int32 i = 0; i < Views.Num(); i++)
		{
			Views[i].ViewRect -= TopLeftShift;
		}
	}

	#if !UE_BUILD_SHIPPING
	{
		int32 ViewRectOffset = CVarTestInternalViewRectOffset.GetValueOnRenderThread();

		if (Views.Num() == 1 && ViewRectOffset > 0)
		{
			FViewInfo& View = Views[0];

			FIntPoint DesiredBufferSize = GetDesiredInternalBufferSize(ViewFamily);
			FIntPoint Offset = (DesiredBufferSize - View.ViewRect.Size()) / 2;
			FIntPoint NewViewRectMin(0, 0);

			switch (ViewRectOffset)
			{
			// Move to the center of the buffer.
			case 1: NewViewRectMin = Offset; break;

			// Move to top left.
			case 2: break;

			// Move to top right.
			case 3: NewViewRectMin = FIntPoint(2 * Offset.X, 0); break;

			// Move to bottom right.
			case 4: NewViewRectMin = FIntPoint(0, 2 * Offset.Y); break;

			// Move to bottom left.
			case 5: NewViewRectMin = FIntPoint(2 * Offset.X, 2 * Offset.Y); break;
			}

			View.ViewRect += QuantizeViewRectMin(NewViewRectMin) - View.ViewRect.Min;

			check(View.VerifyMembersChecks());
		}
	}
	#endif

	ComputeFamilySize();

	// Notify StereoRenderingDevice about new ViewRects
	if (GEngine->StereoRenderingDevice.IsValid())
	{
		for (int32 i = 0; i < Views.Num(); i++)
		{
			FViewInfo& View = Views[i];
			GEngine->StereoRenderingDevice->SetFinalViewRect(View.StereoPass, View.ViewRect);
		}
	}
}

#if WITH_MGPU
void FSceneRenderer::ComputeViewGPUMasks(FRHIGPUMask RenderTargetGPUMask)
{
	// First check whether we are in multi-GPU and if fork and join cross-gpu transfers are enabled.
	// Otherwise fallback on rendering the whole view family on each relevant GPU using broadcast logic.
	if (GNumExplicitGPUsForRendering > 1 && CVarEnableMultiGPUForkAndJoin.GetValueOnAnyThread() != 0)
	{
		// Check whether this looks like an AFR setup (note that the logic also applies when there is only one AFR group).
			// Each AFR group uses multiple GPU. AFRGroup(i) = { i, NumAFRGroups + i,  2 * NumAFRGroups + i, ... } up to NumGPUs.
			// Each view rendered gets assigned to the next GPU in that group. 
		FRHIGPUMask UsableGPUMask = AFRUtils::GetGPUMaskForGroup(RenderTargetGPUMask);
		FRHIGPUMask::FIterator GPUIterator(UsableGPUMask);
		for (FViewInfo& ViewInfo : Views)
		{
			// Only handle views that are to be rendered (this excludes instance stereo).
			if (ViewInfo.ShouldRenderView())
			{
				// Multi-GPU support : This is inefficient for AFR if the reflection capture
				// updates every frame. Work is wasted on the GPUs that are not involved in
				// rendering the current frame.
				if (ViewInfo.bIsReflectionCapture || ViewInfo.bIsPlanarReflection)
				{
					ViewInfo.GPUMask = FRHIGPUMask::All();
				}
				else
				{
					ViewInfo.GPUMask = FRHIGPUMask::FromIndex(*GPUIterator);
					ViewFamily.bMultiGPUForkAndJoin |= (ViewInfo.GPUMask != RenderTargetGPUMask);
					
					// Increment and wrap around if we reach the last index.
					++GPUIterator;
					if (!GPUIterator)
					{
						GPUIterator = FRHIGPUMask::FIterator(UsableGPUMask);
					}
				}
			}
		}
	}
	else
	{
		for (FViewInfo& ViewInfo : Views)
		{
			if (ViewInfo.ShouldRenderView())
			{
				ViewInfo.GPUMask = RenderTargetGPUMask;
			}
		}
	}

	AllViewsGPUMask = Views[0].GPUMask;
	for (int32 ViewIndex = 1; ViewIndex < Views.Num(); ++ViewIndex)
	{
		AllViewsGPUMask |= Views[ViewIndex].GPUMask;
	}
}
#endif // WITH_MGPU

void FSceneRenderer::DoCrossGPUTransfers(FRHICommandListImmediate& RHICmdList, FRHIGPUMask RenderTargetGPUMask)
{
#if WITH_MGPU
	if (ViewFamily.bMultiGPUForkAndJoin)
	{
		const FTexture2DRHIRef& SceneRenderTarget = ViewFamily.RenderTarget->GetRenderTargetTexture();
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
		{
			const FViewInfo& ViewInfo = Views[ViewIndex];
			if (ViewInfo.GPUMask != RenderTargetGPUMask)
			{
				// Clamp the view rect by the rendertarget rect to prevent issues when resizing the viewport.
				const FIntRect TransferRect(ViewInfo.ViewRect.Min.ComponentMin(SceneRenderTarget->GetSizeXY()), ViewInfo.ViewRect.Max.ComponentMin(SceneRenderTarget->GetSizeXY()));
				if (TransferRect.Width() > 0 && TransferRect.Height() > 0)
				{
					for (uint32 RenderTargetGPUIndex : RenderTargetGPUMask)
					{
						if (!ViewInfo.GPUMask.Contains(RenderTargetGPUIndex))
						{
							RHICmdList.TransferTexture(SceneRenderTarget, TransferRect, ViewInfo.GPUMask.GetFirstIndex(), RenderTargetGPUIndex, true);
						}
					}
				}
			}
		}
	}
#endif
}


void FSceneRenderer::ComputeFamilySize()
{
	check(FamilySize.X == 0);
	check(IsInRenderingThread());

	// Calculate the screen extents of the view family.
	bool bInitializedExtents = false;
	float MaxFamilyX = 0;
	float MaxFamilyY = 0;

	for (const FViewInfo& View : Views)
	{
		float FinalViewMaxX = (float)View.ViewRect.Max.X;
		float FinalViewMaxY = (float)View.ViewRect.Max.Y;

		// Derive the amount of scaling needed for screenpercentage from the scaled / unscaled rect
		const float XScale = FinalViewMaxX / (float)View.UnscaledViewRect.Max.X;
		const float YScale = FinalViewMaxY / (float)View.UnscaledViewRect.Max.Y;

		if (!bInitializedExtents)
		{
			// Note: using the unconstrained view rect to compute family size
			// In the case of constrained views (black bars) this means the scene render targets will fill the whole screen
			// Which is needed for ES2 paths where we render directly to the backbuffer, and the scene depth buffer has to match in size
			MaxFamilyX = View.UnconstrainedViewRect.Max.X * XScale;
			MaxFamilyY = View.UnconstrainedViewRect.Max.Y * YScale;
			bInitializedExtents = true;
		}
		else
		{
			MaxFamilyX = FMath::Max(MaxFamilyX, View.UnconstrainedViewRect.Max.X * XScale);
			MaxFamilyY = FMath::Max(MaxFamilyY, View.UnconstrainedViewRect.Max.Y * YScale);
		}

		// floating point imprecision could cause MaxFamilyX to be less than View->ViewRect.Max.X after integer truncation.
		// since this value controls rendertarget sizes, we don't want to create rendertargets smaller than the view size.
		MaxFamilyX = FMath::Max(MaxFamilyX, FinalViewMaxX);
		MaxFamilyY = FMath::Max(MaxFamilyY, FinalViewMaxY);

		if (!IStereoRendering::IsAnAdditionalView(View))
		{
			InstancedStereoWidth = FPlatformMath::Max(InstancedStereoWidth, static_cast<uint32>(View.ViewRect.Max.X));
		}
	}

	// We render to the actual position of the viewports so with black borders we need the max.
	// We could change it by rendering all to left top but that has implications for splitscreen. 
	FamilySize.X = FMath::TruncToInt(MaxFamilyX);
	FamilySize.Y = FMath::TruncToInt(MaxFamilyY);

	check(FamilySize.X != 0);
	check(bInitializedExtents);
}

bool FSceneRenderer::DoOcclusionQueries(ERHIFeatureLevel::Type InFeatureLevel) const
{
	return InFeatureLevel >= ERHIFeatureLevel::ES3_1 && CVarAllowOcclusionQueries.GetValueOnRenderThread() != 0;
}

FSceneRenderer::~FSceneRenderer()
{	
	if(Scene)
	{
		// Destruct the projected shadow infos.
		for(TSparseArray<FLightSceneInfoCompact>::TConstIterator LightIt(Scene->Lights);LightIt;++LightIt)
		{
			if( VisibleLightInfos.IsValidIndex(LightIt.GetIndex()) )
			{
				FVisibleLightInfo& VisibleLightInfo = VisibleLightInfos[LightIt.GetIndex()];
				for(int32 ShadowIndex = 0;ShadowIndex < VisibleLightInfo.MemStackProjectedShadows.Num();ShadowIndex++)
				{
					// FProjectedShadowInfo's in MemStackProjectedShadows were allocated on the rendering thread mem stack, 
					// Their memory will be freed when the stack is freed with no destructor call, so invoke the destructor explicitly
					VisibleLightInfo.MemStackProjectedShadows[ShadowIndex]->~FProjectedShadowInfo();
				}
			}
		}
	}

	// Manually release references to TRefCountPtrs that are allocated on the mem stack, which doesn't call dtors
	SortedShadowsForShadowDepthPass.Release();

	Views.Empty();
}

/** 
* Finishes the view family rendering.
*/
void FSceneRenderer::RenderFinish(FRHICommandListImmediate& RHICmdList)
{
	SCOPED_DRAW_EVENT(RHICmdList, RenderFinish);

	if(FRCPassPostProcessBusyWait::IsPassRequired())
	{
		// mostly view independent but to be safe we use the first view
		FViewInfo& View = Views[0];

		FMemMark Mark(FMemStack::Get());
		FRenderingCompositePassContext CompositeContext(RHICmdList, View);

		FRenderingCompositeOutputRef BusyWait;
		{
			// for debugging purpose, can be controlled by console variable
			FRenderingCompositePass* Node = CompositeContext.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessBusyWait());
			BusyWait = FRenderingCompositeOutputRef(Node);
		}		
		
		if(BusyWait.IsValid())
		{
			CompositeContext.Process(BusyWait.GetPass(), TEXT("RenderFinish"));
		}
	}
	
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	{
		bool bShowPrecomputedVisibilityWarning = false;
		static const auto* CVarPrecomputedVisibilityWarning = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.PrecomputedVisibilityWarning"));
		if (CVarPrecomputedVisibilityWarning && CVarPrecomputedVisibilityWarning->GetValueOnRenderThread() == 1)
		{
			bShowPrecomputedVisibilityWarning = !bUsedPrecomputedVisibility;
		}

		bool bShowGlobalClipPlaneWarning = false;

		if (Scene->PlanarReflections.Num() > 0)
		{
			static const auto* CVarClipPlane = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.AllowGlobalClipPlane"));
			if (CVarClipPlane && CVarClipPlane->GetValueOnRenderThread() == 0)
			{
				bShowGlobalClipPlaneWarning = true;
			}
		}
		
		const FReadOnlyCVARCache& ReadOnlyCVARCache = Scene->ReadOnlyCVARCache;
		static auto* CVarSkinCacheOOM = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.SkinCache.SceneMemoryLimitInMB"));

		uint64 GPUSkinCacheExtraRequiredMemory = 0;
		extern ENGINE_API bool IsGPUSkinCacheAvailable(EShaderPlatform Platform);
		if (IsGPUSkinCacheAvailable(ShaderPlatform))
		{
			if (FGPUSkinCache* SkinCache = Scene->GetGPUSkinCache())
			{
				GPUSkinCacheExtraRequiredMemory = SkinCache->GetExtraRequiredMemoryAndReset();
			}
		}
		const bool bShowSkinCacheOOM = CVarSkinCacheOOM != nullptr && GPUSkinCacheExtraRequiredMemory > 0;

		static const auto* CVarGenerateMeshDistanceFields = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.GenerateMeshDistanceFields"));
		const bool bMeshDistanceFieldEnabled = CVarGenerateMeshDistanceFields != nullptr && CVarGenerateMeshDistanceFields->GetValueOnRenderThread() > 0;
		extern bool UseDistanceFieldAO();
		const bool bShowDFAODisabledWarning = !UseDistanceFieldAO() && (ViewFamily.EngineShowFlags.VisualizeDistanceFieldAO);
		const bool bShowDFDisabledWarning = !bMeshDistanceFieldEnabled && (ViewFamily.EngineShowFlags.VisualizeMeshDistanceFields || ViewFamily.EngineShowFlags.VisualizeGlobalDistanceField || ViewFamily.EngineShowFlags.VisualizeDistanceFieldAO);

		const bool bShowAtmosphericFogWarning = Scene->AtmosphericFog != nullptr && !ReadOnlyCVARCache.bEnableAtmosphericFog;

		const bool bStationarySkylight = Scene->SkyLight && Scene->SkyLight->bWantsStaticShadowing;
		const bool bShowSkylightWarning = bStationarySkylight && !ReadOnlyCVARCache.bEnableStationarySkylight;

		const bool bShowPointLightWarning = UsedWholeScenePointLightNames.Num() > 0 && !ReadOnlyCVARCache.bEnablePointLightShadows;
		const bool bShowShadowedLightOverflowWarning = Scene->OverflowingDynamicShadowedLights.Num() > 0;

		// Mobile-specific warnings
		const bool bMobile = (FeatureLevel <= ERHIFeatureLevel::ES3_1);
		const bool bShowMobileLowQualityLightmapWarning = bMobile && !ReadOnlyCVARCache.bEnableLowQualityLightmaps && ReadOnlyCVARCache.bAllowStaticLighting;
		const bool bShowMobileDynamicCSMWarning = bMobile && Scene->NumMobileStaticAndCSMLights_RenderThread > 0 && !(ReadOnlyCVARCache.bMobileEnableStaticAndCSMShadowReceivers && ReadOnlyCVARCache.bMobileAllowDistanceFieldShadows);
		const bool bShowMobileMovableDirectionalLightWarning = bMobile && Scene->NumMobileMovableDirectionalLights_RenderThread > 0 && !ReadOnlyCVARCache.bMobileAllowMovableDirectionalLights;
		
		bool bMobileShowVertexFogWarning = false;
		if (bMobile && Scene->ExponentialFogs.Num() > 0)
		{
			static const auto* CVarDisableVertexFog = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.DisableVertexFog"));
			if (CVarDisableVertexFog && CVarDisableVertexFog->GetValueOnRenderThread() != 0)
			{
				bMobileShowVertexFogWarning = true;
			}
		}
		
		const bool bSingleLayerWaterWarning = ShouldRenderSingleLayerWaterSkippedRenderEditorNotification(Views);
		
		const bool bAnyWarning = bShowPrecomputedVisibilityWarning || bShowGlobalClipPlaneWarning || bShowAtmosphericFogWarning || bShowSkylightWarning || bShowPointLightWarning 
			|| bShowDFAODisabledWarning || bShowShadowedLightOverflowWarning || bShowMobileDynamicCSMWarning || bShowMobileLowQualityLightmapWarning || bShowMobileMovableDirectionalLightWarning
			|| bMobileShowVertexFogWarning || bShowSkinCacheOOM || bSingleLayerWaterWarning || bShowDFDisabledWarning;

		for(int32 ViewIndex = 0;ViewIndex < Views.Num();ViewIndex++)
		{	
			FViewInfo& View = Views[ViewIndex];
			if (!View.bIsReflectionCapture && !View.bIsSceneCapture )
			{
				// display a message saying we're frozen
				FSceneViewState* ViewState = (FSceneViewState*)View.State;
				bool bViewParentOrFrozen = ViewState && (ViewState->HasViewParent() || ViewState->bIsFrozen);
				bool bLocked = View.bIsLocked;
				if (GAreScreenMessagesEnabled && (bViewParentOrFrozen || bLocked || bAnyWarning))
				{
					SCOPED_CONDITIONAL_DRAW_EVENTF(RHICmdList, EventView, Views.Num() > 1, TEXT("View%d"), ViewIndex);

					FRenderTargetTemp TempRenderTarget(View);

					RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, TempRenderTarget.GetRenderTargetTexture());

					// create a temporary FCanvas object with the temp render target
					// so it can get the screen size
					int32 Y = 130;
					FCanvas Canvas(&TempRenderTarget, NULL, View.Family->CurrentRealTime, View.Family->CurrentWorldTime, View.Family->DeltaWorldTime, FeatureLevel);
					// Make sure draws to the canvas are not rendered upside down.
					Canvas.SetAllowSwitchVerticalAxis(true);
					if (bViewParentOrFrozen)
					{
						const FText StateText =
							ViewState->bIsFrozen ?
							NSLOCTEXT("SceneRendering", "RenderingFrozen", "Rendering frozen...")
							:
							NSLOCTEXT("SceneRendering", "OcclusionChild", "Occlusion Child");
						Canvas.DrawShadowedText(10, Y, StateText, GetStatsFont(), FLinearColor(0.8, 1.0, 0.2, 1.0));
						Y += 14;
					}
					if (bShowPrecomputedVisibilityWarning)
					{
						static const FText Message = NSLOCTEXT("Renderer", "NoPrecomputedVisibility", "NO PRECOMPUTED VISIBILITY");
						Canvas.DrawShadowedText(10, Y, Message, GetStatsFont(), FLinearColor(1.0, 0.05, 0.05, 1.0));
						Y += 14;
					}
					if (bShowGlobalClipPlaneWarning)
					{
						static const FText Message = NSLOCTEXT("Renderer", "NoGlobalClipPlane", "PLANAR REFLECTION REQUIRES GLOBAL CLIP PLANE PROJECT SETTING ENABLED TO WORK PROPERLY");
						Canvas.DrawShadowedText(10, Y, Message, GetStatsFont(), FLinearColor(1.0, 0.05, 0.05, 1.0));
						Y += 14;
					}
					if (bShowDFAODisabledWarning)
					{
						static const FText Message = NSLOCTEXT("Renderer", "DFAODisabled", "Distance Field AO is disabled through scalability");
						Canvas.DrawShadowedText(10, Y, Message, GetStatsFont(), FLinearColor(1.0, 0.05, 0.05, 1.0));
						Y += 14;
					}
					if (bShowDFDisabledWarning)
					{
						static const FText Message = NSLOCTEXT("Renderer", "DFDisabled", "Mesh distance fields generation is disabled by project settings, cannot visualize DFAO, mesh or global distance field.");
						Canvas.DrawShadowedText(10, Y, Message, GetStatsFont(), FLinearColor(1.0, 0.05, 0.05, 1.0));
						Y += 14;
					}

					if (bShowAtmosphericFogWarning)
					{
						static const FText Message = NSLOCTEXT("Renderer", "AtmosphericFog", "PROJECT DOES NOT SUPPORT ATMOSPHERIC FOG");
						Canvas.DrawShadowedText(10, Y, Message, GetStatsFont(), FLinearColor(1.0, 0.05, 0.05, 1.0));
						Y += 14;						
					}
					if (bShowSkylightWarning)
					{
						static const FText Message = NSLOCTEXT("Renderer", "Skylight", "PROJECT DOES NOT SUPPORT STATIONARY SKYLIGHT: ");
						Canvas.DrawShadowedText(10, Y, Message, GetStatsFont(), FLinearColor(1.0, 0.05, 0.05, 1.0));
						Y += 14;
					}
					if (bShowPointLightWarning)
					{
						static const FText Message = NSLOCTEXT("Renderer", "PointLight", "PROJECT DOES NOT SUPPORT WHOLE SCENE POINT LIGHT SHADOWS: ");
						Canvas.DrawShadowedText(10, Y, Message, GetStatsFont(), FLinearColor(1.0, 0.05, 0.05, 1.0));
						Y += 14;

						for (FName LightName : UsedWholeScenePointLightNames)
						{
							Canvas.DrawShadowedText(10, Y, FText::FromString(LightName.ToString()), GetStatsFont(), FLinearColor(1.0, 0.05, 0.05, 1.0));
							Y += 14;
						}
					}
					if (bShowShadowedLightOverflowWarning)
					{
						static const FText Message = NSLOCTEXT("Renderer", "ShadowedLightOverflow", "TOO MANY OVERLAPPING SHADOWED MOVABLE LIGHTS, SHADOW CASTING DISABLED: ");
						Canvas.DrawShadowedText(10, Y, Message, GetStatsFont(), FLinearColor(1.0, 0.05, 0.05, 1.0));
						Y += 14;

						for (FName LightName : Scene->OverflowingDynamicShadowedLights)
						{
							Canvas.DrawShadowedText(10, Y, FText::FromString(LightName.ToString()), GetStatsFont(), FLinearColor(1.0, 0.05, 0.05, 1.0));
							Y += 14;
						}
					}
					if (bShowMobileLowQualityLightmapWarning)
					{
						static const FText Message = NSLOCTEXT("Renderer", "MobileLQLightmap", "MOBILE PROJECTS SUPPORTING STATIC LIGHTING MUST HAVE LQ LIGHTMAPS ENABLED");
						Canvas.DrawShadowedText(10, Y, Message, GetStatsFont(), FLinearColor(1.0, 0.05, 0.05, 1.0));
						Y += 14;
					}
					if (bShowMobileMovableDirectionalLightWarning)
					{
						static const FText Message = NSLOCTEXT("Renderer", "MobileMovableDirectional", "PROJECT HAS MOVABLE DIRECTIONAL LIGHTS ON MOBILE DISABLED");
						Canvas.DrawShadowedText(10, Y, Message, GetStatsFont(), FLinearColor(1.0, 0.05, 0.05, 1.0));
						Y += 14;
					}
					if (bShowMobileDynamicCSMWarning)
					{
						static const FText Message = (!ReadOnlyCVARCache.bMobileEnableStaticAndCSMShadowReceivers)
							? NSLOCTEXT("Renderer", "MobileDynamicCSM", "PROJECT HAS MOBILE CSM SHADOWS FROM STATIONARY DIRECTIONAL LIGHTS DISABLED")
							: NSLOCTEXT("Renderer", "MobileDynamicCSMDistFieldShadows", "MOBILE CSM+STATIC REQUIRES DISTANCE FIELD SHADOWS ENABLED FOR PROJECT");
						Canvas.DrawShadowedText(10, Y, Message, GetStatsFont(), FLinearColor(1.0, 0.05, 0.05, 1.0));
						Y += 14;
					}
					if (bMobileShowVertexFogWarning)
					{
						static const FText Message = NSLOCTEXT("Renderer", "MobileVertexFog", "PROJECT HAS VERTEX FOG ON MOBILE DISABLED");
						Canvas.DrawShadowedText(10, Y, Message, GetStatsFont(), FLinearColor(1.0, 0.05, 0.05, 1.0));
						Y += 14;
					}
					
					if (bShowSkinCacheOOM)
					{
						FString String = FString::Printf(TEXT("OUT OF MEMORY FOR SKIN CACHE, REQUIRES %.3f extra MB (currently at %.3f)"), (float)GPUSkinCacheExtraRequiredMemory / 1048576.0f, CVarSkinCacheOOM->GetValueOnAnyThread());
						Canvas.DrawShadowedText(10, Y, FText::FromString(String), GetStatsFont(), FLinearColor(1.0, 0.05, 0.05, 1.0));
						Y += 14;
					}

					if (bLocked)
					{
						static const FText Message = NSLOCTEXT("Renderer", "ViewLocked", "VIEW LOCKED");
						Canvas.DrawShadowedText(10, Y, Message, GetStatsFont(), FLinearColor(0.8, 1.0, 0.2, 1.0));
						Y += 14;
					}

					if (bSingleLayerWaterWarning)
					{
						static const FText Message = NSLOCTEXT("Renderer", "SingleLayerWater", "r.Water.SingleLayer rendering is disabled with a view containing meshe(s) using water material. Meshes are not visible.");
						Canvas.DrawShadowedText(10, Y, Message, GetStatsFont(), FLinearColor(1.0, 0.05, 0.05, 1.0));
						Y += 14;
					}

					Canvas.Flush_RenderThread(RHICmdList);
				}
				
				// Software occlusion debug draw
				if (ViewState && ViewState->SceneSoftwareOcclusion)
				{
					ViewState->SceneSoftwareOcclusion->DebugDraw(RHICmdList, View, 20, 20);
				}
			}
		}
	}
	
#endif //!(UE_BUILD_SHIPPING || UE_BUILD_TEST)

	// Save the post-occlusion visibility stats for the frame and freezing info
	for(int32 ViewIndex = 0;ViewIndex < Views.Num();ViewIndex++)
	{
		const FViewInfo& View = Views[ViewIndex];
		INC_DWORD_STAT_BY(STAT_VisibleStaticMeshElements,View.NumVisibleStaticMeshElements);
		INC_DWORD_STAT_BY(STAT_VisibleDynamicPrimitives,View.NumVisibleDynamicPrimitives);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		// update freezing info
		FSceneViewState* ViewState = (FSceneViewState*)View.State;
		if (ViewState)
		{
			// if we're finished freezing, now we are frozen
			if (ViewState->bIsFreezing)
			{
				ViewState->bIsFreezing = false;
				ViewState->bIsFrozen = true;
				ViewState->bIsFrozenViewMatricesCached = true;
				ViewState->CachedViewMatrices = View.ViewMatrices;
			}

			// handle freeze toggle request
			if (bHasRequestedToggleFreeze)
			{
				// do we want to start freezing or stop?
				ViewState->bIsFreezing = !ViewState->bIsFrozen;
				ViewState->bIsFrozen = false;
				ViewState->bIsFrozenViewMatricesCached = false;
				ViewState->FrozenPrimitives.Empty();
			}
		}
#endif
	}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	// clear the commands
	bHasRequestedToggleFreeze = false;

	if(ViewFamily.EngineShowFlags.OnScreenDebug)
	{
		for(int32 ViewIndex = 0;ViewIndex < Views.Num();ViewIndex++)
		{
			const FViewInfo& View = Views[ViewIndex];

			if(!View.IsPerspectiveProjection())
			{
				continue;
			}

			FVisualizeTexturePresent::PresentContent(RHICmdList, View);
		}
	}
#endif

	{
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_ViewExtensionPostRenderView);
		for(int32 ViewExt = 0; ViewExt < ViewFamily.ViewExtensions.Num(); ++ViewExt)
		{
			ViewFamily.ViewExtensions[ViewExt]->PostRenderViewFamily_RenderThread(RHICmdList, ViewFamily);
			for(int32 ViewIndex = 0; ViewIndex < ViewFamily.Views.Num(); ++ViewIndex)
			{
				ViewFamily.ViewExtensions[ViewExt]->PostRenderView_RenderThread(RHICmdList, Views[ViewIndex]);
			}
		}
	}

	// Notify the RHI we are done rendering a scene.
	RHICmdList.EndScene();

	if (GDumpMeshDrawCommandMemoryStats)
	{
		GDumpMeshDrawCommandMemoryStats = 0;
		Scene->DumpMeshDrawCommandMemoryStats();
	}
}

void FSceneRenderer::SetupMeshPass(FViewInfo& View, FExclusiveDepthStencil::Type BasePassDepthStencilAccess, FViewCommands& ViewCommands)
{
	SCOPE_CYCLE_COUNTER(STAT_SetupMeshPass);

	const EShadingPath ShadingPath = Scene->GetShadingPath();

	for (int32 PassIndex = 0; PassIndex < EMeshPass::Num; PassIndex++)
	{
		const EMeshPass::Type PassType = (EMeshPass::Type)PassIndex;

		if ((FPassProcessorManager::GetPassFlags(ShadingPath, PassType) & EMeshPassFlags::MainView) != EMeshPassFlags::None)
		{
			// Mobile: BasePass and MobileBasePassCSM lists need to be merged and sorted after shadow pass.
			if (ShadingPath == EShadingPath::Mobile && (PassType == EMeshPass::BasePass || PassType == EMeshPass::MobileBasePassCSM))
			{
				continue;
			}

			if (ViewFamily.UseDebugViewPS() && ShadingPath == EShadingPath::Deferred)
			{
				switch (PassType)
				{
					case EMeshPass::DepthPass:
					case EMeshPass::CustomDepth:
					case EMeshPass::DebugViewMode:
#if WITH_EDITOR
					case EMeshPass::HitProxy:
					case EMeshPass::HitProxyOpaqueOnly:
					case EMeshPass::EditorSelection:
#endif
						break;
					default:
						continue;
				}
			}

			PassProcessorCreateFunction CreateFunction = FPassProcessorManager::GetCreateFunction(ShadingPath, PassType);
			FMeshPassProcessor* MeshPassProcessor = CreateFunction(Scene, &View, nullptr);

			FParallelMeshDrawCommandPass& Pass = View.ParallelMeshDrawCommandPasses[PassIndex];

			if (ShouldDumpMeshDrawCommandInstancingStats())
			{
				Pass.SetDumpInstancingStats(GetMeshPassName(PassType));
			}

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
}

FSceneRenderer* FSceneRenderer::CreateSceneRenderer(const FSceneViewFamily* InViewFamily, FHitProxyConsumer* HitProxyConsumer)
{
	EShadingPath ShadingPath = InViewFamily->Scene->GetShadingPath();
	FSceneRenderer* SceneRenderer = nullptr;

	if (ShadingPath == EShadingPath::Deferred)
	{
		SceneRenderer = new FDeferredShadingSceneRenderer(InViewFamily, HitProxyConsumer);
	}
	else 
	{
		check(ShadingPath == EShadingPath::Mobile);
		SceneRenderer = new FMobileSceneRenderer(InViewFamily, HitProxyConsumer);
	}

	return SceneRenderer;
}

void ServiceLocalQueue();

void FSceneRenderer::RenderCustomDepthPassAtLocation(FRHICommandListImmediate& RHICmdList, int32 Location)
{		
	extern TAutoConsoleVariable<int32> CVarCustomDepthOrder;
	int32 CustomDepthOrder = FMath::Clamp(CVarCustomDepthOrder.GetValueOnRenderThread(), 0, 1);

	if(CustomDepthOrder == Location)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_CustomDepthPass);
		RenderCustomDepthPass(RHICmdList);
		ServiceLocalQueue();
	}
}

void FSceneRenderer::RenderCustomDepthPass(FRHICommandListImmediate& RHICmdList)
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(RenderCustomDepthPass);

	// do we have primitives in this pass?
	bool bPrimitives = false;

	if(!Scene->World || (Scene->World->WorldType != EWorldType::EditorPreview && Scene->World->WorldType != EWorldType::Inactive))
	{
		for(int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
		{
			const FViewInfo& View = Views[ViewIndex];
			if (View.bHasCustomDepthPrimitives)
			{
				bPrimitives = true;
				break;
			}
		}
	}

	// Render CustomDepth
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	if (SceneContext.BeginRenderingCustomDepth(RHICmdList, bPrimitives))
	{
		SCOPED_DRAW_EVENT(RHICmdList, CustomDepth);
		SCOPED_GPU_STAT(RHICmdList, CustomDepth);

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			SCOPED_CONDITIONAL_DRAW_EVENTF(RHICmdList, EventView, Views.Num() > 1, TEXT("View%d"), ViewIndex);

			FViewInfo& View = Views[ViewIndex];

			if (View.ShouldRenderView())
			{
				FRHIUniformBuffer* PassUniformBuffer = nullptr;

				bool bMobilePath = FSceneInterface::GetShadingPath(View.FeatureLevel) == EShadingPath::Mobile;

				if (bMobilePath)
				{
					FMobileSceneTextureUniformParameters SceneTextureParameters;
					SetupMobileSceneTextureUniformParameters(SceneContext, View.FeatureLevel, false, false, SceneTextureParameters);
					Scene->UniformBuffers.MobileCustomDepthPassUniformBuffer.UpdateUniformBufferImmediate(SceneTextureParameters);
					PassUniformBuffer = Scene->UniformBuffers.MobileCustomDepthPassUniformBuffer;
				}
				else
				{
					FSceneTexturesUniformParameters SceneTextureParameters;
					SetupSceneTextureUniformParameters(SceneContext, View.FeatureLevel, ESceneTextureSetupMode::None, SceneTextureParameters);
					Scene->UniformBuffers.CustomDepthPassUniformBuffer.UpdateUniformBufferImmediate(SceneTextureParameters);
					PassUniformBuffer = Scene->UniformBuffers.CustomDepthPassUniformBuffer;
				}
				
				static const auto MobileCustomDepthDownSampleLocalCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.CustomDepthDownSample"));

				bool bMobileCustomDepthDownSample = bMobilePath && MobileCustomDepthDownSampleLocalCVar && MobileCustomDepthDownSampleLocalCVar->GetValueOnRenderThread() > 0;

				FIntRect ViewRect = bMobileCustomDepthDownSample ? FIntRect::DivideAndRoundUp(View.ViewRect, 2) : View.ViewRect;

				if (!View.IsInstancedStereoPass())
				{
					RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);
				}
				else
				{
					if (View.bIsMultiViewEnabled)
					{
						FIntRect ViewRect0 = bMobileCustomDepthDownSample ? FIntRect::DivideAndRoundUp(Views[0].ViewRect, 2) : Views[0].ViewRect;
						FIntRect ViewRect1 = bMobileCustomDepthDownSample ? FIntRect::DivideAndRoundUp(Views[1].ViewRect, 2) : Views[1].ViewRect;

						const uint32 LeftMinX = ViewRect0.Min.X;
						const uint32 LeftMaxX = ViewRect0.Max.X;
						const uint32 RightMinX = ViewRect1.Min.X;
						const uint32 RightMaxX = ViewRect1.Max.X;
						const uint32 LeftMaxY = ViewRect0.Max.Y;
						const uint32 RightMaxY = ViewRect1.Max.Y;
						RHICmdList.SetStereoViewport(LeftMinX, RightMinX, 0, 0, 0.0f, LeftMaxX, RightMaxX, LeftMaxY, RightMaxY, 1.0f);
					}
					else
					{
						RHICmdList.SetViewport(0, 0, 0, InstancedStereoWidth, ViewRect.Max.Y, 1);
					}
				}

				if (CVarCustomDepthTemporalAAJitter.GetValueOnRenderThread() == 0 && View.AntiAliasingMethod == AAM_TemporalAA)
				{
					// Handle the "current" view the same, always.
					FViewUniformShaderParameters CustomDepthViewUniformBufferParameters = *View.CachedViewUniformShaderParameters;

					FBox VolumeBounds[TVC_MAX];

					FViewMatrices ModifiedViewMatrices = View.ViewMatrices;
					ModifiedViewMatrices.HackRemoveTemporalAAProjectionJitter();

					View.SetupUniformBufferParameters(
						SceneContext,
						ModifiedViewMatrices,
						ModifiedViewMatrices,
						VolumeBounds,
						TVC_MAX,
						CustomDepthViewUniformBufferParameters);

					Scene->UniformBuffers.CustomDepthViewUniformBuffer.UpdateUniformBufferImmediate(CustomDepthViewUniformBufferParameters);

					if ((View.IsInstancedStereoPass() || View.bIsMobileMultiViewEnabled) && View.Family->Views.Num() > 0)
					{
						// When drawing the left eye in a stereo scene, set up the instanced custom depth uniform buffer with the right-eye data,
						// with the TAA jitter removed.
						const EStereoscopicPass StereoPassIndex = IStereoRendering::IsStereoEyeView(View) ? eSSP_RIGHT_EYE : eSSP_FULL;

						const FViewInfo& InstancedView = static_cast<const FViewInfo&>(View.Family->GetStereoEyeView(StereoPassIndex));

						CustomDepthViewUniformBufferParameters = *InstancedView.CachedViewUniformShaderParameters;
						ModifiedViewMatrices = InstancedView.ViewMatrices;
						ModifiedViewMatrices.HackRemoveTemporalAAProjectionJitter();

						InstancedView.SetupUniformBufferParameters(
							SceneContext,
							ModifiedViewMatrices,
							ModifiedViewMatrices,
							VolumeBounds,
							TVC_MAX,
							CustomDepthViewUniformBufferParameters);

						Scene->UniformBuffers.InstancedCustomDepthViewUniformBuffer.UpdateUniformBufferImmediate(reinterpret_cast<FInstancedViewUniformShaderParameters&>(CustomDepthViewUniformBufferParameters));
					}
				}
				else
				{
					Scene->UniformBuffers.CustomDepthViewUniformBuffer.UpdateUniformBufferImmediate(*View.CachedViewUniformShaderParameters);
					if ((View.IsInstancedStereoPass() || View.bIsMobileMultiViewEnabled) && View.Family->Views.Num() > 0)
					{
						const FViewInfo& InstancedView = Scene->UniformBuffers.GetInstancedView(View);
						Scene->UniformBuffers.InstancedCustomDepthViewUniformBuffer.UpdateUniformBufferImmediate(reinterpret_cast<FInstancedViewUniformShaderParameters&>(*InstancedView.CachedViewUniformShaderParameters));
					}
					else
					{
						// If we don't render this pass in stereo we simply update the buffer with the same view uniform parameters.
						Scene->UniformBuffers.InstancedCustomDepthViewUniformBuffer.UpdateUniformBufferImmediate(reinterpret_cast<FInstancedViewUniformShaderParameters&>(*View.CachedViewUniformShaderParameters));
					}
				}
	
				extern TSet<IPersistentViewUniformBufferExtension*> PersistentViewUniformBufferExtensions;
				
				for (IPersistentViewUniformBufferExtension* Extension : PersistentViewUniformBufferExtensions)
				{
					Extension->BeginRenderView(&View);
				}
	
				View.ParallelMeshDrawCommandPasses[EMeshPass::CustomDepth].DispatchDraw(nullptr, RHICmdList);
			}
		}

		// resolve using the current ResolveParams 
		SceneContext.FinishRenderingCustomDepth(RHICmdList);
	}
}

void FSceneRenderer::OnStartRender(FRHICommandListImmediate& RHICmdList)
{
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

	FVisualizeTexturePresent::OnStartRender(Views[0]);
	CompositionGraph_OnStartFrame();
	SceneContext.bScreenSpaceAOIsValid = false;
	SceneContext.bCustomDepthIsValid = false;

	for (FViewInfo& View : Views)
	{
		if (View.ViewState)
		{
			View.ViewState->OnStartRender(View, ViewFamily);
		}
	}
}

bool FSceneRenderer::ShouldCompositeEditorPrimitives(const FViewInfo& View)
{
	if (View.Family->EngineShowFlags.VisualizeHDR || View.Family->UseDebugViewPS())
	{
		// certain visualize modes get obstructed too much
		return false;
	}

	if (View.Family->EngineShowFlags.Wireframe)
	{
		// We want wireframe view use MSAA if possible.
		return true;
	}
	else if (View.Family->EngineShowFlags.CompositeEditorPrimitives)
	{
	    // Any elements that needed compositing were drawn then compositing should be done
	    if (View.ViewMeshElements.Num() 
		    || View.TopViewMeshElements.Num() 
		    || View.BatchedViewElements.HasPrimsToDraw() 
		    || View.TopBatchedViewElements.HasPrimsToDraw() 
		    || View.NumVisibleDynamicEditorPrimitives > 0
			|| IsMobileColorsRGB())
	    {
		    return true;
	    }
	}

	return false;
}

void FSceneRenderer::WaitForTasksClearSnapshotsAndDeleteSceneRenderer(FRHICommandListImmediate& RHICmdList, FSceneRenderer* SceneRenderer, bool bWaitForTasks)
{
	// we are about to destroy things that are being used for async tasks, so we wait here for them.
	if (bWaitForTasks)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_DeleteSceneRenderer_WaitForTasks);
		RHICmdList.ImmediateFlush(EImmediateFlushType::WaitForOutstandingTasksOnly);
	}

	// Wait for all dispatched shadow mesh draw tasks.
	for (int32 PassIndex = 0; PassIndex < SceneRenderer->DispatchedShadowDepthPasses.Num(); ++PassIndex)
	{
		SceneRenderer->DispatchedShadowDepthPasses[PassIndex]->WaitForTasksAndEmpty();
	}

	FViewInfo::DestroyAllSnapshots(); // this destroys viewinfo snapshots
	FSceneRenderTargets::GetGlobalUnsafe().DestroyAllSnapshots(); // this will destroy the render target snapshots
	static const IConsoleVariable* AsyncDispatch	= IConsoleManager::Get().FindConsoleVariable(TEXT("r.RHICmdAsyncRHIThreadDispatch"));

	if (AsyncDispatch->GetInt() == 0)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_DeleteSceneRenderer_Dispatch);
		RHICmdList.ImmediateFlush(EImmediateFlushType::WaitForDispatchToRHIThread); // we want to make sure this all gets to the rhi thread this frame and doesn't hang around
	}


	FMemMark* LocalRootMark = SceneRenderer->RootMark;
	SceneRenderer->RootMark = nullptr;
	// Delete the scene renderer.
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_DeleteSceneRenderer);
		delete SceneRenderer;
	}

	// Can relase only after all mesh pass tasks are finished.
	GPrimitiveIdVertexBufferPool.DiscardAll();
	FGraphicsMinimalPipelineStateId::ResetLocalPipelineIdTableSize();

	delete LocalRootMark;
}

class FClearSnapshotsAndDeleteSceneRendererTask
{
	FSceneRenderer* SceneRenderer;

public:

	FClearSnapshotsAndDeleteSceneRendererTask(FSceneRenderer* InSceneRenderer)
		: SceneRenderer(InSceneRenderer)
	{
	}

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FClearSnapshotsAndDeleteSceneRendererTask, STATGROUP_TaskGraphTasks);
	}

	ENamedThreads::Type GetDesiredThread()
	{
		return ENamedThreads::GetRenderThread_Local();
	}

	static ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::TrackSubsequents; }

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		FSceneRenderer::WaitForTasksClearSnapshotsAndDeleteSceneRenderer(FRHICommandListExecutor::GetImmediateCommandList(), SceneRenderer, false);
	}
};

void FSceneRenderer::DelayWaitForTasksClearSnapshotsAndDeleteSceneRenderer(FRHICommandListImmediate& RHICmdList, FSceneRenderer* SceneRenderer)
{
	FGraphEventArray& WaitOutstandingTasks = RHICmdList.GetRenderThreadTaskArray();
	FGraphEventRef ClearSnapshotsAndDeleteSceneRendererTask = TGraphTask<FClearSnapshotsAndDeleteSceneRendererTask>::CreateTask(&WaitOutstandingTasks, ENamedThreads::GetRenderThread()).ConstructAndDispatchWhenReady(SceneRenderer);

	WaitOutstandingTasks.Empty();
	WaitOutstandingTasks.Add(ClearSnapshotsAndDeleteSceneRendererTask);

	FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GetRenderThread_Local()); 
}

void FSceneRenderer::UpdatePrimitiveIndirectLightingCacheBuffers()
{
	// Use a bit array to prevent primitives from being updated more than once.
	FSceneBitArray UpdatedPrimitiveMap;
	UpdatedPrimitiveMap.Init(false, Scene->Primitives.Num());

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{		
		FViewInfo& View = Views[ViewIndex];

		for (int32 Index = 0; Index < View.DirtyIndirectLightingCacheBufferPrimitives.Num(); ++Index)
		{
			FPrimitiveSceneInfo* PrimitiveSceneInfo = View.DirtyIndirectLightingCacheBufferPrimitives[Index];

			FBitReference bInserted = UpdatedPrimitiveMap[PrimitiveSceneInfo->GetIndex()];
			if (!bInserted)
			{
				PrimitiveSceneInfo->UpdateIndirectLightingCacheBuffer();
				bInserted = true;
			}
			else
			{
				// This will prevent clearing it twice.
				View.DirtyIndirectLightingCacheBufferPrimitives[Index] = nullptr;
			}
		}
	}

	const uint32 CurrentSceneFrameNumber = Scene->GetFrameNumber();

	// Trim old CPUInterpolationCache entries occasionally
	if (CurrentSceneFrameNumber % 10 == 0)
	{
		for (TMap<FVector, FVolumetricLightmapInterpolation>::TIterator It(Scene->VolumetricLightmapSceneData.CPUInterpolationCache); It; ++It)
		{
			FVolumetricLightmapInterpolation& Interpolation = It.Value();

			if (Interpolation.LastUsedSceneFrameNumber < CurrentSceneFrameNumber - 100)
			{
				It.RemoveCurrent();
			}
		}
	}
}

/*-----------------------------------------------------------------------------
	FRendererModule
-----------------------------------------------------------------------------*/

/**
* Helper function performing actual work in render thread.
*
* @param SceneRenderer	Scene renderer to use for rendering.
*/
static void ViewExtensionPreRender_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneRenderer* SceneRenderer)
{
	FMemMark MemStackMark(FMemStack::Get());

	{
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(PreRender);
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_ViewExtensionPreRenderView);

		for (int ViewExt = 0; ViewExt < SceneRenderer->ViewFamily.ViewExtensions.Num(); ViewExt++)
		{
			SceneRenderer->ViewFamily.ViewExtensions[ViewExt]->PreRenderViewFamily_RenderThread(RHICmdList, SceneRenderer->ViewFamily);
			for (int ViewIndex = 0; ViewIndex < SceneRenderer->ViewFamily.Views.Num(); ViewIndex++)
			{
				SceneRenderer->ViewFamily.ViewExtensions[ViewExt]->PreRenderView_RenderThread(RHICmdList, SceneRenderer->Views[ViewIndex]);
			}
		}
	}
	
	// update any resources that needed a deferred update
	FDeferredUpdateResource::UpdateResources(RHICmdList);
}


static TAutoConsoleVariable<int32> CVarDelaySceneRenderCompletion(
	TEXT("r.DelaySceneRenderCompletion"),
	0,
	TEXT("Experimental option to postpone the cleanup of the scene renderer until later. This does NOT currently work because it is possible for the scene to be modified before ~FSceneRenderer, and that assumes the scene is unchanged."),
	ECVF_RenderThreadSafe
);

/**
 * Helper function performing actual work in render thread.
 *
 * @param SceneRenderer	Scene renderer to use for rendering.
 */
static void RenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneRenderer* SceneRenderer)
{
	LLM_SCOPE(ELLMTag::SceneRender);

	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_DelaySceneRenderCompletion_TaskWait);
		FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::WaitForOutstandingTasksOnly);
	}

	static const IConsoleVariable* AsyncDispatch = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RHICmdAsyncRHIThreadDispatch"));

#if WITH_EDITOR
	bool bDelayCleanup = false;
#else
	bool bDelayCleanup = !!AsyncDispatch->GetInt() && IsRunningRHIInSeparateThread() && !!CVarDelaySceneRenderCompletion.GetValueOnRenderThread();
#endif

	FMemMark* MemStackMark = new FMemMark(FMemStack::Get());

	if (bDelayCleanup)
	{
		SceneRenderer->RootMark = MemStackMark;
		MemStackMark = nullptr;
	}

	// update any resources that needed a deferred update
	FDeferredUpdateResource::UpdateResources(RHICmdList);

	if(SceneRenderer->ViewFamily.EngineShowFlags.OnScreenDebug)
	{
		GRenderTargetPool.SetEventRecordingActive(true);
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_TotalSceneRenderingTime);
	
		if(SceneRenderer->ViewFamily.EngineShowFlags.HitProxies)
		{
			// Render the scene's hit proxies.
			SceneRenderer->RenderHitProxies(RHICmdList);
		}
		else
		{
			// Render the scene.
			SceneRenderer->Render(RHICmdList);
		}

		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(PostRenderCleanUp);

		// Only reset per-frame scene state once all views have processed their frame, including those in planar reflections
		for (int32 CacheType = 0; CacheType < UE_ARRAY_COUNT(SceneRenderer->Scene->DistanceFieldSceneData.PrimitiveModifiedBounds); CacheType++)
		{
			SceneRenderer->Scene->DistanceFieldSceneData.PrimitiveModifiedBounds[CacheType].Reset();
		}

		// Immediately issue EndFrame() for all extensions in case any of the outstanding tasks they issued getting out of this frame
		extern TSet<IPersistentViewUniformBufferExtension*> PersistentViewUniformBufferExtensions;

		for (IPersistentViewUniformBufferExtension* Extension : PersistentViewUniformBufferExtensions)
		{
			Extension->EndFrame();
		}

#if STATS
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_RenderViewFamily_RenderThread_MemStats);

			// Update scene memory stats that couldn't be tracked continuously
			SET_MEMORY_STAT(STAT_RenderingSceneMemory, SceneRenderer->Scene->GetSizeBytes());

			SIZE_T ViewStateMemory = 0;
			for (int32 ViewIndex = 0; ViewIndex < SceneRenderer->Views.Num(); ViewIndex++)
			{
				if (SceneRenderer->Views[ViewIndex].State)
				{
					ViewStateMemory += SceneRenderer->Views[ViewIndex].State->GetSizeBytes();
				}
			}
			SET_MEMORY_STAT(STAT_ViewStateMemory, ViewStateMemory);
			SET_MEMORY_STAT(STAT_RenderingMemStackMemory, FMemStack::Get().GetByteCount());
			SET_MEMORY_STAT(STAT_LightInteractionMemory, FLightPrimitiveInteraction::GetMemoryPoolSize());
		}
#endif
		

		GRenderTargetPool.SetEventRecordingActive(false);

		if (bDelayCleanup)
		{
			check(!MemStackMark && SceneRenderer->RootMark);
			FSceneRenderer::DelayWaitForTasksClearSnapshotsAndDeleteSceneRenderer(RHICmdList, SceneRenderer);
		}
		else
		{
			FSceneRenderer::WaitForTasksClearSnapshotsAndDeleteSceneRenderer(RHICmdList, SceneRenderer);
		}
	}

#if STATS
	QUICK_SCOPE_CYCLE_COUNTER(STAT_RenderViewFamily_RenderThread_RHIGetGPUFrameCycles);
	if (FPlatformProperties::SupportsWindowedMode() == false)
	{
		/** Update STATS with the total GPU time taken to render the last frame. */
		SET_CYCLE_COUNTER(STAT_TotalGPUFrameTime, RHIGetGPUFrameCycles());
	}
#endif

	delete MemStackMark;
}

void OnChangeSimpleForwardShading(IConsoleVariable* Var)
{
	static const auto SupportSimpleForwardShadingCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.SupportSimpleForwardShading"));
	static const auto SimpleForwardShadingCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.SimpleForwardShading"));

	const bool bWasEnabled = CVarSimpleForwardShading_PreviousValue != 0;
	const bool bShouldBeEnabled = SimpleForwardShadingCVar->GetValueOnAnyThread() != 0;
	if( bWasEnabled != bShouldBeEnabled )
	{
	bool bWasIgnored = false;
	{
		if (SupportSimpleForwardShadingCVar->GetValueOnAnyThread() == 0)
		{
				if (bShouldBeEnabled)
				{
					UE_LOG( LogRenderer, Warning, TEXT( "r.SimpleForwardShading ignored as r.SupportSimpleForwardShading is not enabled" ) );
				}
			bWasIgnored = true;
		}
		else if (!PlatformSupportsSimpleForwardShading(GMaxRHIShaderPlatform))
		{
				if (bShouldBeEnabled)
				{
					UE_LOG( LogRenderer, Warning, TEXT( "r.SimpleForwardShading ignored, only supported on PC shader platforms.  Current shader platform %s" ), *LegacyShaderPlatformToShaderFormat( GMaxRHIShaderPlatform ).ToString() );
				}
			bWasIgnored = true;
		}
	}

	if( !bWasIgnored )
	{
		// Propagate cvar change to static draw lists
	FGlobalComponentRecreateRenderStateContext Context;
	}
	}

	CVarSimpleForwardShading_PreviousValue = SimpleForwardShadingCVar->GetValueOnAnyThread();
}

void OnChangeCVarRequiringRecreateRenderState(IConsoleVariable* Var)
{
	// Propgate cvar change to static draw lists
	FGlobalComponentRecreateRenderStateContext Context;
}

FRendererModule::FRendererModule()
{
	CVarSimpleForwardShading_PreviousValue = CVarSimpleForwardShading.AsVariable()->GetInt();
	CVarSimpleForwardShading.AsVariable()->SetOnChangedCallback(FConsoleVariableDelegate::CreateStatic(&OnChangeSimpleForwardShading));

	static auto EarlyZPassVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.EarlyZPass"));
	EarlyZPassVar->SetOnChangedCallback(FConsoleVariableDelegate::CreateStatic(&OnChangeCVarRequiringRecreateRenderState));

	static auto CVarVertexDeformationOutputsVelocity = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VertexDeformationOutputsVelocity"));
	CVarVertexDeformationOutputsVelocity->SetOnChangedCallback(FConsoleVariableDelegate::CreateStatic(&OnChangeCVarRequiringRecreateRenderState));

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	void InitDebugViewModeInterfaces();
	InitDebugViewModeInterfaces();
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
}

void FRendererModule::CreateAndInitSingleView(FRHICommandListImmediate& RHICmdList, class FSceneViewFamily* ViewFamily, const struct FSceneViewInitOptions* ViewInitOptions)
{
	// Create and add the new view
	FViewInfo* NewView = new FViewInfo(*ViewInitOptions);
	ViewFamily->Views.Add(NewView);
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FRHIRenderTargetView RTV(ViewFamily->RenderTarget->GetRenderTargetTexture(), ERenderTargetLoadAction::EClear);
	RHICmdList.SetRenderTargets(1, &RTV, nullptr);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	FViewInfo* View = (FViewInfo*)ViewFamily->Views[0];
	View->ViewRect = View->UnscaledViewRect;
	View->InitRHIResources();
}

void FRendererModule::BeginRenderingViewFamily(FCanvas* Canvas, FSceneViewFamily* ViewFamily)
{
	check(Canvas);
	check(ViewFamily);
	check(ViewFamily->Scene);
	check(ViewFamily->GetScreenPercentageInterface());

	UWorld* World = nullptr;

	FScene* const Scene = ViewFamily->Scene->GetRenderScene();
	if (Scene)
	{
		World = Scene->GetWorld();
		if (World)
		{
			//guarantee that all render proxies are up to date before kicking off a BeginRenderViewFamily.
			World->SendAllEndOfFrameUpdates();
		}
	}

	ENQUEUE_RENDER_COMMAND(UpdateDeferredCachedUniformExpressions)(
		[](FRHICommandList& RHICmdList)
		{
			FMaterialRenderProxy::UpdateDeferredCachedUniformExpressions();
		});

	ENQUEUE_RENDER_COMMAND(UpdateFastVRamConfig)(
		[](FRHICommandList& RHICmdList)
		{
			GFastVRamConfig.Update();
		});

	// Flush the canvas first.
	Canvas->Flush_GameThread();

	if (Scene)
	{
		// We allow caching of per-frame, per-scene data
		Scene->IncrementFrameNumber();
		ViewFamily->FrameNumber = Scene->GetFrameNumber();
	}
	else
	{
		// this is passes to the render thread, better access that than GFrameNumberRenderThread
		ViewFamily->FrameNumber = GFrameNumber;
	}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	{
		extern TSharedRef<ISceneViewExtension, ESPMode::ThreadSafe> GetRendererViewExtension();

		ViewFamily->ViewExtensions.Add(GetRendererViewExtension());
	}
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

	for (int ViewExt = 0; ViewExt < ViewFamily->ViewExtensions.Num(); ViewExt++)
	{
		ViewFamily->ViewExtensions[ViewExt]->BeginRenderViewFamily(*ViewFamily);
	}

	if (Scene)
	{		
		// Set the world's "needs full lighting rebuild" flag if the scene has any uncached static lighting interactions.
		if(World)
		{
			// Note: reading NumUncachedStaticLightingInteractions on the game thread here which is written to by the rendering thread
			// This is reliable because the RT uses interlocked mechanisms to update it
			World->SetMapNeedsLightingFullyRebuilt(Scene->NumUncachedStaticLightingInteractions, Scene->NumUnbuiltReflectionCaptures);
		}

		// Construct the scene renderer.  This copies the view family attributes into its own structures.
		FSceneRenderer* SceneRenderer = FSceneRenderer::CreateSceneRenderer(ViewFamily, Canvas->GetHitProxyConsumer());

		if (!SceneRenderer->ViewFamily.EngineShowFlags.HitProxies)
		{
			USceneCaptureComponent::UpdateDeferredCaptures(Scene);
		}

		// We need to execute the pre-render view extensions before we do any view dependent work.
		// Anything between here and FDrawSceneCommand will add to HMD view latency
		ENQUEUE_RENDER_COMMAND(FViewExtensionPreDrawCommand)(
			[SceneRenderer](FRHICommandListImmediate& RHICmdList)
			{
				ViewExtensionPreRender_RenderThread(RHICmdList, SceneRenderer);
			});

		if (!SceneRenderer->ViewFamily.EngineShowFlags.HitProxies)
		{
			for (int32 ReflectionIndex = 0; ReflectionIndex < SceneRenderer->Scene->PlanarReflections_GameThread.Num(); ReflectionIndex++)
			{
				UPlanarReflectionComponent* ReflectionComponent = SceneRenderer->Scene->PlanarReflections_GameThread[ReflectionIndex];
				SceneRenderer->Scene->UpdatePlanarReflectionContents(ReflectionComponent, *SceneRenderer);
			}
		}

		SceneRenderer->ViewFamily.DisplayInternalsData.Setup(World);

		ENQUEUE_RENDER_COMMAND(FDrawSceneCommand)(
			[SceneRenderer](FRHICommandListImmediate& RHICmdList)
			{
				RenderViewFamily_RenderThread(RHICmdList, SceneRenderer);
				FlushPendingDeleteRHIResources_RenderThread();
			});
	}
}

void FRendererModule::PostRenderAllViewports()
{
	// Increment FrameNumber before render the scene. Wrapping around is no problem.
	// This is the only spot we change GFrameNumber, other places can only read.
	++GFrameNumber;
}

void FRendererModule::PerFrameCleanupIfSkipRenderer()
{
	// Some systems (e.g. Slate) can still draw (via FRendererModule::DrawTileMesh for example) when scene renderer is not used
	ENQUEUE_RENDER_COMMAND(CmdPerFrameCleanupIfSkipRenderer)(
		[](FRHICommandListImmediate&)
	{
		GPrimitiveIdVertexBufferPool.DiscardAll();
	});
}

void FRendererModule::UpdateMapNeedsLightingFullyRebuiltState(UWorld* World)
{
	World->SetMapNeedsLightingFullyRebuilt(World->Scene->GetRenderScene()->NumUncachedStaticLightingInteractions, World->Scene->GetRenderScene()->NumUnbuiltReflectionCaptures);
}

void FRendererModule::DrawRectangle(
		FRHICommandList& RHICmdList,
		float X,
		float Y,
		float SizeX,
		float SizeY,
		float U,
		float V,
		float SizeU,
		float SizeV,
		FIntPoint TargetSize,
		FIntPoint TextureSize,
		const TShaderRef<FShader>& VertexShader,
		EDrawRectangleFlags Flags
		)
{
	::DrawRectangle( RHICmdList, X, Y, SizeX, SizeY, U, V, SizeU, SizeV, TargetSize, TextureSize, VertexShader, Flags );
}

void FRendererModule::RegisterPostOpaqueRenderDelegate(const FPostOpaqueRenderDelegate& InPostOpaqueRenderDelegate)
{
	this->PostOpaqueRenderDelegate = InPostOpaqueRenderDelegate;
}

void FRendererModule::RegisterOverlayRenderDelegate(const FPostOpaqueRenderDelegate& InOverlayRenderDelegate)
{
	this->OverlayRenderDelegate = InOverlayRenderDelegate;
}

void FRendererModule::RenderPostOpaqueExtensions(const FViewInfo& View, FRHICommandListImmediate& RHICmdList, FSceneRenderTargets& SceneContext, TUniformBufferRef<FSceneTexturesUniformParameters>& SceneTextureUniformParams)
{
	check(IsInRenderingThread());
	FPostOpaqueRenderParameters RenderParameters;
	RenderParameters.ViewMatrix = View.ViewMatrices.GetViewMatrix();
	RenderParameters.ProjMatrix = View.ViewMatrices.GetProjectionMatrix();
	RenderParameters.DepthTexture = SceneContext.GetSceneDepthSurface()->GetTexture2D();
	RenderParameters.NormalTexture = SceneContext.GBufferA.IsValid() ? SceneContext.GetGBufferATexture() : nullptr;
	RenderParameters.SmallDepthTexture = SceneContext.GetSmallDepthSurface()->GetTexture2D();
	RenderParameters.ViewUniformBuffer = View.ViewUniformBuffer;
	RenderParameters.SceneTexturesUniformParams = SceneTextureUniformParams;
	RenderParameters.GlobalDistanceFieldParams = &View.GlobalDistanceFieldInfo.ParameterData;

	RenderParameters.ViewportRect = View.ViewRect;
	RenderParameters.RHICmdList = &RHICmdList;

	RenderParameters.Uid = (void*)(&View);
	PostOpaqueRenderDelegate.ExecuteIfBound(RenderParameters);
}

void FRendererModule::RenderOverlayExtensions(const FViewInfo& View, FRHICommandListImmediate& RHICmdList, FSceneRenderTargets& SceneContext)
{
	check(IsInRenderingThread());
	FPostOpaqueRenderParameters RenderParameters;
	RenderParameters.ViewMatrix = View.ViewMatrices.GetViewMatrix();
	RenderParameters.ProjMatrix = View.ViewMatrices.GetProjectionMatrix();
	RenderParameters.DepthTexture = SceneContext.GetSceneDepthSurface()->GetTexture2D();
	RenderParameters.SmallDepthTexture = SceneContext.GetSmallDepthSurface()->GetTexture2D();

	RenderParameters.ViewportRect = View.ViewRect;
	RenderParameters.RHICmdList = &RHICmdList;

	RenderParameters.Uid=(void*)(&View);
	OverlayRenderDelegate.ExecuteIfBound(RenderParameters);
}

void FRendererModule::RenderPostResolvedSceneColorExtension(FRHICommandListImmediate& RHICmdList, class FSceneRenderTargets& SceneContext)
{
	PostResolvedSceneColorCallbacks.Broadcast(RHICmdList, SceneContext);
}

IAllocatedVirtualTexture* FRendererModule::AllocateVirtualTexture(const FAllocatedVTDescription& Desc)
{
	return FVirtualTextureSystem::Get().AllocateVirtualTexture(Desc);
}

void FRendererModule::DestroyVirtualTexture(IAllocatedVirtualTexture* AllocatedVT)
{
	FVirtualTextureSystem::Get().DestroyVirtualTexture(AllocatedVT);
}

FVirtualTextureProducerHandle FRendererModule::RegisterVirtualTextureProducer(const FVTProducerDescription& Desc, IVirtualTexture* Producer)
{
	return FVirtualTextureSystem::Get().RegisterProducer(Desc, Producer);
}

void FRendererModule::ReleaseVirtualTextureProducer(const FVirtualTextureProducerHandle& Handle)
{
	FVirtualTextureSystem::Get().ReleaseProducer(Handle);
}

void FRendererModule::AddVirtualTextureProducerDestroyedCallback(const FVirtualTextureProducerHandle& Handle, FVTProducerDestroyedFunction* Function, void* Baton)
{
	FVirtualTextureSystem::Get().AddProducerDestroyedCallback(Handle, Function, Baton);
}

uint32 FRendererModule::RemoveAllVirtualTextureProducerDestroyedCallbacks(const void* Baton)
{
	return FVirtualTextureSystem::Get().RemoveAllProducerDestroyedCallbacks(Baton);
}

void FRendererModule::RequestVirtualTextureTiles(const FVector2D& InScreenSpaceSize, int32 InMipLevel)
{
	FVirtualTextureSystem::Get().RequestTiles(InScreenSpaceSize, InMipLevel);
}

void FRendererModule::RequestVirtualTextureTilesForRegion(IAllocatedVirtualTexture* AllocatedVT, const FVector2D& InScreenSpaceSize, const FIntRect& InTextureRegion, int32 InMipLevel)
{
	FVirtualTextureSystem::Get().RequestTilesForRegion(AllocatedVT, InScreenSpaceSize, InTextureRegion, InMipLevel);
}

void FRendererModule::LoadPendingVirtualTextureTiles(FRHICommandListImmediate& RHICmdList, ERHIFeatureLevel::Type FeatureLevel)
{
	FVirtualTextureSystem::Get().LoadPendingTiles(RHICmdList, FeatureLevel);
}

void FRendererModule::FlushVirtualTextureCache()
{
	FVirtualTextureSystem::Get().FlushCache();
}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

class FConsoleVariableAutoCompleteVisitor 
{
public:
	// @param Name must not be 0
	// @param CVar must not be 0
	static void OnConsoleVariable(const TCHAR *Name, IConsoleObject* CObj, uint32& Crc)
	{
		IConsoleVariable* CVar = CObj->AsVariable();
		if(CVar)
		{
			if(CObj->TestFlags(ECVF_Scalability) || CObj->TestFlags(ECVF_ScalabilityGroup))
			{
				// float should work on int32 as well
				float Value = CVar->GetFloat();
				Crc = FCrc::MemCrc32(&Value, sizeof(Value), Crc);
			}
		}
	}
};
static uint32 ComputeScalabilityCVarHash()
{
	uint32 Ret = 0;

	IConsoleManager::Get().ForEachConsoleObjectThatStartsWith(FConsoleObjectVisitor::CreateStatic< uint32& >(&FConsoleVariableAutoCompleteVisitor::OnConsoleVariable, Ret));

	return Ret;
}

static void DisplayInternals(FRHICommandListImmediate& RHICmdList, FViewInfo& InView)
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	auto Family = InView.Family;
	// if r.DisplayInternals != 0
	if(Family->EngineShowFlags.OnScreenDebug && Family->DisplayInternalsData.IsValid())
	{
		// could be 0
		auto State = InView.ViewState;

		FCanvas Canvas((FRenderTarget*)Family->RenderTarget, NULL, Family->CurrentRealTime, Family->CurrentWorldTime, Family->DeltaWorldTime, InView.GetFeatureLevel());
		Canvas.SetRenderTargetRect(FIntRect(0, 0, Family->RenderTarget->GetSizeXY().X, Family->RenderTarget->GetSizeXY().Y));


		FRHIRenderPassInfo RenderPassInfo(Family->RenderTarget->GetRenderTargetTexture(), ERenderTargetActions::Load_Store);
		RHICmdList.BeginRenderPass(RenderPassInfo, TEXT("DisplayInternalsRenderPass"));

		// further down to not intersect with "LIGHTING NEEDS TO BE REBUILT"
		FVector2D Pos(30, 140);
		const int32 FontSizeY = 14;

		// dark background
		const uint32 BackgroundHeight = 30;
		Canvas.DrawTile(Pos.X - 4, Pos.Y - 4, 500 + 8, FontSizeY * BackgroundHeight + 8, 0, 0, 1, 1, FLinearColor(0,0,0,0.6f), 0, true);

		UFont* Font = GEngine->GetSmallFont();
		FCanvasTextItem SmallTextItem( Pos, FText::GetEmpty(), GEngine->GetSmallFont(), FLinearColor::White );

		SmallTextItem.SetColor(FLinearColor::White);
		SmallTextItem.Text = FText::FromString(FString::Printf(TEXT("r.DisplayInternals = %d"), Family->DisplayInternalsData.DisplayInternalsCVarValue));
		Canvas.DrawItem(SmallTextItem, Pos);
		SmallTextItem.SetColor(FLinearColor::Gray);
		Pos.Y += 2 * FontSizeY;

		FViewInfo& ViewInfo = (FViewInfo&)InView;
#define CANVAS_HEADER(txt) \
		{ \
			SmallTextItem.SetColor(FLinearColor::Gray); \
			SmallTextItem.Text = FText::FromString(txt); \
			Canvas.DrawItem(SmallTextItem, Pos); \
			Pos.Y += FontSizeY; \
		}
#define CANVAS_LINE(bHighlight, txt, ... ) \
		{ \
			SmallTextItem.SetColor(bHighlight ? FLinearColor::Red : FLinearColor::Gray); \
			SmallTextItem.Text = FText::FromString(FString::Printf(txt, __VA_ARGS__)); \
			Canvas.DrawItem(SmallTextItem, Pos); \
			Pos.Y += FontSizeY; \
		}

		CANVAS_HEADER(TEXT("command line options:"))
		{
			bool bHighlight = !(FApp::UseFixedTimeStep() && FApp::bUseFixedSeed);
			CANVAS_LINE(bHighlight, TEXT("  -UseFixedTimeStep: %u"), FApp::UseFixedTimeStep())
			CANVAS_LINE(bHighlight, TEXT("  -FixedSeed: %u"), FApp::bUseFixedSeed)
			CANVAS_LINE(false, TEXT("  -gABC= (changelist): %d"), GetChangeListNumberForPerfTesting())
		}

		CANVAS_HEADER(TEXT("Global:"))
		CANVAS_LINE(false, TEXT("  FrameNumberRT: %u"), GFrameNumberRenderThread)
		CANVAS_LINE(false, TEXT("  Scalability CVar Hash: %x (use console command \"Scalability\")"), ComputeScalabilityCVarHash())
		//not really useful as it is non deterministic and should not be used for rendering features:  CANVAS_LINE(false, TEXT("  FrameNumberRT: %u"), GFrameNumberRenderThread)
		CANVAS_LINE(false, TEXT("  FrameCounter: %llu"), (uint64)GFrameCounter)
		CANVAS_LINE(false, TEXT("  rand()/SRand: %x/%x"), FMath::Rand(), FMath::GetRandSeed())
		{
			bool bHighlight = Family->DisplayInternalsData.NumPendingStreamingRequests != 0;
			CANVAS_LINE(bHighlight, TEXT("  FStreamAllResourcesLatentCommand: %d"), bHighlight)
		}
		{
			static auto* Var = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Streaming.FramesForFullUpdate"));
			int32 Value = Var->GetValueOnRenderThread();
			bool bHighlight = Value != 0;
			CANVAS_LINE(bHighlight, TEXT("  r.Streaming.FramesForFullUpdate: %u%s"), Value, bHighlight ? TEXT(" (should be 0)") : TEXT(""));
		}

		if(State)
		{
			CANVAS_HEADER(TEXT("State:"))
			CANVAS_LINE(false, TEXT("  TemporalAASample: %u"), State->GetCurrentTemporalAASampleIndex())
			CANVAS_LINE(false, TEXT("  FrameIndexMod8: %u"), State->GetFrameIndex(8))
			CANVAS_LINE(false, TEXT("  LODTransition: %.2f"), State->GetTemporalLODTransition())
		}

		CANVAS_HEADER(TEXT("Family:"))
		CANVAS_LINE(false, TEXT("  Time (Real/World/DeltaWorld): %.2f/%.2f/%.2f"), Family->CurrentRealTime, Family->CurrentWorldTime, Family->DeltaWorldTime)
		CANVAS_LINE(false, TEXT("  MatineeTime: %f"), Family->DisplayInternalsData.MatineeTime)
		CANVAS_LINE(false, TEXT("  FrameNumber: %u"), Family->FrameNumber)
		CANVAS_LINE(false, TEXT("  ExposureSettings: %s"), *Family->ExposureSettings.ToString())
		CANVAS_LINE(false, TEXT("  GammaCorrection: %.2f"), Family->GammaCorrection)

		CANVAS_HEADER(TEXT("View:"))
		CANVAS_LINE(false, TEXT("  TemporalJitter: %.2f/%.2f"), ViewInfo.TemporalJitterPixels.X, ViewInfo.TemporalJitterPixels.Y)
		CANVAS_LINE(false, TEXT("  ViewProjectionMatrix Hash: %x"), InView.ViewMatrices.GetViewProjectionMatrix().ComputeHash())
		CANVAS_LINE(false, TEXT("  ViewLocation: %s"), *InView.ViewLocation.ToString())
		CANVAS_LINE(false, TEXT("  ViewRotation: %s"), *InView.ViewRotation.ToString())
		CANVAS_LINE(false, TEXT("  ViewRect: %s"), *ViewInfo.ViewRect.ToString())

		CANVAS_LINE(false, TEXT("  DynMeshElements/TranslPrim: %d/%d"), ViewInfo.DynamicMeshElements.Num(), ViewInfo.TranslucentPrimCount.NumPrims())

#undef CANVAS_LINE
#undef CANVAS_HEADER

		RHICmdList.EndRenderPass();
		Canvas.Flush_RenderThread(RHICmdList);
	}
#endif

}

TSharedRef<ISceneViewExtension, ESPMode::ThreadSafe> GetRendererViewExtension()
{
	class FRendererViewExtension : public ISceneViewExtension
	{
	public:
		virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) {}
		virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) {}
		virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) {}
		virtual void PreRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily) {}
		virtual void PreRenderView_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView) {}
		virtual int32 GetPriority() const { return 0; }
		virtual void PostRenderView_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView)
		{
			FViewInfo& View = static_cast<FViewInfo&>(InView);
			DisplayInternals(RHICmdList, View);
		}
	};
	TSharedRef<FRendererViewExtension, ESPMode::ThreadSafe> ref(new FRendererViewExtension);
	return StaticCastSharedRef<ISceneViewExtension>(ref);
}

#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

/**
* Saves a previously rendered scene color target
*/

class FDummySceneColorResolveBuffer : public FVertexBuffer
{
public:
	virtual void InitRHI() override
	{
		const int32 NumDummyVerts = 3;
		const uint32 Size = sizeof(FVector4) * NumDummyVerts;
		FRHIResourceCreateInfo CreateInfo;
		void* BufferData = nullptr;
		VertexBufferRHI = RHICreateAndLockVertexBuffer(Size, BUF_Static, CreateInfo, BufferData);
		FMemory::Memset(BufferData, 0, Size);		
		RHIUnlockVertexBuffer(VertexBufferRHI);		
	}
};

TGlobalResource<FDummySceneColorResolveBuffer> GResolveDummyVertexBuffer;
extern int32 GAllowCustomMSAAResolves;

void FSceneRenderer::ResolveSceneColor(FRHICommandList& RHICmdList)
{
	SCOPED_DRAW_EVENT(RHICmdList, ResolveSceneColor);
	check(FamilySize.X);

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	auto& SceneColorRT = SceneContext.GetSceneColor();
	auto& SceneColor = SceneColorRT->GetRenderTargetItem();
	uint32 CurrentNumSamples = SceneColorRT->GetDesc().NumSamples;

	const EShaderPlatform CurrentShaderPlatform = GShaderPlatformForFeatureLevel[SceneContext.GetCurrentFeatureLevel()];
	if (CurrentNumSamples <= 1 || !RHISupportsSeparateMSAAAndResolveTextures(CurrentShaderPlatform) || !GAllowCustomMSAAResolves)
	{
		RHICmdList.CopyToResolveTarget(SceneColor.TargetableTexture, SceneColor.ShaderResourceTexture, FResolveRect(0, 0, FamilySize.X, FamilySize.Y));
	}
	else
	{
		RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, SceneColor.ShaderResourceTexture);
		RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, SceneColor.TargetableTexture);

		// Custom shader based color resolve for HDR color to emulate mobile.
		FRHIRenderPassInfo RPInfo(SceneColor.ShaderResourceTexture, ERenderTargetActions::Load_Store);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("ResolveColor"));
		{
			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
			{
				const FViewInfo& View = Views[ViewIndex];

				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

				GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
				GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

				// Resolve views individually
				// In the case of adaptive resolution, the view family will be much larger than the views individually
				RHICmdList.SetScissorRect(true, View.ViewRect.Min.X, View.ViewRect.Min.Y, View.ViewRect.Max.X, View.ViewRect.Max.Y);

				int32 ResolveWidth = CVarWideCustomResolve.GetValueOnRenderThread();

				if (CurrentNumSamples <= 1)
				{
					ResolveWidth = 0;
				}

				if (ResolveWidth != 0)
				{
					ResolveFilterWide(RHICmdList, GraphicsPSOInit, SceneContext.GetCurrentFeatureLevel(), SceneColor.TargetableTexture, SceneColor.FmaskSRV, FIntPoint(0, 0), CurrentNumSamples, ResolveWidth, GResolveDummyVertexBuffer.VertexBufferRHI);
				}
				else
				{
					auto ShaderMap = GetGlobalShaderMap(SceneContext.GetCurrentFeatureLevel());
					TShaderMapRef<FHdrCustomResolveVS> VertexShader(ShaderMap);
					GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector4();
					GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
					GraphicsPSOInit.PrimitiveType = PT_TriangleList;

					if (SceneColor.FmaskSRV)
					{
						if (CurrentNumSamples == 2)
						{
							TShaderMapRef<FHdrCustomResolveFMask2xPS> PixelShader(ShaderMap);
							GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

							SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
							PixelShader->SetParameters(RHICmdList, SceneColor.TargetableTexture, SceneColor.FmaskSRV);
						}
						else if (CurrentNumSamples == 4)
						{
							TShaderMapRef<FHdrCustomResolveFMask4xPS> PixelShader(ShaderMap);
							GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

							SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
							PixelShader->SetParameters(RHICmdList, SceneColor.TargetableTexture, SceneColor.FmaskSRV);
						}
						else if (CurrentNumSamples == 8)
						{
							TShaderMapRef<FHdrCustomResolveFMask8xPS> PixelShader(ShaderMap);
							GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

							SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
							PixelShader->SetParameters(RHICmdList, SceneColor.TargetableTexture, SceneColor.FmaskSRV);
						}
						else
						{
							// Everything other than 2,4,8 samples is not implemented.
							check(0);
							break;
						}
					}
					else
					{
						if (CurrentNumSamples == 2)
						{
							TShaderMapRef<FHdrCustomResolve2xPS> PixelShader(ShaderMap);
							GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

							SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
							PixelShader->SetParameters(RHICmdList, SceneColor.TargetableTexture);
						}
						else if (CurrentNumSamples == 4)
						{
							TShaderMapRef<FHdrCustomResolve4xPS> PixelShader(ShaderMap);
							GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

							SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
							PixelShader->SetParameters(RHICmdList, SceneColor.TargetableTexture);
						}
						else if (CurrentNumSamples == 8)
						{
							TShaderMapRef<FHdrCustomResolve8xPS> PixelShader(ShaderMap);
							GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

							SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
							PixelShader->SetParameters(RHICmdList, SceneColor.TargetableTexture);
						}
						else
						{
							// Everything other than 2,4,8 samples is not implemented.
							check(0);
							break;
						}
					}

					RHICmdList.SetStreamSource(0, GResolveDummyVertexBuffer.VertexBufferRHI, 0);

					RHICmdList.DrawPrimitive(0, 1, 1);
				}
			}

			RHICmdList.SetScissorRect(false, 0, 0, 0, 0);
		}

		RHICmdList.EndRenderPass();

		// The destination texture must be made readable after the resolve.
		RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, SceneContext.GetSceneColorTexture());
	}
}

FRHITexture* FSceneRenderer::GetMultiViewSceneColor(const FSceneRenderTargets& SceneContext) const
{
	const FViewInfo& View = Views[0];

	if (View.bIsMobileMultiViewEnabled && !View.bIsMobileMultiViewDirectEnabled)
	{
		return SceneContext.MobileMultiViewSceneColor->GetRenderTargetItem().TargetableTexture;
	}
	else
	{
		return static_cast<FTextureRHIRef>(ViewFamily.RenderTarget->GetRenderTargetTexture());
	}
}

void RunGPUSkinCacheTransition(FRHICommandList& RHICmdList, FScene* Scene, EGPUSkinCacheTransition Type)
{
	// * When hair strands is disabled, the skin cache sync point run later 
	//   during the deferred render pass
	// * When hair strands is enabled, the skin cache sync point is run earlier, during 
	//   the init views pass, as the output of the skin cached is used by Niagara
	const bool bHairStrandsEnabled = IsHairStrandsEnable(Scene->GetShaderPlatform());
	const bool bRun = 
		(bHairStrandsEnabled && EGPUSkinCacheTransition::FrameSetup == Type) || 
		(!bHairStrandsEnabled && EGPUSkinCacheTransition::FrameSetup != Type);
	if (bRun)
	{
		if (FGPUSkinCache* GPUSkinCache = Scene->GetGPUSkinCache())
		{
			GPUSkinCache->TransitionAllToReadable(RHICmdList);
		}
	}
}
