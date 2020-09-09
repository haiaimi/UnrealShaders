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

namespace FluidSimulation3D
{
	class FAdvectVelocityCS : public FGlobalShader
	{
		DECLARE_GLOBAL_SHADER(FAdvectVelocityCS);
		SHADER_USE_PARAMETER_STRUCT(FAdvectVelocityCS, FGlobalShader)

	public:

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(float, TimeStep)
			SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float4>, VelocityField)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, RWVelocityField)
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
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, RWVorticityField)
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

	IMPLEMENT_SHADER_TYPE(, FVorticityCS, TEXT("/Shaders/Private/Fluid.usf"), TEXT("ComputeVorticity"), SF_Compute)

	class FVorticityForceCS : public FGlobalShader
	{
		DECLARE_GLOBAL_SHADER(FVorticityForceCS);
		SHADER_USE_PARAMETER_STRUCT(FVorticityForceCS, FGlobalShader);

	public:

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(float, Halfrdx)
			SHADER_PARAMETER(float, TimeStep)
			SHADER_PARAMETER(float, ConfinementScale)
			SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float4>, VorticityField)
			SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float4>, VelocityField)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, RWVelocityField)
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

	class FDivergenceCS : public FGlobalShader
	{
		DECLARE_GLOBAL_SHADER(FDivergenceCS);
		SHADER_USE_PARAMETER_STRUCT(FDivergenceCS, FGlobalShader);

	public:

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(float, Halfrdx)
			SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float4>, VelocityField)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, RWDivergence)
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

	IMPLEMENT_SHADER_TYPE(, FDivergenceCS, TEXT("/Shaders/Private/Fluid.usf"), TEXT("Divergence"), SF_Compute)

	void ComputeAdvect(FRDGBuilder& RDG, FGlobalShaderMap* ShaderMap, FIntVector FluidVolumeSize, float TimeStep, FRDGTextureSRVRef VelocityField, FRDGTextureUAVRef DstField)
	{
		TShaderMapRef<FAdvectVelocityCS> AdvectCS(ShaderMap);
		FAdvectVelocityCS::FParameters* PassParameters = RDG.AllocParameters<FAdvectVelocityCS::FParameters>();
		PassParameters->TimeStep = TimeStep;
		PassParameters->VelocityField = VelocityField;
		PassParameters->RWVelocityField = DstField;

		FComputeShaderUtils::AddPass(RDG, RDG_EVENT_NAME("ComputeAdvect"), AdvectCS, PassParameters, FIntVector(FMath::DivideAndRoundUp(FluidVolumeSize.X, THREAD_GROUP_SIZE), FMath::DivideAndRoundUp(FluidVolumeSize.Y, THREAD_GROUP_SIZE), FMath::DivideAndRoundUp(FluidVolumeSize.Z, THREAD_GROUP_SIZE)));
	}

	void ComputeVorticity(FRDGBuilder& RDG, FGlobalShaderMap* ShaderMap, FIntVector FluidVolumeSize, float Halfrdx, FRDGTextureSRVRef VelocityField, FRDGTextureUAVRef VorticityFieldUAV)
	{
		TShaderMapRef<FVorticityCS> VorticityCS(ShaderMap);
		FVorticityCS::FParameters* PassParameters = RDG.AllocParameters<FVorticityCS::FParameters>();
		PassParameters->Halfrdx = Halfrdx;
		PassParameters->VelocityField = VelocityField;
		PassParameters->RWVorticityField = VorticityFieldUAV;

		FComputeShaderUtils::AddPass(RDG, RDG_EVENT_NAME("ComputeVorticity"), VorticityCS, PassParameters, FIntVector(FMath::DivideAndRoundUp(FluidVolumeSize.X , THREAD_GROUP_SIZE), FMath::DivideAndRoundUp(FluidVolumeSize.Y, THREAD_GROUP_SIZE), FMath::DivideAndRoundUp(FluidVolumeSize.Y, THREAD_GROUP_SIZE)));
	}

	void ComputeVorticityForce(FRDGBuilder& RDG, FGlobalShaderMap* ShaderMap, FIntVector FluidVolumeSize, float Halfrdx, float TimeStep, float ConfinementScale, FRDGTextureSRVRef VorticityField, FRDGTextureSRVRef VelocityField, FRDGTextureUAVRef VelocityFieldUAV)
	{
		TShaderMapRef<FVorticityForceCS> VorticityForceCS(ShaderMap);
		FVorticityForceCS::FParameters* PassParameters = RDG.AllocParameters<FVorticityForceCS::FParameters>();
		PassParameters->Halfrdx = Halfrdx;
		PassParameters->TimeStep = TimeStep;
		PassParameters->ConfinementScale = ConfinementScale;
		PassParameters->VorticityField = VorticityField;
		PassParameters->VelocityField = VelocityField;
		PassParameters->RWVelocityField = VelocityFieldUAV;

		FComputeShaderUtils::AddPass(RDG, RDG_EVENT_NAME("ComputeVorticityForce"), VorticityForceCS, PassParameters, FIntVector(FMath::DivideAndRoundUp(FluidVolumeSize.Y, THREAD_GROUP_SIZE), FMath::DivideAndRoundUp(FluidVolumeSize.Y, THREAD_GROUP_SIZE), FMath::DivideAndRoundUp(FluidVolumeSize.Y, THREAD_GROUP_SIZE)));
	}

	void ComputeDivergence(FRDGBuilder& RDG, FGlobalShaderMap* ShaderMap, FIntVector FluidVolumeSize, float Halfrdx, FRDGTextureSRVRef VelocityFieldSRV, FRDGTextureUAVRef DivergenceFieldUAV)
	{
		TShaderMapRef<FDivergenceCS> DivergenceCS(ShaderMap);
		FDivergenceCS::FParameters* PassParameters = RDG.AllocParameters<FDivergenceCS::FParameters>();
		PassParameters->Halfrdx = Halfrdx;
		PassParameters->VelocityField = VelocityFieldSRV;
		PassParameters->RWDivergence = DivergenceFieldUAV;

		FComputeShaderUtils::AddPass(RDG, RDG_EVENT_NAME("ComputeDivergence"), DivergenceCS, PassParameters, FIntVector(FMath::DivideAndRoundUp(FluidVolumeSize.X, THREAD_GROUP_SIZE), FMath::DivideAndRoundUp(FluidVolumeSize.Y, THREAD_GROUP_SIZE), FMath::DivideAndRoundUp(FluidVolumeSize.Y, THREAD_GROUP_SIZE)));
	}
}

