#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "HAL/IConsoleManager.h"
#include "RHI.h"
#include "UniformBuffer.h"
#include <ShaderParameterMacros.h>

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FMobileClusterLightingUniformParameters, )
	SHADER_PARAMETER_SRV(Buffer<float4>, MobileLocalLightBuffer)
	SHADER_PARAMETER_SRV(Buffer<uint>, NumCulledLightsGrid)
	SHADER_PARAMETER_SRV(Buffer<uint>, CulledLightDataGrid)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

class FMobileClusterLightingResources
{
public:
	FDynamicReadBuffer MobileLocalLight;
	FDynamicReadBuffer NumCulledLightsGrid;
	FDynamicReadBuffer CulledLightDataGrid;

	void Release()
	{
		MobileLocalLight.Release();
		NumCulledLightsGrid.Release();
		CulledLightDataGrid.Release();
	}
};