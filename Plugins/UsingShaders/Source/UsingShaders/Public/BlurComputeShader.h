#pragma once
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Classes/Kismet/BlueprintFunctionLibrary.h"
#include "RHIResources.h"

/**
 * 
 */

void DrawBlurComputeShaderRenderTarget(AActor* Ac, int32 BlurCounts, FTextureRHIParamRef MyTexture, FTextureRHIParamRef UAVSource,FUnorderedAccessViewRHIParamRef TextureUAV);