void UpdateFluid3D(FRHICommandListImmediate& RHICmdList, float DeltaTime, float VorticityScale, FIntVector FluidVolumeSize, ERHIFeatureLevel::Type FeatureLevel)
{
	check(IsInRenderingThread());

	FPooledRenderTargetDesc FluidVloumeDesc = FPooledRenderTargetDesc::CreateVolumeDesc(FluidVolumeSize.X, FluidVolumeSize.Y, FluidVolumeSize.Z, EPixelFormat::PF_A32B32G32R32F, FClearValueBinding::None, ETextureCreateFlags::TexCreate_None, ETextureCreateFlags::TexCreate_UAV | ETextureCreateFlags::TexCreate_ShaderResource, false);
	TRefCountPtr<IPooledRenderTarget> VelocityTexture3D_0, VelocityTexture3D_1, ColorTexture3D_0, ColorTexture3D_1, VorticityTexture3D, DivergenceTexture3D;
	GRenderTargetPool.FindFreeElement(RHICmdList, FluidVloumeDesc, VelocityTexture3D_0, TEXT("VelocityTexture3D_0"));
	GRenderTargetPool.FindFreeElement(RHICmdList, FluidVloumeDesc, VelocityTexture3D_1, TEXT("VelocityTexture3D_1"));
	GRenderTargetPool.FindFreeElement(RHICmdList, FluidVloumeDesc, ColorTexture3D_0, TEXT("ColorTexture3D_0"));
	GRenderTargetPool.FindFreeElement(RHICmdList, FluidVloumeDesc, ColorTexture3D_1, TEXT("ColorTexture3D_1"));
	GRenderTargetPool.FindFreeElement(RHICmdList, FluidVloumeDesc, VorticityTexture3D, TEXT("VorticityTexture3D"));
	GRenderTargetPool.FindFreeElement(RHICmdList, FluidVloumeDesc, DivergenceTexture3D, TEXT("DivergenceTexture3D"));

	FRDGBuilder GraphBuilder(RHICmdList);

	FRDGTextureRef VelocityField_0 = GraphBuilder.RegisterExternalTexture(VelocityTexture3D_0, TEXT("VelocityField_0"), ERDGResourceFlags::MultiFrame);
	FRDGTextureRef VelocityField_1 = GraphBuilder.RegisterExternalTexture(VelocityTexture3D_1, TEXT("VelocityField_1"), ERDGResourceFlags::MultiFrame);
	FRDGTextureRef ColorField_0 = GraphBuilder.RegisterExternalTexture(ColorTexture3D_0, TEXT("ColorField_0"), ERDGResourceFlags::MultiFrame);
	FRDGTextureRef ColorField_1 = GraphBuilder.RegisterExternalTexture(ColorTexture3D_1, TEXT("ColorField_1"), ERDGResourceFlags::MultiFrame);
	FRDGTextureRef Vorticity = GraphBuilder.RegisterExternalTexture(VorticityTexture3D, TEXT("VorticityField"), ERDGResourceFlags::MultiFrame);
	FRDGTextureRef Divergence = GraphBuilder.RegisterExternalTexture(DivergenceTexture3D, TEXT("DivergenceField"), ERDGResourceFlags::MultiFrame);

	FRDGTextureSRVRef VelocityFieldSRV0 = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(VelocityField_0));
	FRDGTextureSRVRef VelocityFieldSRV1 = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(VelocityField_1));
	FRDGTextureUAVRef VelocityFieldUAV0 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(VelocityField_0));
	FRDGTextureUAVRef VelocityFieldUAV1 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(VelocityField_1));

	FRDGTextureSRVRef ColorFieldSRV0 = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(ColorField_0));
	FRDGTextureSRVRef ColorFieldSRV1 = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(ColorField_1));
	FRDGTextureUAVRef ColorFieldUAV0 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ColorField_0));
	FRDGTextureUAVRef ColorFieldUAV1 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ColorField_1));

	FRDGTextureSRVRef VorticitySRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(Vorticity));
	FRDGTextureUAVRef VorticityUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(Vorticity));

	FRDGTextureSRVRef DivergenceSRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(Divergence));
	FRDGTextureUAVRef DivergenceUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(Divergence));

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(FeatureLevel);
	// The main steps may have some difference with fluid 2D, because this time we don't need to compute Viscous, so we can reduce a jacobi iteration
	
	// 1. Advect velocity field and color 
	// #TODO it may use a high-order advection scheme which is called MacCormack Advection Scheme 
	FluidSimulation3D::ComputeAdvect(GraphBuilder, ShaderMap, FluidVolumeSize, DeltaTime, VelocityFieldSRV0, VelocityFieldUAV1);
	FluidSimulation3D::ComputeAdvect(GraphBuilder, ShaderMap, FluidVolumeSize, DeltaTime, ColorFieldSRV0, ColorFieldUAV1);

	// 2. Apply VorticityConfinement
	FluidSimulation3D::ComputeVorticity(GraphBuilder, ShaderMap, FluidVolumeSize, 0.5f, VelocityFieldSRV1, VorticityUAV);
	FluidSimulation3D::ComputeVorticityForce(GraphBuilder, ShaderMap, FluidVolumeSize, 0.5f, DeltaTime, VorticityScale, VorticitySRV, VelocityFieldSRV1, VelocityFieldUAV0);

	// 3. Apply external force

	// 4. Compute velocity divergence
	FluidSimulation3D::ComputeDivergence(GraphBuilder, ShaderMap, FluidVolumeSize, 0.5f, VelocityFieldSRV0, DivergenceUAV);

	// 5. Compute pressure by jacobi iteration

	// 6. Project velocity to free-divergence


	// Draw fluid with ray-marching
}