// Fill out your copyright notice in the Description page of Project Settings.


#include "FluidSimulation2D.h"
#include "GlobalShader.h"
#include <ShaderParameterMacros.h>
#include "UniformBuffer.h"
#include "Shader.h"
#include "RenderGraphResources.h"
#include "RenderGraphBuilder.h"
#include "ShaderPermutation.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphUtils.h"

#define THREAD_GROUP_SIZE 4

class FComputeBoundaryShaderCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FComputeBoundaryShaderCS);
	SHADER_USE_PARAMETER_STRUCT(FComputeBoundaryShaderCS, FGlobalShader)

	class FIsVerticalBoundary : SHADER_PERMUTATION_BOOL("VERTICAL_BOUNDARY");
	using FPermutationDomain = TShaderPermutationDomain<FIsVerticalBoundary>;
public:

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(float, ValueScale)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, SrcTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWDstTexture)
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
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), THREAD_GROUP_SIZE);
	}
};

IMPLEMENT_SHADER_TYPE(, FComputeBoundaryShaderCS, TEXT("/Shaders/Private/Fluid.usf"), TEXT("Boundary"), SF_Compute)

class FFluid2DAdvectCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FFluid2DAdvectCS);
	SHADER_USE_PARAMETER_STRUCT(FFluid2DAdvectCS, FGlobalShader);
	
public:

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(float, TimeStep)
		SHADER_PARAMETER(float, Dissipation)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, SrcTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWDstTexture)
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
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), THREAD_GROUP_SIZE);
	}
};

IMPLEMENT_SHADER_TYPE(, FFluid2DAdvectCS, TEXT("/Shaders/Private/Fluid.usf"), TEXT("Advect"), SF_Compute)

class FJacobiSolverCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FJacobiSolverCS);
	SHADER_USE_PARAMETER_STRUCT(FJacobiSolverCS, FGlobalShader);

public:

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(float, Alpha)
		SHADER_PARAMETER(float, rBeta)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, x)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, b)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWDstTexture)
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
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), THREAD_GROUP_SIZE);
	}
};

IMPLEMENT_SHADER_TYPE(, FJacobiSolverCS, TEXT("/Shaders/Private/Fluid.usf"), TEXT("Jacobi"), SF_Compute)

class FDivergenceCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDivergenceCS);
	SHADER_USE_PARAMETER_STRUCT(FDivergenceCS, FGlobalShader);

public:

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(float, Halfrdx)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, SrcTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWDstTexture)
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
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), THREAD_GROUP_SIZE);
	}
};

IMPLEMENT_SHADER_TYPE(, FDivergenceCS, TEXT("/Shaders/Private/Fluid.usf"), TEXT("Divergence"), SF_Compute)

void ComputeBoundary(FRDGBuilder& RDG, FGlobalShaderMap* ShaderMap, FIntPoint FluidSurfaceSize, float Scale, FRDGTextureSRVRef SrcTexure, FRDGTextureUAVRef DstTexture)
{
	FComputeBoundaryShaderCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FComputeBoundaryShaderCS::FIsVerticalBoundary>(true);
	TShaderMapRef<FComputeBoundaryShaderCS> BoundaryVert_CS(ShaderMap, PermutationVector);
	FComputeBoundaryShaderCS::FParameters* PassParameters = RDG.AllocParameters<FComputeBoundaryShaderCS::FParameters>();
	PassParameters->ValueScale = Scale;
	PassParameters->SrcTexture = SrcTexure;
	PassParameters->RWDstTexture = DstTexture;

	FComputeShaderUtils::AddPass(RDG, RDG_EVENT_NAME("InitBoundary_Vertical_CS"), BoundaryVert_CS, PassParameters, FIntVector(FMath::DivideAndRoundUp(FluidSurfaceSize.Y - 1, THREAD_GROUP_SIZE), 1, 1));

	PermutationVector.Set<FComputeBoundaryShaderCS::FIsVerticalBoundary>(false);
	TShaderMapRef<FComputeBoundaryShaderCS> BoundaryHori_CS(ShaderMap, PermutationVector);
	FComputeShaderUtils::AddPass(RDG, RDG_EVENT_NAME("InitBoundary_Horizontal_CS"), BoundaryHori_CS, PassParameters, FIntVector(FMath::DivideAndRoundUp(FluidSurfaceSize.X - 1, THREAD_GROUP_SIZE), 1, 1));
}

void ComputeAdvect(FRDGBuilder& RDG, FGlobalShaderMap* ShaderMap, FIntPoint FluidSurfaceSize, float TimeStep, float Dissipation, FRDGTextureSRVRef SrcTexure, FRDGTextureUAVRef DstTexture)
{
	TShaderMapRef<FFluid2DAdvectCS> AdvectCS(ShaderMap);
	FFluid2DAdvectCS::FParameters* PassParameters = RDG.AllocParameters<FFluid2DAdvectCS::FParameters>();
	PassParameters->TimeStep = TimeStep;
	PassParameters->Dissipation = Dissipation;
	PassParameters->SrcTexture = SrcTexure;
	PassParameters->RWDstTexture = DstTexture;

	FComputeShaderUtils::AddPass(RDG, RDG_EVENT_NAME("ComputeAdvect"), AdvectCS, PassParameters, FIntVector(FMath::DivideAndRoundUp(FluidSurfaceSize.X - 1, THREAD_GROUP_SIZE), FMath::DivideAndRoundUp(FluidSurfaceSize.Y - 1, THREAD_GROUP_SIZE), 1));
}

