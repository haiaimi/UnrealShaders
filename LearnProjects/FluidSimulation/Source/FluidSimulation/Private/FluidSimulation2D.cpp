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
#include "RenderTargetPool.h"

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
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, VelocityField)
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

class FAddImpluseCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FAddImpluseCS);
	SHADER_USE_PARAMETER_STRUCT(FAddImpluseCS, FGlobalShader);

public:

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector4, ForceParam)
		SHADER_PARAMETER(FIntPoint, ForcePos)
		SHADER_PARAMETER(float, Radius)
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

IMPLEMENT_SHADER_TYPE(, FAddImpluseCS, TEXT("/Shaders/Private/Fluid.usf"), TEXT("AddImpluse"), SF_Compute)

class FJacobiSolverCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FJacobiSolverCS);
	SHADER_USE_PARAMETER_STRUCT(FJacobiSolverCS, FGlobalShader);

public:

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(float, Alpha)
		SHADER_PARAMETER(float, rBeta)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, Jacobi_x)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, Jacobi_b)
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
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWDivergence)
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

class FSubstractGradientCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSubstractGradientCS);
	SHADER_USE_PARAMETER_STRUCT(FSubstractGradientCS, FGlobalShader);

public:

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(float, Halfrdx)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, VelocityField)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, PressureField)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWVelocityField)
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

IMPLEMENT_SHADER_TYPE(, FSubstractGradientCS, TEXT("/Shaders/Private/Fluid.usf"), TEXT("SubstractGradient"), SF_Compute)

void ComputeBoundary(FRDGBuilder& RDG, FGlobalShaderMap* ShaderMap, FIntPoint FluidSurfaceSize, float Scale, FRDGTextureRef Textures[2], FRDGTextureSRVRef SrcTexure[2], FRDGTextureUAVRef RWDstTexture[2])
{
	AddCopyTexturePass(RDG, Textures[0], Textures[1], FIntPoint(1, 1), FIntPoint(1, 1), FluidSurfaceSize - 1);
	FComputeBoundaryShaderCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FComputeBoundaryShaderCS::FIsVerticalBoundary>(true);
	TShaderMapRef<FComputeBoundaryShaderCS> BoundaryVert_CS(ShaderMap, PermutationVector);
	FComputeBoundaryShaderCS::FParameters* PassParameters = RDG.AllocParameters<FComputeBoundaryShaderCS::FParameters>();
	PassParameters->ValueScale = Scale;
	PassParameters->SrcTexture = SrcTexure[0];
	PassParameters->RWDstTexture = RWDstTexture[1];

	FComputeShaderUtils::AddPass(RDG, RDG_EVENT_NAME("InitBoundary_Vertical_CS"), BoundaryVert_CS, PassParameters, FIntVector(FMath::DivideAndRoundUp(FluidSurfaceSize.Y - 1, THREAD_GROUP_SIZE), 1, 1));

	PassParameters = RDG.AllocParameters<FComputeBoundaryShaderCS::FParameters>();
	PassParameters->ValueScale = Scale;
	PassParameters->SrcTexture = SrcTexure[1];
	PassParameters->RWDstTexture = RWDstTexture[0];

	FComputeShaderUtils::AddPass(RDG, RDG_EVENT_NAME("InitBoundary_Vertical_CS"), BoundaryVert_CS, PassParameters, FIntVector(FMath::DivideAndRoundUp(FluidSurfaceSize.Y - 1, THREAD_GROUP_SIZE), 1, 1));

	PermutationVector.Set<FComputeBoundaryShaderCS::FIsVerticalBoundary>(false);
	TShaderMapRef<FComputeBoundaryShaderCS> BoundaryHori_CS(ShaderMap, PermutationVector);
	PassParameters = RDG.AllocParameters<FComputeBoundaryShaderCS::FParameters>();
	PassParameters->ValueScale = Scale;
	PassParameters->SrcTexture = SrcTexure[0];
	PassParameters->RWDstTexture = RWDstTexture[1];
	FComputeShaderUtils::AddPass(RDG, RDG_EVENT_NAME("InitBoundary_Horizontal_CS"), BoundaryHori_CS, PassParameters, FIntVector(FMath::DivideAndRoundUp(FluidSurfaceSize.X - 1, THREAD_GROUP_SIZE), 1, 1));

	PassParameters = RDG.AllocParameters<FComputeBoundaryShaderCS::FParameters>();
	PassParameters->ValueScale = Scale;
	PassParameters->SrcTexture = SrcTexure[1];
	PassParameters->RWDstTexture = RWDstTexture[0];
	FComputeShaderUtils::AddPass(RDG, RDG_EVENT_NAME("InitBoundary_Horizontal_CS"), BoundaryHori_CS, PassParameters, FIntVector(FMath::DivideAndRoundUp(FluidSurfaceSize.X - 1, THREAD_GROUP_SIZE), 1, 1));
}

