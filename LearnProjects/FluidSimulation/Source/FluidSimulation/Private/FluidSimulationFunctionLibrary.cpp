#include "FluidSimulationFunctionLibrary.h"
#include "RHICommandList.h"
#include "Engine/Engine.h"

extern void UpdateFluid(FRHICommandListImmediate& RHICmdList, float DeltaTime, FIntPoint FluidSurfaceSize, float Viscosity, ERHIFeatureLevel::Type FeatureLevel);

void UFluidSimulationFunctionLibrary::SimulateFluid2D(const UObject* WorldContextObject, float DeltaTime, FIntPoint FluidSurfaceSize)
{
	UWorld* World = WorldContextObject->GetWorld();
	ERHIFeatureLevel::Type FeatureLevel = WorldContextObject->GetWorld()->Scene->GetFeatureLevel();
	if (!GEngine->PreRenderDelegate.IsBoundToObject(World))
	{
		GEngine->PreRenderDelegate.AddWeakLambda(World, [FeatureLevel, DeltaTime, FluidSurfaceSize]() {
			FRHICommandListImmediate& RHICmdList = GetImmediateCommandList_ForRenderCommand();
			UpdateFluid(RHICmdList, DeltaTime, FluidSurfaceSize, 1.f, FeatureLevel);
		});
	}
}
