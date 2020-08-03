#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "HAL/IConsoleManager.h"
#include "RHI.h"
#include "UniformBuffer.h"
#include <ShaderParameterMacros.h>

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FMobileClusterLightingUniformParameters, )
	SHADER_PARAMETER(FUintVector4, CulledGridSizeParams)
	SHADER_PARAMETER(FVector, LightGridZParams)
	SHADER_PARAMETER_SRV(Buffer<float4>, MobileLocalLightBuffer)
	SHADER_PARAMETER_SRV(Buffer<float4>, MobileSpotLightBuffer)
	SHADER_PARAMETER_SRV(Buffer<uint>, NumCulledLightsGrid)
	SHADER_PARAMETER_SRV(Buffer<uint>, CulledLightDataGrid)
END_GLOBAL_SHADER_PARAMETER_STRUCT()


#define MAX_BUFFER_MIP_LEVEL 4u
#define BUFFER_MIP_LEVEL_SCALE 2u

DECLARE_CYCLE_STAT_EXTERN(TEXT("Mobile Compute Grid"), STAT_MobileComputeGrid, STATGROUP_SceneRendering, RENDERCORE_API);

template<class BufferType>
struct FMobileClusterMipBuffer
{
public:
	void Initialize(uint32 ElementSize, uint32 MaxElementByteCount, EPixelFormat Format, uint32 AdditionalUsage = 0)
	{
		for (uint8 i = 0; i < MAX_BUFFER_MIP_LEVEL; ++i)
		{
			uint32 CurLevelByteCount = MaxElementByteCount / FMath::Max(1u, i * BUFFER_MIP_LEVEL_SCALE);
			if (CurLevelByteCount / ElementSize > 0)
				MipBuffers[i].Initialize(ElementSize, CurLevelByteCount / ElementSize, Format, AdditionalUsage);
		}
	}

	BufferType& GetCurLevelBuffer()
	{
		return MipBuffers[CurLevel];
	}

	uint32 GetMaxSizeBytes()
	{
		return MipBuffers[0].NumBytes;
	}

	void Release()
	{
		for (uint8 i = 0; i < MAX_BUFFER_MIP_LEVEL; ++i)
			MipBuffers[i].Release();
	}

	BufferType MipBuffers[MAX_BUFFER_MIP_LEVEL];

	uint8 CurLevel = 0;
};

class FMobileClusterLightingResources
{
public:
	FDynamicReadBuffer MobileLocalLight;
	FDynamicReadBuffer MobileSpotLight;
	FDynamicReadBuffer ViewSpacePosAndRadiusData;
	FDynamicReadBuffer ViewSpaceDirAndPreprocAngleData;

	FMobileClusterMipBuffer<FDynamicReadBuffer> NumCulledLightsGrid;
	FMobileClusterMipBuffer<FDynamicReadBuffer> CulledLightDataGrid;

	FMobileClusterMipBuffer<FRWBuffer> RWNumCulledLightsGrid;
	FMobileClusterMipBuffer<FRWBuffer> RWCulledLightDataGrid;

	void Release()
	{
		MobileLocalLight.Release();
		MobileSpotLight.Release();
		ViewSpacePosAndRadiusData.Release();
		ViewSpaceDirAndPreprocAngleData.Release();
		NumCulledLightsGrid.Release();
		CulledLightDataGrid.Release();
		RWNumCulledLightsGrid.Release();
		RWCulledLightDataGrid.Release();
	}
};