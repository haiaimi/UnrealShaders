// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "FluidSimulationCommon.generated.h"

USTRUCT(BlueprintType)
struct FApplyForceParam
{
	GENERATED_BODY()

public:
	FApplyForceParam():
		ForcePos(FVector::ZeroVector),
		ForceRadius(10.f)
	{

	}

	FApplyForceParam(FVector InForcePos, float InRadius):
		ForcePos(InForcePos),
		ForceRadius(InRadius)
	{}

	UPROPERTY(BlueprintReadWrite)
	FVector ForcePos = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite)
	float ForceRadius = 10.f;
};

/**
 * 
 */
UCLASS()
class FLUIDSIMULATIONLIBRARY_API UFluidSimulationCommon : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject"))
	static void AddForcePos(const UObject* WorldContextObject, class UStaticMeshComponent* CurWaterMesh, FApplyForceParam NewForce);
	
};
