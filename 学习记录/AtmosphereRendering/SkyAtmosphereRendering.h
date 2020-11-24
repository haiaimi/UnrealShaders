// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SkyAtmosphereRendering.h
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "EngineDefines.h"
#include "RendererInterface.h"
#include "RenderResource.h"
#include "Rendering/SkyAtmosphereCommonData.h"


class FScene;
class FViewInfo;
class FLightSceneInfo;
class USkyAtmosphereComponent;
class FSkyAtmosphereSceneProxy;

struct FEngineShowFlags;


// Use as a global shader parameter struct and also the CPU structure representing the atmosphere it self.
// This is static for a version of a component. When a component is changed/tweaked, it is recreated.
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FAtmosphereUniformShaderParameters, )
	SHADER_PARAMETER(float, MultiScatteringFactor)
	SHADER_PARAMETER(float, BottomRadiusKm)
	SHADER_PARAMETER(float, TopRadiusKm)
	SHADER_PARAMETER(float, RayleighDensityExpScale)
	SHADER_PARAMETER(FLinearColor, RayleighScattering)
	SHADER_PARAMETER(FLinearColor, MieScattering)
	SHADER_PARAMETER(float, MieDensityExpScale)
	SHADER_PARAMETER(FLinearColor, MieExtinction)
	SHADER_PARAMETER(float, MiePhaseG)
	SHADER_PARAMETER(FLinearColor, MieAbsorption)
	SHADER_PARAMETER(float, AbsorptionDensity0LayerWidth)
	SHADER_PARAMETER(float, AbsorptionDensity0ConstantTerm)
	SHADER_PARAMETER(float, AbsorptionDensity0LinearTerm)
	SHADER_PARAMETER(float, AbsorptionDensity1ConstantTerm)
	SHADER_PARAMETER(float, AbsorptionDensity1LinearTerm)
	SHADER_PARAMETER(FLinearColor, AbsorptionExtinction)
	SHADER_PARAMETER(FLinearColor, GroundAlbedo)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

//@StarLight code - BEGIN Precomputed Multi Scattering on mobile, edit by wanghai
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FPrecomputedAtmosphereUniformShaderParameters, )
SHADER_PARAMETER(FIntVector4, ScatteringTextureSize)
SHADER_PARAMETER(FIntPoint, TransmittanceLutSize)
SHADER_PARAMETER(FIntPoint, IrradianceLutSize)
SHADER_PARAMETER(float, LightZenithCosMin)
SHADER_PARAMETER(float, LightAngularRadius)
END_GLOBAL_SHADER_PARAMETER_STRUCT()
//@StarLight code - END Precomputed Multi Scattering on mobile, edit by wanghai

// These parameters are shared on the view global uniform buffer and are dynamically changed with cvars.
struct FSkyAtmosphereViewSharedUniformShaderParameters
{
	float AerialPerspectiveStartDepthKm;
	float CameraAerialPerspectiveVolumeDepthResolution;
	float CameraAerialPerspectiveVolumeDepthResolutionInv;
	float CameraAerialPerspectiveVolumeDepthSliceLengthKm;
	float CameraAerialPerspectiveVolumeDepthSliceLengthKmInv;
	float ApplyCameraAerialPerspectiveVolume;
};



class FSkyAtmosphereRenderSceneInfo
{
public:

	/** Initialization constructor. */
	explicit FSkyAtmosphereRenderSceneInfo(FSkyAtmosphereSceneProxy& SkyAtmosphereSceneProxy);
	~FSkyAtmosphereRenderSceneInfo();

	const TUniformBufferRef<FAtmosphereUniformShaderParameters>& GetAtmosphereUniformBuffer() { return AtmosphereUniformBuffer; }
	TRefCountPtr<IPooledRenderTarget>& GetTransmittanceLutTexture() { return TransmittanceLutTexture; }
	TRefCountPtr<IPooledRenderTarget>& GetMultiScatteredLuminanceLutTexture() { return MultiScatteredLuminanceLutTexture; }
	TRefCountPtr<IPooledRenderTarget>& GetDistantSkyLightLutTexture();

	//@StarLight code - BEGIN Precomputed Multi Scattering on mobile, edit by wanghai
	TRefCountPtr<IPooledRenderTarget>& GetRayleighScatteringLutTexture() { return RayleighScatteringLutTexture; }
	TRefCountPtr<IPooledRenderTarget>& GetMieScatteringLutTexture() { return MieScatteringLutTexture; }
	TRefCountPtr<IPooledRenderTarget>& GetMultiScatteringLutTexture() { return MultiScatteringLutTexture; }
	TRefCountPtr<IPooledRenderTarget>& GetMultiScatteringLutTextureSwapA() { return MultiScatteringLutTextureA; }
	TRefCountPtr<IPooledRenderTarget>& GetMultiScatteringLutTextureSwapB() { return MultiScatteringLutTextureB; }
	TRefCountPtr<IPooledRenderTarget>& GetIntermediateMultiScatteringLutTexture() { return IntermediateMultiScatteringLutTexture; }
	TRefCountPtr<IPooledRenderTarget>& GetIrradianceLutTextureSwapA() { return IrradianceLutTextureA; }
	TRefCountPtr<IPooledRenderTarget>& GetIrradianceLutTextureSwapB() { return IrradianceLutTextureB; }
	TRefCountPtr<IPooledRenderTarget>& GetIrradianceLutTexture() { return IrradianceLutTexture; }
	TRefCountPtr<IPooledRenderTarget>& GetIntermediateIrradianceLutTexture() { return IntermediateIrradianceLutTexture; }
	TRefCountPtr<IPooledRenderTarget>& GetScatteringDensityLutTexture() { return ScatteringDensityLutTexture; }
	TRefCountPtr<IPooledRenderTarget>& GetSkyAtmosphereViewLutTexture() { return SkyAtmosphereViewLutTexture; }
	TRefCountPtr<IPooledRenderTarget>& GetStaticLightScatteringLutTexture() { return StaticLightScatteringLutTexture; }

