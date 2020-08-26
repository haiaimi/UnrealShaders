#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "FluidSimulationFunctionLibrary.generated.h"

UCLASS()
class UFluidSimulationFunctionLibrary : public UObject 
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable)
	void SimulateFluid2D(float DeltaTime, FIntPoint FluidSurfaceSize);
};