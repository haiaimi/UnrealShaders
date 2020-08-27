#include "FluidSimulationFunctionLibrary.h"
#include "RHICommandList.h"
#include "Engine/Engine.h"

extern void UpdateFluid(FRHICommandListImmediate& RHICmdList, float DeltaTime, FIntPoint FluidSurfaceSize, ERHIFeatureLevel::Type FeatureLevel);

void UFluidSimulationFunctionLibrary::SimulateFluid2D(const UObject* WorldContextObject, float DeltaTime, FIntPoint FluidSurfaceSize)
{
	ERHIFeatureLevel::Type FeatureLevel = WorldContextObject->GetWorld()->Scene->GetFeatureLevel();
	//if (!GEngine->PreRenderDelegate.IsBoundToObject(WorldContextObject))
	{
		GEngine->PreRenderDelegate.AddLambda([FeatureLevel, DeltaTime, FluidSurfaceSize]() {
			FRHICommandListImmediate& RHICmdList = GetImmediateCommandList_ForRenderCommand();
			UpdateFluid(RHICmdList, DeltaTime, FluidSurfaceSize, FeatureLevel);
		});
	}
}