void ComputeAdvect(FRDGBuilder& RDG, FGlobalShaderMap* ShaderMap, FIntPoint FluidSurfaceSize, float TimeStep, float Dissipation, FRDGTextureSRVRef VelocityField, FRDGTextureSRVRef SrcTexure, FRDGTextureUAVRef DstTexture)
{
	TShaderMapRef<FFluid2DAdvectCS> AdvectCS(ShaderMap);
	FFluid2DAdvectCS::FParameters* PassParameters = RDG.AllocParameters<FFluid2DAdvectCS::FParameters>();
	PassParameters->TimeStep = TimeStep;
	PassParameters->Dissipation = Dissipation;
	PassParameters->VelocityField = VelocityField;
	PassParameters->SrcTexture = SrcTexure;
	PassParameters->RWDstTexture = DstTexture;

	FComputeShaderUtils::AddPass(RDG, RDG_EVENT_NAME("ComputeAdvect"), AdvectCS, PassParameters, FIntVector(FMath::DivideAndRoundUp(FluidSurfaceSize.X - 1, THREAD_GROUP_SIZE), FMath::DivideAndRoundUp(FluidSurfaceSize.Y - 1, THREAD_GROUP_SIZE), 1));
}

void AddImpluse(FRDGBuilder& RDG, FGlobalShaderMap* ShaderMap, FIntPoint FluidSurfaceSize, FVector4 ForceParam, FIntPoint ForcePos, float ForceRadius, FRDGTextureSRVRef SrcTexture, FRDGTextureUAVRef DstTexture)
{
	TShaderMapRef<FAddImpluseCS> AddImpluseCS(ShaderMap);
	FAddImpluseCS::FParameters* PassParameters = RDG.AllocParameters<FAddImpluseCS::FParameters>();
	PassParameters->ForceParam = ForceParam;
	PassParameters->ForcePos = ForcePos;
	PassParameters->Radius = ForceRadius;
	PassParameters->SrcTexture = SrcTexture;
	PassParameters->RWDstTexture = DstTexture;

	FComputeShaderUtils::AddPass(RDG, RDG_EVENT_NAME("AddImpluse"), AddImpluseCS, PassParameters, FIntVector(FMath::DivideAndRoundUp(FluidSurfaceSize.X - 1, THREAD_GROUP_SIZE), FMath::DivideAndRoundUp(FluidSurfaceSize.Y - 1, THREAD_GROUP_SIZE), 1));
}


