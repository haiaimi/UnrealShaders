// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GenerateWaveFunctionLibrary.generated.h"

/**
 * 
 */
UCLASS()
class UGenerateWaveFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

	UFUNCTION(BlueprintCallable, Category = "WaveGenerator", meta = (WorldContext = "WorldContextObject"))
	static void GenerateWave(const UObject* WorldContextObject, int32 HorizontalTiles, int32 VerticalTiles, TSubclassOf<class AFFTWaveSimulator> WaveSimulatorClass, const FTransform& WaveTransform);
	
	// Test trinangle normal lerp
	UFUNCTION(BlueprintCallable, Category = "WaveGenerator", meta = (WorldContext = "WorldContextObject"))
	static void TriangleNormalInterpolation(const UObject* WorldContextObject, int32 InterpolationNum, const FVector& FirstVec, const FVector& SecondVec, const FVector& ThirdVec, const FVector& FirstColor, const FVector& SecondColor, const FVector& ThirdColor);

	// Test quad normal lerp
	UFUNCTION(BlueprintCallable, Category = "WaveGenerator", meta = (WorldContext = "WorldContextObject"))
	static void QuadNormalInterpolation(const UObject* WorldContextObject, int32 InterpolationNum, const FVector& Min, const FVector& Max, const FVector& TopLeft, const FVector& TopRight, const FVector& DownLeft, const FVector& DownRight);
};
