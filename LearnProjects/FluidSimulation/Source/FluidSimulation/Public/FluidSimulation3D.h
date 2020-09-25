// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "RHICommandList.h"


/**
 * This file was used to simulate 3D fluid, such as smoke, fire 
 */
 
 struct FFluidResourceParams
 {
	FTextureResource* TextureResource = nullptr;

	FTextureRenderTargetResource* TextureRenderTargetResource = nullptr;
 };

void UpdateFluid3D(FRHICommandListImmediate& RHICmdList, FFluidResourceParams ResourceParam, uint32 IterationCount, float DeltaTime, float VorticityScale, FIntVector FluidVolumeSize, FScene* Scene, ERHIFeatureLevel::Type FeatureLevel);