// used to solve poisson equation
void Jacobi(FRDGBuilder& RDG, FGlobalShaderMap* ShaderMap, FIntPoint FluidSurfaceSize, uint32 IterationCount, float Alpha, float Beta, FRDGTextureSRVRef x_SRVs[], FRDGTextureUAVRef x_UAVs[], FRDGTextureSRVRef b_SRVs[], bool bUpdateBoundary = false, float Scale = 1.f)
{
	TShaderMapRef<FJacobiSolverCS> JacobiCS(ShaderMap);
	uint8 Switcher = 0;
	for (uint32 i = 0; i < IterationCount; ++i)
	{
		if (bUpdateBoundary)
		{
			FRDGTextureRef Textures[2] = {x_SRVs[Switcher]->GetParent(), x_UAVs[(Switcher + 1)]->GetParent()};
			FRDGTextureSRVRef SRVs[2] = {x_SRVs[Switcher], x_SRVs[(Switcher + 1) & 1]};
			FRDGTextureUAVRef UAVs[2] = {x_UAVs[Switcher], x_UAVs[(Switcher + 1) & 1]};
			ComputeBoundary(RDG, ShaderMap, FluidSurfaceSize, Scale, Textures, SRVs, UAVs);
			Switcher ^= 1;
		}

		FJacobiSolverCS::FParameters* PassParameters = RDG.AllocParameters<FJacobiSolverCS::FParameters>();
		PassParameters->Alpha = Alpha;
		PassParameters->rBeta = 1.f / Beta;
		PassParameters->Jacobi_x = x_SRVs[Switcher];
		PassParameters->Jacobi_b = b_SRVs[Switcher];
		PassParameters->RWDstTexture = x_UAVs[(Switcher + 1) & 1];
		FComputeShaderUtils::AddPass(RDG, RDG_EVENT_NAME("JacobiIteration_%d", i), JacobiCS, PassParameters, FIntVector(FMath::DivideAndRoundUp(FluidSurfaceSize.X - 1, THREAD_GROUP_SIZE), FMath::DivideAndRoundUp(FluidSurfaceSize.Y - 1, THREAD_GROUP_SIZE), 1));
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
	PassParameters->RWDivergence = DstTexture;

	FComputeShaderUtils::AddPass(RDG, RDG_EVENT_NAME("ComputeDivergence"), DivergenceCS, PassParameters, FIntVector(FMath::DivideAndRoundUp(FluidSurfaceSize.X - 1, THREAD_GROUP_SIZE), FMath::DivideAndRoundUp(FluidSurfaceSize.Y - 1, THREAD_GROUP_SIZE), 1));
}

// The final step, u = w - (nabla)p, w is a velocity field with divergence, u is a divergence-free velocity field, now we have got p(pressure field),  
void SubstarctPressureGradient(FRDGBuilder& RDG, FGlobalShaderMap* ShaderMap, FIntPoint FluidSurfaceSize, float Halfrdx, FRDGTextureSRVRef VelocityField, FRDGTextureSRVRef PressureField, FRDGTextureUAVRef RWVelocityField)
{
	TShaderMapRef<FSubstractGradientCS> SubstractGradientCS(ShaderMap);
	FSubstractGradientCS::FParameters* PassParameters = RDG.AllocParameters<FSubstractGradientCS::FParameters>();
	PassParameters->Halfrdx = Halfrdx;
	PassParameters->VelocityField = VelocityField;
	PassParameters->PressureField = PressureField; 
	PassParameters->RWVelocityField = RWVelocityField;

	FComputeShaderUtils::AddPass(RDG, RDG_EVENT_NAME("SubstarctPressureGradient"), SubstractGradientCS, PassParameters, FIntVector(FMath::DivideAndRoundUp(FluidSurfaceSize.X - 1, THREAD_GROUP_SIZE), FMath::DivideAndRoundUp(FluidSurfaceSize.Y - 1, THREAD_GROUP_SIZE), 1));
}

void UpdateFluid(FRHICommandListImmediate& RHICmdList, FTextureRenderTargetResource* TextureRenderTargetResource, float DeltaTime, FIntPoint FluidSurfaceSize, float Viscosity, ERHIFeatureLevel::Type FeatureLevel)
{
	check(IsInRenderingThread());

	FTexture2DRHIRef OutTexture = TextureRenderTargetResource->GetRenderTargetTexture();
	float Dissipation = 0.5f;
	const uint32 IterationCount = 20;

	// First we should create all texture that will used in RenderGraph 
	FRDGTextureDesc TexDesc = FRDGTextureDesc::Create2DDesc(FluidSurfaceSize, PF_A32B32G32R32F, FClearValueBinding(FLinearColor::Black), TexCreate_None, TexCreate_UAV | TexCreate_ShaderResource, false);
	TRefCountPtr<IPooledRenderTarget> PooledVelocityFieldSwap0, PooledVelocityFieldSwap1, PooledDensityFieldSwap0, PooledDensityFieldSwap1, PooledDivregenceField, PooledPressureFieldSwap0, PooledPressureFieldSwap1;
	GRenderTargetPool.FindFreeElement(RHICmdList, TexDesc, PooledVelocityFieldSwap0, TEXT("VelocityFieldSwap0"));
	GRenderTargetPool.FindFreeElement(RHICmdList, TexDesc, PooledVelocityFieldSwap1, TEXT("VelocityFieldSwap1"));
	GRenderTargetPool.FindFreeElement(RHICmdList, TexDesc, PooledDensityFieldSwap0, TEXT("DensityFieldSwap0"));
	GRenderTargetPool.FindFreeElement(RHICmdList, TexDesc, PooledDensityFieldSwap1, TEXT("DensityFieldSwap1"));
	GRenderTargetPool.FindFreeElement(RHICmdList, TexDesc, PooledDivregenceField, TEXT("DivregenceField"));
	GRenderTargetPool.FindFreeElement(RHICmdList, TexDesc, PooledPressureFieldSwap0, TEXT("PressureFieldSwap0"));
	GRenderTargetPool.FindFreeElement(RHICmdList, TexDesc, PooledPressureFieldSwap1, TEXT("PressureFieldSwap1"));

	FRHICopyTextureInfo CopyInfo;
	CopyInfo.Size = FIntVector(FluidSurfaceSize.X, FluidSurfaceSize.Y, 1);
	RHICmdList.CopyTexture(PooledVelocityFieldSwap0->GetRenderTargetItem().TargetableTexture, OutTexture, CopyInfo);

	FRDGBuilder GraphBuilder(RHICmdList);
	{
		FRDGTextureRef VelocityFieldSwap0 = GraphBuilder.RegisterExternalTexture(PooledVelocityFieldSwap0, TEXT("VelocityFieldSwap0"), ERDGResourceFlags::MultiFrame);
		FRDGTextureRef VelocityFieldSwap1 = GraphBuilder.RegisterExternalTexture(PooledVelocityFieldSwap1, TEXT("VelocityFieldSwap1"), ERDGResourceFlags::MultiFrame);
		FRDGTextureRef DensityFieldSwap0 = GraphBuilder.RegisterExternalTexture(PooledDensityFieldSwap0, TEXT("DensityFieldSwap0"), ERDGResourceFlags::MultiFrame);
		FRDGTextureRef DensityFieldSwap1 = GraphBuilder.RegisterExternalTexture(PooledDensityFieldSwap1, TEXT("DensityFieldSwap1"), ERDGResourceFlags::MultiFrame);
		FRDGTextureRef DivregenceField = GraphBuilder.RegisterExternalTexture(PooledDivregenceField, TEXT("DivregenceField"), ERDGResourceFlags::MultiFrame);
		FRDGTextureRef PressureFieldSwap0 = GraphBuilder.RegisterExternalTexture(PooledPressureFieldSwap0, TEXT("PressureFieldSwap0"), ERDGResourceFlags::MultiFrame);
		FRDGTextureRef PressureFieldSwap1 = GraphBuilder.RegisterExternalTexture(PooledPressureFieldSwap1, TEXT("PressureFieldSwap1"), ERDGResourceFlags::MultiFrame);

		FRDGTextureSRVRef VelocityFieldSRV0 = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(VelocityFieldSwap0));
		FRDGTextureSRVRef VelocityFieldSRV1 = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(VelocityFieldSwap1));
		FRDGTextureUAVRef VelocityFieldUAV0 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(VelocityFieldSwap0));
		FRDGTextureUAVRef VelocityFieldUAV1 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(VelocityFieldSwap1));

		FRDGTextureSRVRef DensityFieldSRV0 = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(DensityFieldSwap0));
		FRDGTextureSRVRef DensityFieldSRV1 = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(DensityFieldSwap1));
		FRDGTextureUAVRef DensityFieldUAV0 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(DensityFieldSwap0));
		FRDGTextureUAVRef DensityFieldUAV1 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(DensityFieldSwap1));

		FRDGTextureSRVRef DivregenceFieldSRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(DivregenceField));
		FRDGTextureUAVRef DivregenceFieldUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(DivregenceField));

		FRDGTextureSRVRef PressureFieldSRV0 = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(PressureFieldSwap0));
		FRDGTextureSRVRef PressureFieldSRV1 = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(PressureFieldSwap1));
		FRDGTextureUAVRef PressureFieldUAV0 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(PressureFieldSwap0));
		FRDGTextureUAVRef PressureFieldUAV1 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(PressureFieldSwap1));

		FRDGTextureSRVRef VelocityFieldSRVs[2] = { VelocityFieldSRV0, VelocityFieldSRV1 };
		FRDGTextureUAVRef VelocityFieldUAVs[2] = { VelocityFieldUAV0, VelocityFieldUAV1 };

		FRDGTextureSRVRef DensityFiledSRVs[2] = { DensityFieldSRV0, DensityFieldSRV1 };
		FRDGTextureUAVRef DensityFiledUAVs[2] = { DensityFieldUAV0, DensityFieldUAV1 };

		FRDGTextureSRVRef PressureFieldSRVs[2] = { PressureFieldSRV0, PressureFieldSRV1 };
		FRDGTextureUAVRef PressureFieldUAVs[2] = { PressureFieldUAV0, PressureFieldUAV1 };

		// #TODO
		/*AddClearUAVPass(GraphBuilder, VelocityFieldUAV0, FLinearColor::Black);
		AddClearUAVPass(GraphBuilder, VelocityFieldUAV1, FLinearColor::Black);
		AddClearUAVPass(GraphBuilder, DensityFieldUAV0, FLinearColor::Black);
		AddClearUAVPass(GraphBuilder, DensityFieldUAV1, FLinearColor::Black);
		AddClearUAVPass(GraphBuilder, DivregenceFieldUAV, FLinearColor::Black);
		AddClearUAVPass(GraphBuilder, PressureFieldUAV0, FLinearColor::Black);
		AddClearUAVPass(GraphBuilder, PressureFieldUAV1, FLinearColor::Black);*/

		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(FeatureLevel);

		// 1. Compute the boundary of the velocity field, the velocity of boundary is reverse to the velocity inside
		// The compute the advect of velocity field
		FRDGTextureRef VelocityTextures[2] = { VelocityFieldSwap0, VelocityFieldSwap1 };
		ComputeBoundary(GraphBuilder, ShaderMap, FluidSurfaceSize, -1.f, VelocityTextures, VelocityFieldSRVs, VelocityFieldUAVs);
		ComputeAdvect(GraphBuilder, ShaderMap, FluidSurfaceSize, DeltaTime, Dissipation, VelocityFieldSRV0, VelocityFieldSRV0, VelocityFieldUAV1);

		// Compute for density, such as ink in fluid, make fluid more obviously
		FRDGTextureRef DensityTextures[2] = { DensityFieldSwap0, DensityFieldSwap1 };
		ComputeBoundary(GraphBuilder, ShaderMap, FluidSurfaceSize, 0.f, DensityTextures, DensityFiledSRVs, DensityFiledUAVs);
		ComputeAdvect(GraphBuilder, ShaderMap, FluidSurfaceSize, DeltaTime, Dissipation, VelocityFieldSRV1, DensityFieldSRV0, DensityFieldUAV1);

		// Add Impluse
		FVector4 ForceParam(20.f, 0.f, 0.f, 0.f);
		FIntPoint ForcePos = FluidSurfaceSize / 2;
		float ForceRadius = 20.f;
		AddImpluse(GraphBuilder, ShaderMap, FluidSurfaceSize, ForceParam, ForcePos, ForceRadius, VelocityFieldSRV1, VelocityFieldUAV0);
		// Add ink to field
		FVector4 InkColor(1.f, 1.f, 1.f, 1.f);
		AddImpluse(GraphBuilder, ShaderMap, FluidSurfaceSize, InkColor, ForcePos, ForceRadius, DensityFieldSRV1, DensityFieldUAV0);

		// 2.
		// #TODO Solve the velocity field possion equation for Viscous Diffusion, so that we can get a new velocity field
		float Alpha = 1.f / (Viscosity * DeltaTime);
		float Beta = 4.f + Alpha;
		Jacobi(GraphBuilder, ShaderMap, FluidSurfaceSize, IterationCount & ~0x1, Alpha, Beta, VelocityFieldSRVs, VelocityFieldUAVs, VelocityFieldSRVs);

		// 3.
		// #TODO Compute the divergence of the velocity field that compute from pre Jacobi pass, it will be used to compute pressure field, 
		// (nabla)^2 P = nabla ¡¤ w
		// where the left of equation is a nabla arithmetic, right is the divergence of a field(in this place is velocity field)
		float Halfrdx = 0.5f;
		ComputeDivergence(GraphBuilder, ShaderMap, FluidSurfaceSize, Halfrdx, VelocityFieldSRV0, DivregenceFieldUAV);

		//4.
		// Compute the boundary of pressure field, the presure of boundary is equal to the inside so the scale is 1
		FRDGTextureSRVRef DivregenceFieldSRVs[2] = { DivregenceFieldSRV, DivregenceFieldSRV };
		Alpha = -1.f;
		Beta = 4.f;
		Jacobi(GraphBuilder, ShaderMap, FluidSurfaceSize, IterationCount, Alpha, Beta, PressureFieldSRVs, PressureFieldUAVs, DivregenceFieldSRVs, true, 1.f);
		
		// Set the boundary of velocity field
		ComputeBoundary(GraphBuilder, ShaderMap, FluidSurfaceSize, -1.f, VelocityTextures, VelocityFieldSRVs, VelocityFieldUAVs);

		// 5. substract divergence velocityfield with gradient of pressure field 
		SubstarctPressureGradient(GraphBuilder, ShaderMap, FluidSurfaceSize, Halfrdx, VelocityFieldSRV1, PressureFieldSRV0, VelocityFieldUAV0);
	}

	GraphBuilder.Execute();
}

FluidSimulation2D::FluidSimulation2D()
{
}

FluidSimulation2D::~FluidSimulation2D()
{
}
