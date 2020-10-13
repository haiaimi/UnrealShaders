// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "RenderResource.h"

/**
 * 
 */
class FLUIDSIMULATION_API FInteractiveWater
{
public:
	FInteractiveWater();
	~FInteractiveWater();

	void SetResource(class UTextureRenderTarget* Height01, class UTextureRenderTarget* Height02, class UTextureRenderTarget* InNormalMap, float SimulateDuration, ERHIFeatureLevel::Type InFeatureLevel);

	bool ShouldSimulate(float DeltaTime);

	void UpdateWater();

	void UpdateForceParams(float DeltaTime, FVector2D CurDir, FVector CenterPos, float AreaSize, const TArray<FVector>& AllForce);

	void ApplyForce_RenderThread();
	void UpdateHeightField_RenderThread();
	void ComputeNormal_RenderThread();

	class FRHITexture* GetCurrentTarget();

	FVector2D UpdateRoleUV(FVector2D CurDir);

	class UTextureRenderTarget* GetCurrentTarget_GameThread();

	class UTextureRenderTarget* GetCurrentTarget_GameThread(FVector2D CurDir);

	class FRHITexture* GetPreHeightField();

	bool IsResourceValid();

	void ReleaseResource();

	float DeltaTime;

	// #TODO
	bool bShouldApplyForce;

	FVector2D MoveDir;

	FVector2D ForcePos = FVector2D(0.5f, 0.5f);
	FVector2D Offset;

private:
	class FTextureRenderTargetResource* HeightMapRTs[2];
	class FTextureRenderTargetResource* NormalMap;
	class UTextureRenderTarget* HeightMapRTs_GameThread[2];

	float TimeAccumlator;

	float ForceTimeAccumlator;

	uint8 Switcher : 1;

	FIntPoint RectSize;

	float PerSimulateDuration;

	bool bShouldUpdate;

	TArray<FVector4> ForcePointParams;

	ERHIFeatureLevel::Type FeatureLevel;
};

static FInteractiveWater GInteractiveWater;