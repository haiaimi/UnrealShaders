#include "FluidSimulationFunctionLibrary.h"
#include "RHICommandList.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget.h"
#include "FluidSimulation3D.h"
#include "../Private/SceneRendering.h"
#include "InteractiveWater.h"
#include "RenderingThread.h"

extern void UpdateFluid(FRHICommandListImmediate& RHICmdList, FTextureRenderTargetResource* TextureRenderTargetResource, int32 IterationCount, float Dissipation, float Viscosity, float DeltaTime, FIntPoint FluidSurfaceSize, bool bApplyVorticityForce, float VorticityScale, ERHIFeatureLevel::Type FeatureLevel);

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
	ResourceParam.IterationCount = IterationCount;
	ResourceParam.TimeStep = DeltaTime;
	ResourceParam.FluidVolumeSize = FluidVolumeSize;
	ResourceParam.VorticityScale = VorticityScale;
	ResourceParam.TextureRenderTargetResource = TextureRenderTargetResource;
	ERHIFeatureLevel::Type FeatureLevel = WorldContextObject->GetWorld()->Scene->GetFeatureLevel();
	FScene* Scene = WorldContextObject->GetWorld()->Scene->GetRenderScene();
	if (!GEngine->PreRenderDelegate.IsBoundToObject(World))
	{
		GEngine->PreRenderDelegate.AddWeakLambda(World, [ResourceParam, FeatureLevel, IterationCount, DeltaTime, FluidVolumeSize,  VorticityScale, Scene]() {
			FRHICommandListImmediate& RHICmdList = GetImmediateCommandList_ForRenderCommand();
			FViewInfo* ViewInfo = nullptr;
			UpdateFluid3D(RHICmdList, ResourceParam, ViewInfo);
		});
	}
}

void UFluidSimulationFunctionLibrary::SimulateIntearctiveWater01(const UObject* WorldContextObject, const FVector2D& InMoveDir, class UTextureRenderTarget* HeightField01, class UTextureRenderTarget* HeightField02, float DeltaTime)
{
	if (!GInteractiveWater.IsResourceValid())
	{
		GInteractiveWater.SetResource(HeightField01->GameThread_GetRenderTargetResource(), HeightField02->GameThread_GetRenderTargetResource());
	}
	UWorld* World = WorldContextObject->GetWorld();
	if (!GEngine->PreRenderDelegate.IsBoundToObject(World))
	{
		UE_LOG(LogTemp, Log, TEXT("------Move Dir: %s------"), *InMoveDir.ToString());

		GEngine->PreRenderDelegate.AddWeakLambda(World, [DeltaTime, InMoveDir]() {
			GInteractiveWater.DeltaTime = DeltaTime;
			GInteractiveWater.MoveDir = InMoveDir;
			UE_LOG(LogTemp, Log, TEXT("------Move Dir: %s------"), *InMoveDir.ToString());
			GInteractiveWater.UpdateWater();
		});
	}
	/*float InDeltaTime = DeltaTime;
	ENQUEUE_RENDER_COMMAND(FSimuateWater)([InDeltaTime](FRHICommandListImmediate& RHICmdList)
	{
		GInteractiveWater.UpdateWater(InDeltaTime);
	});*/
}

void UFluidSimulationFunctionLibrary::BPTest(const UObject* WorldContextObject, const FVector2D& InMoveDir)
{
	UE_LOG(LogTemp, Log, TEXT("------Move Dir: %s"), *InMoveDir.ToString());
}

