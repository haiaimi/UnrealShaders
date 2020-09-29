// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "RenderResource.h"

/**
 * 
 */
class FLUIDSIMULATION_API FInteractiveWater : public FRenderResource
{
public:
	FInteractiveWater();
	~FInteractiveWater();

	void SetResource(class FTextureRenderTargetResource* Height01, class FTextureRenderTargetResource* Height02);

	void UpdateWater();

	void ApplyForce_RenderThread();
	void UpdateHeightField_RenderThread();

	class FRHITexture* GetCurrentTarget();

	class FRHITexture* GetPreHeightField();

	bool IsResourceValid();

	virtual void ReleaseResource();

	float DeltaTime;

	FVector2D MoveDir;

private:
	class FTextureRenderTargetResource* HeightMapRTs[2];

	float TimeAccumlator;

	float ForceTimeAccumlator;

	uint8 Switcher : 1;

	FIntPoint RectSize;

	uint32 SimulateTimePerSecond;

	//ERHIFeatureLevel::Type FeatureLevel;
};

static TGlobalResource<FInteractiveWater> GInteractiveWater;