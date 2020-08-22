// Fill out your copyright notice in the Description page of Project Settings.


#include "FluidSimulation2D.h"
#include "GlobalShader.h"

class FComputeBoundaryShaderCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FComputeBoundaryShaderCS, Global)

public:
	FComputeBoundaryShaderCS(){};

	FComputeBoundaryShaderCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		ValueScale.Bind(Initializer.ParameterMap, TEXT("ValueScale"));
		Offset.Bind(Initializer.ParameterMap, TEXT("Offset"));
		SrcTexture.Bind(Initializer.ParameterMap, TEXT("SrcTexture"));
		DstTexture.Bind(Initializer.ParameterMap, TEXT("DstTexture"));
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

private:
	LAYOUT_FIELD(FShaderParameter, ValueScale);
	LAYOUT_FIELD(FShaderParameter, Offset);
	LAYOUT_FIELD(FShaderResourceParameter, SrcTexture);
	LAYOUT_FIELD(FRWShaderParameter, DstTexture);
}

IMPLEMENT_SHADER_TYPE(, FComputeBoundaryShaderCS, TEXT("/Shaders/Private/Fluid.usf"), TEXT("Boundary"), SF_Compute)

void ComputeBoundary()
{

}

FluidSimulation2D::FluidSimulation2D()
{
}

FluidSimulation2D::~FluidSimulation2D()
{
}