// used to solve poisson equation
void Jacobi(FRDGBuilder& RDG, FGlobalShaderMap* ShaderMap, FIntPoint FluidSurfaceSize, uint32 IterationCount, float Alpha, float Beta, FRDGTextureSRVRef xb_SRVs[2], FRDGTextureUAVRef xb_UAVs[2])
{
	TShaderMapRef<FJacobiSolverCS> JacobiCS(ShaderMap);
	FJacobiSolverCS::FParameters* PassParameters = RDG.AllocParameters<FJacobiSolverCS::FParameters>();
	PassParameters->Alpha = Alpha;
	PassParameters->rBeta = 1.f / Beta;

	uint8 Switcher = 0;
	for (uint32 i = 0; i < IterationCount; ++i)
	{
		PassParameters->x = xb_SRVs[Switcher];
		PassParameters->b = xb_SRVs[Switcher];
		PassParameters->RWDstTexture = xb_UAVs[(Switcher + 1) & 1];
		FComputeShaderUtils::AddPass(RDG, RDG_EVENT_NAME("JacobiIteration"), JacobiCS, PassParameters, FIntVector(FMath::DivideAndRoundUp(FluidSurfaceSize.X - 1, THREAD_GROUP_SIZE), FMath::DivideAndRoundUp(FluidSurfaceSize.Y - 1, THREAD_GROUP_SIZE), 1));
		Switcher ^= 1;
	}
}

// Compute divergence of a field, in this project we only need to compute divergence of velocity field
void ComputeDivergence(FRDGBuilder& RDG, FGlobalShaderMap* ShaderMap, FIntPoint FluidSurfaceSize, float Halfrdx, FRDGTextureSRVRef SrcTexture, FRDGTextureUAVRef DstTexture)
{
	TShaderMapRef<FDivergenceCS> DivergenceCS(ShaderMap);
	FDivergenceCS::FParameters* PassParameters = RDG.AllocParameters<FDivergenceCS::FParameters>();
	PassParameters->Halfrdx = Halfrdx;
	PassParameters->SrcTexture = SrcTexture;
	PassParameters->RWDstTexture = DstTexture;

	FComputeShaderUtils::AddPass(RDG, RDG_EVENT_NAME("ComputeDivergence"), DivergenceCS, PassParameters, FIntVector(FMath::DivideAndRoundUp(FluidSurfaceSize.X - 1, THREAD_GROUP_SIZE), FMath::DivideAndRoundUp(FluidSurfaceSize.Y - 1, THREAD_GROUP_SIZE), 1));
}

void UpdateFluid(FRHICommandListImmediate& RHICmdList, float DeltaTime, FIntPoint FluidSurfaceSize, ERHIFeatureLevel::Type FeatureLevel)
{
	float Dissipation = 1.f;
	const uint32 IterationCount = 20;
	FRDGBuilder GraphBuilder(RHICmdList);
	{
		FPooledRenderTargetDesc TexDesc = FPooledRenderTargetDesc::Create2DDesc(FluidSurfaceSize, EPixelFormat::PF_A32B32G32R32F, FClearValueBinding::None, TexCreate_None, TexCreate_ShaderResource | TexCreate_UAV, false);
		FRDGTextureRef VelocityFieldSwap0 = GraphBuilder.CreateTexture(TexDesc, TEXT("VelocityFieldSwap0"), ERDGResourceFlags::MultiFrame);
		FRDGTextureRef VelocityFieldSwap1 = GraphBuilder.CreateTexture(TexDesc, TEXT("VelocityFieldSwap1"), ERDGResourceFlags::MultiFrame);
		FRDGTextureRef DivregenceField = GraphBuilder.CreateTexture(TexDesc, TEXT("DivregenceField"), ERDGResourceFlags::MultiFrame);
		
		FRDGTextureSRVRef VelocityFieldSRV0 = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(VelocityFieldSwap0));
		FRDGTextureSRVRef VelocityFieldSRV1 = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(VelocityFieldSwap1));
		FRDGTextureUAVRef VelocityFieldUAV0 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(VelocityFieldSwap0));
		FRDGTextureUAVRef VelocityFieldUAV1 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(VelocityFieldSwap1));

		FRDGTextureSRVRef DivregenceFieldSRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(DivregenceField));
		FRDGTextureUAVRef DivregenceFieldUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(DivregenceField));

		FRDGTextureSRVRef VelocityFiledSRVs[2] = { VelocityFieldSRV0, VelocityFieldSRV1 };
		FRDGTextureUAVRef VelocityFiledUAVs[2] = { VelocityFieldUAV0, VelocityFieldUAV1 };

		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(FeatureLevel);
		ComputeBoundary(GraphBuilder, ShaderMap, FluidSurfaceSize, -1.f, VelocityFieldSRV0, VelocityFieldUAV1);

		ComputeAdvect(GraphBuilder, ShaderMap, FluidSurfaceSize, DeltaTime, Dissipation, VelocityFieldSRV0, VelocityFieldUAV1);

		// #TODO Solve the velocity field possion equation for Viscous Diffusion, so that we can get a new velocity field
		float Alpha = 1.f;
		float Beta = 1.f;
		Jacobi(GraphBuilder, ShaderMap, FluidSurfaceSize, IterationCount & ~0x1, Alpha, Beta,VelocityFiledSRVs, VelocityFiledUAVs);

		// #TODO Compute the divergence of the velocity field compute from pre Jacobi pass, it will be used to compute pressure field, 
		// (nabla)^2 P = nabla ¡¤ w
		// where the left of equation is a nabla arithmetic, right is the divergence of a field(in this place is velocity field)
		float Halfrdx = 0.1f;
		ComputeDivergence(GraphBuilder, ShaderMap, FluidSurfaceSize, Halfrdx, VelocityFieldSRV0, DivregenceFieldUAV);
	}
}

FluidSimulation2D::FluidSimulation2D()
{
}

FluidSimulation2D::~FluidSimulation2D()
{
}
