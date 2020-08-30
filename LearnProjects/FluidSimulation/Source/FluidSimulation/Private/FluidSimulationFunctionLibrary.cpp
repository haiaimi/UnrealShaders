#include "FluidSimulationFunctionLibrary.h"
#include "RHICommandList.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget.h"

extern void UpdateFluid(FRHICommandListImmediate& RHICmdList, FTextureRenderTargetResource* TextureRenderTargetResource, int32 IterationCount, float Dissipation, float DeltaTime, FIntPoint FluidSurfaceSize, float Viscosity, ERHIFeatureLevel::Type FeatureLevel);

void UFluidSimulationFunctionLibrary::SimulateFluid2D(const UObject* WorldContextObject, class UTextureRenderTarget* OutputRenderTarget, int32 IterationCount, float Dissipation, float DeltaTime, FIntPoint FluidSurfaceSize)
{
	FTextureRenderTargetResource* TextureRenderTargetResource = OutputRenderTarget->GameThread_GetRenderTargetResource();
	UWorld* World = WorldContextObject->GetWorld();
	ERHIFeatureLevel::Type FeatureLevel = WorldContextObject->GetWorld()->Scene->GetFeatureLevel();
	if (!GEngine->PreRenderDelegate.IsBoundToObject(World) && OutputRenderTarget)
	{
		GEngine->PreRenderDelegate.AddWeakLambda(World, [TextureRenderTargetResource, FeatureLevel, IterationCount, Dissipation, DeltaTime, FluidSurfaceSize]() {
			FRHICommandListImmediate& RHICmdList = GetImmediateCommandList_ForRenderCommand();
			UpdateFluid(RHICmdList, TextureRenderTargetResource, IterationCount, Dissipation, DeltaTime, FluidSurfaceSize, 0.5f, FeatureLevel);
		});
	}
}
