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
	/*SHADER_PARAMETER_TEXTURE(Texture2D, MobileLocalLightBuffer)
	SHADER_PARAMETER_TEXTURE(Texture2D, NumCulledLightsGrid)
	SHADER_PARAMETER_TEXTURE(Texture2D, CulledLightDataGrid)*/
	SHADER_PARAMETER_SRV(Buffer<float4>, MobileLocalLightBuffer)
	SHADER_PARAMETER_SRV(Buffer<uint>, NumCulledLightsGrid)
	SHADER_PARAMETER_SRV(Buffer<uint>, CulledLightDataGrid)
END_GLOBAL_SHADER_PARAMETER_STRUCT()


#define MAX_BUFFER_MIP_LEVEL 4u
#define BUFFER_MIP_LEVEL_SCALE 4u

struct FMobileClusterMipBuffer
{
public:
	void Initialize(uint32 ElementSize, uint32 ElementCount, EPixelFormat Format);

	FDynamicReadBuffer& GetCurLevelBuffer()
	{
		return MipBuffers[CurLevel];
	}

	void Release()
	{
		for (uint8 i = 0; i < MAX_BUFFER_MIP_LEVEL; ++i)
			MipBuffers[i].Release();
	}

	FDynamicReadBuffer MipBuffers[MAX_BUFFER_MIP_LEVEL];

	uint8 CurLevel = 0;
};

class FMobileClusterLightingResources
{
public:
	FDynamicReadBuffer MobileLocalLight;
	FMobileClusterMipBuffer NumCulledLightsGrid;
	FMobileClusterMipBuffer CulledLightDataGrid;

	/*FRHITexture* MobileLocalLight;
	FRHITexture* NumCulledLightsGrid;
	FRHITexture* CulledLightDataGrid;*/

	void Release()
	{
		MobileLocalLight.Release();
		NumCulledLightsGrid.Release();
		CulledLightDataGrid.Release();

		/*MobileLocalLight.SafeRelease();
		NumCulledLightsGrid.SafeRelease();
		CulledLightDataGrid.SafeRelease();*/
	}
};