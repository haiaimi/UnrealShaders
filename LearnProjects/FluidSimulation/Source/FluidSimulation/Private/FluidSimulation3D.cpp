// Fill out your copyright notice in the Description page of Project Settings.


#include "FluidSimulation3D.h"
#include "RHI.h"
#include "RendererInterface.h"
#include "RHICommandList.h"
#include "GlobalShader.h"
#include <ShaderParameterMacros.h>
#include "UniformBuffer.h"
#include "Shader.h"
#include "RenderGraphResources.h"
#include "RenderGraphBuilder.h"
#include "ShaderPermutation.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"

#define THREAD_GROUP_SIZE 8

class FAdvectVelocityCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FAdvectVelocityCS);
	
public:

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(float, TimeStep)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float4>, VelocityTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, RWVelocityTexture)
		END_SHADER_PARAMETER_STRUCT()

public:

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
		OutEnvironment.SetDefine(TEXT("THREAD_GROUP_SIZE"), THREAD_GROUP_SIZE);
	}
};

IMPLEMENT_SHADER_TYPE(, FAdvectVelocityCS, TEXT("/Shaders/Private/Fluid.usf"), TEXT("AdvectVelocity"), SF_Compute)

class FVorticityCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVorticityCS);
	SHADER_USE_PARAMETER_STRUCT(FVorticityCS, FGlobalShader);

public:

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(float, Halfrdx)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float4>, VelocityField)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, RWVorticity)
		END_SHADER_PARAMETER_STRUCT()

public:

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
		OutEnvironment.SetDefine(TEXT("THREAD_GROUP_SIZE"), THREAD_GROUP_SIZE);
	}
};

IMPLEMENT_SHADER_TYPE(, FVorticityCS, TEXT("/Shaders/Private/Fluid.usf"), TEXT("Vorticity"), SF_Compute)

class FVorticityForceCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVorticityForceCS);
	SHADER_USE_PARAMETER_STRUCT(FVorticityForceCS, FGlobalShader);

public:

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(float, Halfrdx)
		SHADER_PARAMETER(float, TimeStep)
		SHADER_PARAMETER(float, dxScale)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float4>, VorticityField)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float4>, VelocityField)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, RWDstTexture)
		END_SHADER_PARAMETER_STRUCT()

public:

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
		OutEnvironment.SetDefine(TEXT("THREAD_GROUP_SIZE"), THREAD_GROUP_SIZE);
	}
};

IMPLEMENT_SHADER_TYPE(, FVorticityForceCS, TEXT("/Shaders/Private/Fluid.usf"), TEXT("VorticityForce"), SF_Compute)


void ComputeAdvect(FRDGBuilder& RDG, FGlobalShaderMap* ShaderMap, FIntVector FluidVolumeSize, float TimeStep, FRDGTextureSRV VelocityField, FRDGTextureUAV DstField)
{
	TShaderMapRef<FAdvectVelocityCS> AdvectCS(ShaderMap);
	FAdvectVelocityCS::FParameters* PassParameters = RDG.AllocParameters<FAdvectVelocityCS::FParameters>();
	PassParameters->TimeStep = TimeStep;
	PassParameters->VelocityField = VelocityField;
	PassParameters->RWVelocityTexture = DstTexture;

	FComputeShaderUtils::AddPass(RDG, RDG_EVENT_NAME("ComputeAdvect"), AdvectCS, PassParameters, FIntVector(FMath::DivideAndRoundUp(FluidVolumeSize.X, THREAD_GROUP_SIZE), FMath::DivideAndRoundUp(FluidVolumeSize.Y, THREAD_GROUP_SIZE), FMath::DivideAndRoundUp(FluidVolumeSize.Z, THREAD_GROUP_SIZE)));
}

void UpdateFluid3D(FRHICommandListImmediate& RHICmdList, float DeltaTime, FIntVector FluidVolumeSize, ERHIFeatureLevel::Type FeatureLevel)
{
	check(IsInRenderingThread());

	FPooledRenderTargetDesc FluidVloumeDesc = FPooledRenderTargetDesc::CreateVolumeDesc(FluidVolumeSize.X, FluidVolumeSize.Y, FluidVolumeSize.Z, EPixelFormat::PF_A32B32G32R32F, FClearValueBinding::None, ETextureCreateFlags::None, ETextureCreateFlags::TexCreate_UAV | ETextureCreateFlags::TexCreate_ShaderResource, false);
	TRefCountPtr<IPooledRenderTarget> VelocityTexture3D_0, VelocityTexture3D_1, ColorTexture3D_0, ColorTexture3D_1;
	GRenderTargetPool.FindFreeElement(RHICmdList, FluidVloumeDesc, VelocityTexture3D_0, TEXT("VelocityTexture3D_0"));
	GRenderTargetPool.FindFreeElement(RHICmdList, FluidVloumeDesc, VelocityTexture3D_1, TEXT("VelocityTexture3D_1"));
	GRenderTargetPool.FindFreeElement(RHICmdList, FluidVloumeDesc, ColorTexture3D_0, TEXT("ColorTexture3D_0"));
	GRenderTargetPool.FindFreeElement(RHICmdList, FluidVloumeDesc, ColorTexture3D_1, TEXT("ColorTexture3D_1"));

	FRDGTextureRef VelocityField_0 = GraphBuilder.RegisterExternalTexture(VelocityTexture3D_0, TEXT("VelocityField_0"), ERDGResourceFlags::MultiFrame);
	FRDGTextureRef VelocityField_1 = GraphBuilder.RegisterExternalTexture(VelocityTexture3D_1, TEXT("VelocityField_1"), ERDGResourceFlags::MultiFrame);
	FRDGTextureRef ColorField_0 = GraphBuilder.RegisterExternalTexture(ColorTexture3D_0, TEXT("ColorField_0"), ERDGResourceFlags::MultiFrame);
	FRDGTextureRef ColorField_1 = GraphBuilder.RegisterExternalTexture(ColorTexture3D_1, TEXT("ColorField_1"), ERDGResourceFlags::MultiFrame);

	FRDGTextureSRVRef VelocityFieldSRV0 = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(VelocityField_0));
	FRDGTextureSRVRef VelocityFieldSRV1 = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(VelocityField_1));
	FRDGTextureUAVRef VelocityFieldUAV0 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(VelocityField_0));
	FRDGTextureUAVRef VelocityFieldUAV1 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(VelocityField_1));

	FRDGTextureSRVRef ColorFieldSRV0 = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(ColorField_0));
	FRDGTextureSRVRef ColorFieldSRV1 = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(ColorField_1));
	FRDGTextureUAVRef ColorFieldUAV0 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ColorField_0));
	FRDGTextureUAVRef ColorFieldUAV1 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ColorField_1));

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(FeatureLevel);
	FRDGBuilder GraphBuilder(RHICmdList);
	// The main steps may have some difference with fluid 2D, because this time we don't need to compute Viscous, so we can reduce a jacobi iteration
	
	// 1. Advect velocity field and color 
	// #TODO it may use a high-order advection scheme which is called MacCormack Advection Scheme 
	ComputeAdvect(GraphBuilder, ShaderMap, FluidVolumeSize, DeltaTime, VelocityFieldSRV0, VelocityFieldUAV1);
	ComputeAdvect(GraphBuilder, ShaderMap, FluidVolumeSize, DeltaTime, ColorFieldSRV0, ColorFieldUAV1);

	// 2. Apply VorticityConfinement

	// 3. Apply external force

	// 4. Compute velocity divergence

	// 5. Compute pressure by jacobi iteration

	// 6. Project velocity to free-divergence


	// Draw fluid with ray-marching
}