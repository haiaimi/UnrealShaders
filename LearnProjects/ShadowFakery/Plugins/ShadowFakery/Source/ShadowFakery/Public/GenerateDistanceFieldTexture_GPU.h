// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterUtils.h"

#define GENERATE_TEXTURE_DF 0

/**
 * 
 */
class SHADOWFAKERY_API FGenerateDistanceFieldTexture_GPU
{
public:
	FGenerateDistanceFieldTexture_GPU();
	~FGenerateDistanceFieldTexture_GPU();
};

//static void GenerateMeshMaskTexture(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, class UStaticMesh* StaticMesh, float StartDegree, uint32 TextureSize);
