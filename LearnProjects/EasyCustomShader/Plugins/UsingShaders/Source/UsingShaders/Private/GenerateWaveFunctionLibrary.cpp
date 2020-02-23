// Fill out your copyright notice in the Description page of Project Settings.


#include "GenerateWaveFunctionLibrary.h"
#include "FFTWaveSimulator.h"
#include "DrawDebugHelpers.h"

#define MaxFloat 0x7f7fffff
#define MinFloat 0x00000001

void GetMinAndMax(const TArray<FVector>& AllVectors, FVector& OutMin, FVector& OutMax)
{
	OutMin = FVector(1.f, 1.f, 1.f) * MaxFloat;
	OutMax = FVector(1.f, 1.f, 1.f) * MinFloat;

	for (auto & Iter : AllVectors)
	{
		OutMin.X = FMath::Min(OutMin.X, Iter.X);
		OutMin.Y = FMath::Min(OutMin.Y, Iter.Y);
		OutMin.Z = FMath::Min(OutMin.Z, Iter.Z);

		OutMax.X = FMath::Max(OutMax.X, Iter.X);
		OutMax.Y = FMath::Max(OutMax.Y, Iter.Y);
		OutMax.Z = FMath::Max(OutMax.Z, Iter.Z);
	}
}

bool IsPointInTriangle(const FVector& FirstVec, const FVector& SecondVec, const FVector& ThirdVec, const FVector& Point)
{
	if (FirstVec.Z == SecondVec.Z == ThirdVec.Z)
	{
		FVector V0 = ThirdVec - FirstVec;
		FVector V1 = SecondVec - FirstVec;
		FVector V2 = Point - FirstVec;

		float Dot00 = V0 | V0;
		float Dot01 = V0 | V1;
		float Dot02 = V0 | V2;
		float Dot11 = V1 | V1;
		float Dot12 = V1 | V2;
		float InverDeno = 1 / (Dot00 * Dot11 - Dot01 * Dot01);
		float u = (Dot11 * Dot02 - Dot01 * Dot12) * InverDeno;
		if (u < 0 || u > 1)return false;
		float v = (Dot00 * Dot12 - Dot01 * Dot02) * InverDeno;
		if (v < 0 || v > 1)return false;

		return (u + v) <= 1;
	}

	return false;
}

void UGenerateWaveFunctionLibrary::GenerateWave(const UObject* WorldContextObject, int32 HorizontalTiles, int32 VerticalTiles, TSubclassOf<class AFFTWaveSimulator> WaveSimulatorClass, const FTransform& WaveTransform)
{
	if (WorldContextObject && WorldContextObject->GetWorld())
	{
		if (AFFTWaveSimulator* DefaultWaveSimulator = Cast<AFFTWaveSimulator>(WaveSimulatorClass->GetDefaultObject()))
		{
			const FVector2D Dimension = DefaultWaveSimulator->GetWaveDimension();
		}
	}
}

void UGenerateWaveFunctionLibrary::TriangleNormalInterpolation(const UObject* WorldContextObject, int32 InterpolationNum, const FVector& FirstVec, const FVector& SecondVec, const FVector& ThirdVec, const FVector& FirstColor, const FVector& SecondColor, const FVector& ThirdColor)
{
	if (WorldContextObject && WorldContextObject->GetWorld())
	{
		FVector Min, Max;
		GetMinAndMax({ FirstVec, SecondVec, ThirdVec }, Min, Max);
		const FVector HoriTileOffset = FVector(0.f, (Max.Y - Min.Y) / (InterpolationNum + 1), 0.f);
		const FVector VertTileOffset = FVector((Max.X - Min.X) / (InterpolationNum + 1), 0.f, 0.f);

		const FVector V0 = ThirdVec - FirstVec;
		const FVector V1 = SecondVec - FirstVec;
		const float TriangleArea = (V0 | V1) / 2.f;

		for (int32 i = 0; i < InterpolationNum; ++i)
			for (int32 j = 0;j < InterpolationNum; ++j)
			{
				const FVector Pos = Min + (i + 1) * HoriTileOffset + (j + 1) * VertTileOffset;
				if (IsPointInTriangle(FirstVec, SecondVec, ThirdVec, Pos))
				{
					const float Second = ((Pos - FirstVec) | V0) / (2.f * TriangleArea);
					const float Third = ((Pos - FirstVec) | V1) / (2.f * TriangleArea);
					const float First = 1.f - Second - Third;
					FVector LerpNormal = First * FirstColor + Second * SecondColor + Third * ThirdColor;
					LerpNormal.Normalize();
					DrawDebugDirectionalArrow(WorldContextObject->GetWorld(), Pos, Pos + LerpNormal * 200.f, 30.f, FColor::Red, false, -1.f, 0, 5.f);
				}
			}
		//DrawDebugDirectionalArrow(WorldContextObject->GetWorld(),)
	}
}

void UGenerateWaveFunctionLibrary::QuadNormalInterpolation(const UObject* WorldContextObject, int32 InterpolationNum, const FVector& Min, const FVector& Max, const FVector& TopLeft, const FVector& TopRight, const FVector& DownLeft, const FVector& DownRight)
{
	if (WorldContextObject && WorldContextObject->GetWorld())
	{
		const FVector HoriTileOffset = FVector(0.f, (Max.Y - Min.Y) / (InterpolationNum + 1), 0.f);
		const FVector VertTileOffset = FVector((Max.X - Min.X) / (InterpolationNum + 1), 0.f, 0.f);
		for (int32 i = 0; i < InterpolationNum; ++i)
			for (int32 j = 0;j < InterpolationNum; ++j)
			{
				const FVector Pos = Min + (i + 1) * HoriTileOffset + (j + 1) * VertTileOffset;
				const float U = ((float)i) / (InterpolationNum);
				const float V = ((float)j) / (InterpolationNum);
				FVector LerpNormal = FMath::Lerp(FMath::Lerp(TopLeft, TopRight, U), FMath::Lerp(DownLeft, DownRight, U), V);
				LerpNormal.Normalize();
				DrawDebugDirectionalArrow(WorldContextObject->GetWorld(), Pos, Pos + LerpNormal * 200.f, 30.f, FColor::Red, false, -1.f, 0, 5.f);
			}
	}
}
