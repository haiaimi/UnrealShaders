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

	UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject"))
	static void SimulateFluid3D(const UObject* WorldContextObject, class UTextureRenderTarget* OutputRenderTarget, int32 IterationCount, float DeltaTime, FIntVector FluidVolumeSize, float VorticityScale = 0.5f);

	UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject"))
	static void SimulateIntearctiveWater01(const UObject* WorldContextObject, const FVector2D& InMoveDir, class UTextureRenderTarget* HeightField01, class UTextureRenderTarget* HeightField02, float DeltaTime);

	UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject"))
	static void BPTest(const UObject* WorldContextObject, const FVector2D& InMoveDir);

	UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject"))
	static FVector2D GetCurCharacterUV(const UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject"))
	static UTextureRenderTarget* GetCurHeightMap(const UObject* WorldContextObject);

	static FVector2D MoveDir;
};