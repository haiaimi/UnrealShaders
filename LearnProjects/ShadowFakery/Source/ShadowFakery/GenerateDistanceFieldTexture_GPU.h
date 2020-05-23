// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterUtils.h"

/**
 * 
 */
class SHADOWFAKERY_API FGenerateDistanceFieldTexture_GPU
{
public:
	FGenerateDistanceFieldTexture_GPU();
	~FGenerateDistanceFieldTexture_GPU();
};

class FGenerateMeshMaskShaderVS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FGenerateMeshMaskShaderVS, Global)

public:
	FGenerateMeshMaskShaderVS() {};

	FGenerateMeshMaskShaderVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		ViewProjMatrix.Bind(Initializer.ParameterMap, TEXT("ViewProjMatrix"));
	}

	static bool ShouldCache(EShaderPlatform Platform)
	{
		return true;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Paramers)
	{
		return true;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	void SetParameters(
		FRHICommandListImmediate& RHICmdList,
		FMatrix& ViewProjMat
	)
	{
		SetShaderValue(RHICmdList, GetVertexShader(), ViewProjMatrix, ViewProjMat);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);

		Ar << ViewProjMatrix;

		return bShaderHasOutdatedParameters;
	}

private:
	FShaderParameter ViewProjMatrix;
};

class FGenerateMeshMaskShaderPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FGenerateMeshMaskShaderVS, Global)

public:
	FGenerateMeshMaskShaderPS() {};

	FGenerateMeshMaskShaderPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		ViewProjMatrix.Bind(Initializer.ParameterMap, TEXT("ViewProjMatrix"));
	}

	static bool ShouldCache(EShaderPlatform Platform)
	{
		return true;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Paramers)
	{
		return true;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	void SetParameters(
		FRHICommandListImmediate& RHICmdList,
		FMatrix& ViewProjMat
	)
	{
		SetShaderValue(RHICmdList, GetVertexShader(), ViewProjMatrix, ViewProjMat);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);

		Ar << ViewProjMatrix;

		return bShaderHasOutdatedParameters;
	}

private:
	FShaderParameter ViewProjMatrix;
};

IMPLEMENT_SHADER_TYPE(, FGenerateMeshMaskShaderVS, TEXT("/Plugins/Shaders/Private/FFTWave.usf"), TEXT("GenerateMeshMaskShaderVS"), SF_Vertex)
IMPLEMENT_SHADER_TYPE(, FGenerateMeshMaskShaderPS, TEXT("/Plugins/Shaders/Private/FFTWave.usf"), TEXT("GenerateMeshMaskShaderPS"), SF_Pixel)

static void GenerateMeshMaskTexture(class UStaticMesh* StaticMesh, float StartDegree);