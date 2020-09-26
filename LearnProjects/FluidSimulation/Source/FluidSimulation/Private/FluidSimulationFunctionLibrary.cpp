#include "FluidSimulationFunctionLibrary.h"
#include "RHICommandList.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget.h"
#include "FluidSimulation3D.h"

extern void UpdateFluid3D(FRHICommandListImmediate& RHICmdList, FVolumeFluidProxy ResourceParam, FSceneView& InView);

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

void UFluidSimulationFunctionLibrary::SimulateFluid3D(const UObject* WorldContextObject, class UTextureRenderTarget* OutputRenderTarget, int32 IterationCount, float DeltaTime, FIntVector FluidVolumeSize, float VorticityScale /*= 0.5f*/)
{
	FTextureRenderTargetResource* TextureRenderTargetResource = OutputRenderTarget ? OutputRenderTarget->GameThread_GetRenderTargetResource() : nullptr;
	UWorld* World = WorldContextObject->GetWorld();
	FVolumeFluidProxy ResourceParam;
	ResourceParam.TextureRenderTargetResource = TextureRenderTargetResource;
	ERHIFeatureLevel::Type FeatureLevel = WorldContextObject->GetWorld()->Scene->GetFeatureLevel();
	FScene* Scene = WorldContextObject->GetWorld()->Scene->GetRenderScene();
	if (!GEngine->PreRenderDelegate.IsBoundToObject(World))
	{
		GEngine->PreRenderDelegate.AddWeakLambda(World, [ResourceParam, FeatureLevel, IterationCount, DeltaTime, FluidVolumeSize,  VorticityScale, Scene]() {
			FRHICommandListImmediate& RHICmdList = GetImmediateCommandList_ForRenderCommand();
			UpdateFluid3D(RHICmdList, ResourceParam, IterationCount, DeltaTime, VorticityScale, FluidVolumeSize, Scene, FeatureLevel);
		});
	}
}
