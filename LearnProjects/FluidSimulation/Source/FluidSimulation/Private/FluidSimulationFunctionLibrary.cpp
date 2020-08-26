#include "FluidSimulationFunctionLibrary.h"
#include "RHICommandList.h"

extern void UpdateFluid(FRHICommandListImmediate& RHICmdList, float DeltaTime, FIntPoint FluidSurfaceSize, ERHIFeatureLevel::Type FeatureLevel);

void UFluidSimulationFunctionLibrary::SimulateFluid2D(float DeltaTime, FIntPoint FluidSurfaceSize)
{

}
