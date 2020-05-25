// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GenerateDistanceFieldTexture.generated.h"

class UStaticMesh;
class UTexture2D;

/**
 * Make Distance Field for a primitive, more in MeshDistanceFieldUtilities.cpp
 */
UCLASS()
class SHADOWFAKERY_API UGenerateDistanceFieldTexture : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UGenerateDistanceFieldTexture();

	//FGenerateDistanceFieldTexture(UStaticMesh* GenerateStaticMesh);

	/**
	 * Generate distance field data by using embree, we can also use kd tree
	 */
	UFUNCTION(BlueprintCallable, meta=(WorldContext="WorldContextObject"))
	static void GenerateDistanceFieldTexture(const UObject* WorldContextObject, UStaticMesh* GenerateStaticMesh, int32 DistanceFieldSize, float StartDegree = 90.f, float MakeDFRadius = 16.f, bool bUseGPU = true);

	//~UGenerateDistanceFieldTexture();
};
