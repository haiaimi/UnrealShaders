#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "FluidSimulationFunctionLibrary.generated.h"

UCLASS()
class UFluidSimulationFunctionLibrary : public UObject 
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, meta=(WorldContext="WorldContextObject"))
	static void SimulateFluid2D(const UObject* WorldContextObject, class UTextureRenderTarget* OutputRenderTarget, int32 IterationCount, float Dissipation, float Viscosity, float DeltaTime, FIntPoint FluidSurfaceSize, bool bApplyVorticityForce = false, float VorticityScale = 0.5f);
};