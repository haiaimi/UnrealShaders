// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

class UStaticMesh;
class UTexture;

/**
 * Make Distance Field for a primitive, more in MeshDistanceFieldUtilities.cpp
 */
class SHADOWFAKERY_API FGenerateDistanceFieldTexture
{
public:
	FGenerateDistanceFieldTexture();

	//FGenerateDistanceFieldTexture(UStaticMesh* GenerateStaticMesh);

	/**
	 * Generate distance field data by using embree, we can also use kd tree
	 */
	void GenerateDistanceFieldTexture(UStaticMesh* GenerateStaticMesh, FIntVector DistanceFieldDimension, UTexture* TargetTex);

	~FGenerateDistanceFieldTexture();
};
