#pragma once
#include "CoreMinimal.h"
#include "RHI.h"
#include "RHICommandList.h"
#include "FluidSimulation3D.h"

// After we compute the velocity or density of fluid, we need to render it to screen, but it is more complex than fluid 2D.
void RenderFluidVolume(FRHICommandListImmediate& RHICmdList, const FVolumeFluidProxy& ResourceParam, FTextureRHIRef FluidColor, const FViewInfo* InView);