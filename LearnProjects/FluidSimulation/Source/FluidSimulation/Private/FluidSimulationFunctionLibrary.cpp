#include "FluidSimulationFunctionLibrary.h"
#include "RHICommandList.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget.h"

extern void UpdateFluid(FRHICommandListImmediate& RHICmdList, FTextureRenderTargetResource* TextureRenderTargetResource, int32 IterationCount, float Dissipation, float Viscosity, float DeltaTime, FIntPoint FluidSurfaceSize, bool bApplyVorticityForce, float VorticityScale, ERHIFeatureLevel::Type FeatureLevel);
extern void UpdateFluid3D(FRHICommandListImmediate& RHICmdList, uint32 IterationCount, float DeltaTime, float VorticityScale, FIntVector FluidVolumeSize, ERHIFeatureLevel::Type FeatureLevel);

void UFluidSimulationFunctionLibrary::SimulateFluid2D(const UObject* WorldContextObject, class UTextureRenderTarget* OutputRenderTarget, int32 IterationCount, float Dissipation, float Viscosity, float DeltaTime, FIntPoint FluidSurfaceSize, bool bApplyVorticityForce, float VorticityScale)
{
	FTextureRenderTargetResource* TextureRenderTargetResource = OutputRenderTarget->GameThread_GetRenderTargetResource();
	UWorld* World = WorldContextObject->GetWorld();
	ERHIFeatureLevel::Type FeatureLevel = WorldContextObject->GetWorld()->Scene->GetFeatureLevel();
	if (!GEngine->PreRenderDelegate.IsBoundToObject(World) && OutputRenderTarget)
	{
		GEngine->PreRenderDelegate.AddWeakLambda(World, [TextureRenderTargetResource, FeatureLevel, IterationCount, Dissipation, Viscosity, DeltaTime, FluidSurfaceSize, bApplyVorticityForce, VorticityScale]() {
			FRHICommandListImmediate& RHICmdList = GetImmediateCommandList_ForRenderCommand();
			UpdateFluid(RHICmdList, TextureRenderTargetResource, IterationCount, Dissipation, Viscosity, DeltaTime, FluidSurfaceSize, bApplyVorticityForce, VorticityScale, FeatureLevel);
		});
	}
}

void UFluidSimulationFunctionLibrary::SimulateFluid3D(const UObject* WorldContextObject, int32 IterationCount, float DeltaTime, FIntVector FluidVolumeSize, float VorticityScale /*= 0.5f*/)
{
	UWorld* World = WorldContextObject->GetWorld();
	ERHIFeatureLevel::Type FeatureLevel = WorldContextObject->GetWorld()->Scene->GetFeatureLevel();
	if (!GEngine->PreRenderDelegate.IsBoundToObject(World))
	{
		GEngine->PreRenderDelegate.AddWeakLambda(World, [FeatureLevel, IterationCount, DeltaTime, FluidVolumeSize,  VorticityScale]() {
			FRHICommandListImmediate& RHICmdList = GetImmediateCommandList_ForRenderCommand();
			UpdateFluid3D(RHICmdList, IterationCount, DeltaTime, VorticityScale, FluidVolumeSize, FeatureLevel);
		});
	}
}
