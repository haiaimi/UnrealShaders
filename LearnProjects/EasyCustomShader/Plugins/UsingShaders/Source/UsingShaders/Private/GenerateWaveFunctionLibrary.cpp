// Fill out your copyright notice in the Description page of Project Settings.


#include "GenerateWaveFunctionLibrary.h"
#include "FFTWaveSimulator.h"
#include "DrawDebugHelpers.h"

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

void UGenerateWaveFunctionLibrary::TriangleNormalInterpolation(const UObject* WorldContextObject, const FVector& FirstVec, const FVector& SecondVec, const FVector& ThirdVec)
{
	if (WorldContextObject && WorldContextObject->GetWorld())
	{

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