	TRefCountPtr<IPooledRenderTarget>& GetScatteringAltasTexture() { return ScatteringAltasTexture; }
	FTextureRHIRef GetPrecomputedScatteringLut() { return PrecomputedScatteringLut; }
	FTextureRHIRef GetPrecomputedTranmisttanceLut() { return PrecomputedTranmisttanceLut; }
	FTextureRHIRef GetPrecomputedIrradianceLut() { return PrecomputedIrradianceLut; }
	//@StarLight code - END Precomputed Multi Scattering on mobile, edit by wanghai

	const FAtmosphereUniformShaderParameters* GetAtmosphereShaderParameters() const { return &AtmosphereUniformShaderParameters; }
	const FSkyAtmosphereSceneProxy& GetSkyAtmosphereSceneProxy() const { return SkyAtmosphereSceneProxy; }

private:

	FSkyAtmosphereSceneProxy& SkyAtmosphereSceneProxy;

	FAtmosphereUniformShaderParameters AtmosphereUniformShaderParameters;

	TUniformBufferRef<FAtmosphereUniformShaderParameters> AtmosphereUniformBuffer;

	TRefCountPtr<IPooledRenderTarget> TransmittanceLutTexture;
	TRefCountPtr<IPooledRenderTarget> MultiScatteredLuminanceLutTexture;
	TRefCountPtr<IPooledRenderTarget> DistantSkyLightLutTexture;

	//@StarLight code - BEGIN Precomputed Multi Scattering on mobile, edit by wanghai
	TRefCountPtr<IPooledRenderTarget> RayleighScatteringLutTexture;
	TRefCountPtr<IPooledRenderTarget> MieScatteringLutTexture;
	TRefCountPtr<IPooledRenderTarget> MultiScatteringLutTexture;
	TRefCountPtr<IPooledRenderTarget> MultiScatteringLutTextureA;
	TRefCountPtr<IPooledRenderTarget> MultiScatteringLutTextureB;
	TRefCountPtr<IPooledRenderTarget> IntermediateMultiScatteringLutTexture;
	TRefCountPtr<IPooledRenderTarget> IrradianceLutTextureA;
	TRefCountPtr<IPooledRenderTarget> IrradianceLutTextureB;
	TRefCountPtr<IPooledRenderTarget> IrradianceLutTexture;
	TRefCountPtr<IPooledRenderTarget> IntermediateIrradianceLutTexture;
	TRefCountPtr<IPooledRenderTarget> ScatteringDensityLutTexture;
	TRefCountPtr<IPooledRenderTarget> SkyAtmosphereViewLutTexture;
	TRefCountPtr<IPooledRenderTarget> ScatteringAltasTexture;
	TRefCountPtr<IPooledRenderTarget> StaticLightScatteringLutTexture;

	FTextureRHIRef PrecomputedScatteringLut;
	FTextureRHIRef PrecomputedTranmisttanceLut;
	FTextureRHIRef PrecomputedIrradianceLut;
	//@StarLight code - END Precomputed Multi Scattering on mobile, edit by wanghai
};



bool ShouldRenderSkyAtmosphere(const FScene* Scene, const FEngineShowFlags& EngineShowFlags);
bool ShouldApplyAtmosphereLightPerPixelTransmittance(const FScene* Scene, const FEngineShowFlags& EngineShowFlags);

void InitSkyAtmosphereForScene(FRHICommandListImmediate& RHICmdList, FScene* Scene);
void InitSkyAtmosphereForView(FRHICommandListImmediate& RHICmdList, const FScene* Scene, FViewInfo& View);

//@StarLight code - BEGIN Precomputed Multi Scattering on mobile, edit by wanghai
void PrecomputeSkyAtmosphereLut(FRHICommandListImmediate& RHICmdList, const FScene* InScene, FViewInfo& View);
//@StarLight code - END Precomputed Multi Scattering on mobile, edit by wanghai

extern void SetupSkyAtmosphereViewSharedUniformShaderParameters(const class FViewInfo& View, FSkyAtmosphereViewSharedUniformShaderParameters& OutParameters);

// Prepare the sun light data as a function of the atmosphere state. 
void PrepareSunLightProxy(const FSkyAtmosphereRenderSceneInfo& SkyAtmosphere, uint32 AtmosphereLightIndex, FLightSceneInfo& AtmosphereLight);

