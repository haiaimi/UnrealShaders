// Fill out your copyright notice in the Description page of Project Settings.


#include "GenerateDistanceFieldTexture.h"
#include "Math/RandomStream.h"
#include "StaticMeshResources.h"
#include "Engine/StaticMesh.h"

static void GenerateHemisphereSamples(int32 NumThetaSteps, int32 NumPhiSteps, FRandomStream& RandomStream, TArray<FVector4>& Samples)
{
	Samples.Reserve(NumThetaSteps * NumPhiSteps);

	for (int32 ThetaIndex = 0; ThetaIndex < NumThetaSteps; ++ThetaIndex)
	{
		for (int32 PhiIndex = 0; PhiIndex < NumPhiSteps; ++PhiIndex)
		{
			const float U1 = RandomStream.GetFraction();
			const float U2 = RandomStream.GetFraction();

			const float Fraction1 = (ThetaIndex + U1) / (float)NumThetaSteps;
			const float Fraction2 = (PhiIndex + U2) / (float)NumPhiSteps;

			// The sample is normalized, so the radius of sphere is 1
			// We can see Fraction1 as height (z)
			const float R = FMath::Sqrt(1.0f - Fraction1 * Fraction1);

			// Current Phi (0 - 2*PI)
			const float Phi = 2.0f * (float)PI * Fraction2;
			// Convert to Cartesian
			Samples.Add(FVector4(FMath::Cos(Phi) * R, FMath::Sin(Phi) * R, Fraction1));
		}
	}
}

FGenerateDistanceFieldTexture::FGenerateDistanceFieldTexture()
{
}

void FGenerateDistanceFieldTexture::GenerateDistanceFieldTexture(UStaticMesh* GenerateStaticMesh, FIntVector DistanceFieldDimension, UTexture* TargetTex)
{
	if (!GenerateStaticMesh)return;

	const FStaticMeshLODResources& LODModel = GenerateStaticMesh->RenderData->LODResources[0];
	const FBoxSphereBounds& Bounds = GenerateStaticMesh->RenderData->Bounds;

	const int32 NumVoxelDistanceSamples = 1200;
	TArray<FVector4> SampleDirections;
	const int32 NumThetaSteps = FMath::TruncToInt(FMath::Sqrt(NumVoxelDistanceSamples / (2.0f * (float)PI)));
	const int32 NumPhiSteps = FMath::TruncToInt(NumThetaSteps * (float)PI);
	FRandomStream RandomStream(0);
	GenerateHemisphereSamples(NumThetaSteps, NumPhiSteps, RandomStream, SampleDirections);

	
}

FGenerateDistanceFieldTexture::~FGenerateDistanceFieldTexture()
{
}
