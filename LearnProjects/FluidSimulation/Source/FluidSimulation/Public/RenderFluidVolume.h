#pragma once
#include "CoreMinimal.h"
#include "RHI.h"
#include "RHICommandList.h"

// After we compute the velocity or density of fluid, we need to render it to screen, but it is more complex than fluid 2D.
void RenderFluidVolume(FRHICommandListImmediate& RHICmdList, FTextureRenderTargetResource* TextureRenderTargetResource, FIntVector FluidVolumeSize, FTextureRHIRef FluidColor, ERHIFeatureLevel::Type FeatureLevel);