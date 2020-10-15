// Fill out your copyright notice in the Description page of Project Settings.


#include "Simulation/FluidSimulation3D.h"
#include "RendererInterface.h"
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
#include "../Private/ScenePrivate.h"
#include "Simulation/RenderFluidVolume.h"

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
			SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float4>, SrcTexture)
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

	IMPLEMENT_SHADER_TYPE(, FAdvectVelocityCS, TEXT("/FluidShaders/Fluid3D.usf"), TEXT("AdvectVelocity"), SF_Compute)

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

	IMPLEMENT_SHADER_TYPE(, FVorticityCS, TEXT("/FluidShaders/Fluid3D.usf"), TEXT("ComputeVorticity"), SF_Compute)

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

	IMPLEMENT_SHADER_TYPE(, FVorticityForceCS, TEXT("/FluidShaders/Fluid3D.usf"), TEXT("VorticityForce"), SF_Compute)

	class FDivergenceCS : public FGlobalShader
	{
		DECLARE_GLOBAL_SHADER(FDivergenceCS);
		SHADER_USE_PARAMETER_STRUCT(FDivergenceCS, FGlobalShader);

	public:

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
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

	IMPLEMENT_SHADER_TYPE(, FDivergenceCS, TEXT("/FluidShaders/Fluid3D.usf"), TEXT("DivergenceCS"), SF_Compute)

	class FAddImpluseCS : public FGlobalShader
	{
		DECLARE_GLOBAL_SHADER(FAddImpluseCS);
		SHADER_USE_PARAMETER_STRUCT(FAddImpluseCS, FGlobalShader);

	public:

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(FVector4, ForceParam)
			SHADER_PARAMETER(FIntVector, ForcePos)
			SHADER_PARAMETER(float, Radius)
			SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float4>, SrcTexture)
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

	IMPLEMENT_SHADER_TYPE(, FAddImpluseCS, TEXT("/FluidShaders/Fluid3D.usf"), TEXT("AddImpluse"), SF_Compute)

	class FJacobiSolverCS : public FGlobalShader
	{
		DECLARE_GLOBAL_SHADER(FJacobiSolverCS);
		SHADER_USE_PARAMETER_STRUCT(FJacobiSolverCS, FGlobalShader);

	public:

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float4>, PressureField)
			SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float4>, DivergenceField)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, RWPressureField)
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

	IMPLEMENT_SHADER_TYPE(, FJacobiSolverCS, TEXT("/FluidShaders/Fluid3D.usf"), TEXT("Jacobi"), SF_Compute)

	class FSubstractGradientCS : public FGlobalShader
	{
		DECLARE_GLOBAL_SHADER(FSubstractGradientCS);
		SHADER_USE_PARAMETER_STRUCT(FSubstractGradientCS, FGlobalShader);

	public:

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(float, Halfrdx)
			SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float4>, VelocityField)
			SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float>, PressureField)
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

	IMPLEMENT_SHADER_TYPE(, FSubstractGradientCS, TEXT("/FluidShaders/Fluid3D.usf"), TEXT("SubstractGradient"), SF_Compute)


	void ComputeAdvect(FRDGBuilder& RDG, FGlobalShaderMap* ShaderMap, FIntVector FluidVolumeSize, float TimeStep, FRDGTextureSRVRef VelocityField, FRDGTextureSRVRef SrcField, FRDGTextureUAVRef DstField)
	{
		TShaderMapRef<FAdvectVelocityCS> AdvectCS(ShaderMap);
		FAdvectVelocityCS::FParameters* PassParameters = RDG.AllocParameters<FAdvectVelocityCS::FParameters>();
		PassParameters->TimeStep = TimeStep;
		PassParameters->VelocityField = VelocityField;
		PassParameters->SrcTexture = SrcField;
		PassParameters->RWDstTexture = DstField;

		FComputeShaderUtils::AddPass(RDG, RDG_EVENT_NAME("ComputeAdvect"), AdvectCS, PassParameters, FIntVector(FMath::DivideAndRoundUp(FluidVolumeSize.X, THREAD_GROUP_SIZE), FMath::DivideAndRoundUp(FluidVolumeSize.Y, THREAD_GROUP_SIZE), FMath::DivideAndRoundUp(FluidVolumeSize.Z, THREAD_GROUP_SIZE)));
	}

	void ComputeVorticity(FRDGBuilder& RDG, FGlobalShaderMap* ShaderMap, FIntVector FluidVolumeSize, float Halfrdx, FRDGTextureSRVRef VelocityField, FRDGTextureUAVRef VorticityFieldUAV)
	{
		TShaderMapRef<FVorticityCS> VorticityCS(ShaderMap);
		FVorticityCS::FParameters* PassParameters = RDG.AllocParameters<FVorticityCS::FParameters>();
		PassParameters->Halfrdx = Halfrdx;
		PassParameters->VelocityField = VelocityField;
		PassParameters->RWVorticityField = VorticityFieldUAV;

		FComputeShaderUtils::AddPass(RDG, RDG_EVENT_NAME("ComputeVorticity"), VorticityCS, PassParameters, FIntVector(FMath::DivideAndRoundUp(FluidVolumeSize.X , THREAD_GROUP_SIZE), FMath::DivideAndRoundUp(FluidVolumeSize.Y, THREAD_GROUP_SIZE), FMath::DivideAndRoundUp(FluidVolumeSize.Z, THREAD_GROUP_SIZE)));
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

		FComputeShaderUtils::AddPass(RDG, RDG_EVENT_NAME("ComputeVorticityForce"), VorticityForceCS, PassParameters, FIntVector(FMath::DivideAndRoundUp(FluidVolumeSize.Y, THREAD_GROUP_SIZE), FMath::DivideAndRoundUp(FluidVolumeSize.Y, THREAD_GROUP_SIZE), FMath::DivideAndRoundUp(FluidVolumeSize.Z, THREAD_GROUP_SIZE)));
	}

	void AddImpluse(FRDGBuilder& RDG, FGlobalShaderMap* ShaderMap, FIntVector FluidVolumeSize, FVector4 ForceParam, FIntVector ForcePos, float ForceRadius, FRDGTextureSRVRef SrcTexture, FRDGTextureUAVRef DstTexture)
	{
		TShaderMapRef<FAddImpluseCS> AddImpluseCS(ShaderMap);
		FAddImpluseCS::FParameters* PassParameters = RDG.AllocParameters<FAddImpluseCS::FParameters>();
		PassParameters->ForceParam = ForceParam;
		PassParameters->ForcePos = ForcePos;
		PassParameters->Radius = ForceRadius;
		PassParameters->SrcTexture = SrcTexture;
		PassParameters->RWDstTexture = DstTexture;

		FComputeShaderUtils::AddPass(RDG, RDG_EVENT_NAME("AddImpluse"), AddImpluseCS, PassParameters, FIntVector(FMath::DivideAndRoundUp(FluidVolumeSize.X, THREAD_GROUP_SIZE), FMath::DivideAndRoundUp(FluidVolumeSize.Y, THREAD_GROUP_SIZE), FMath::DivideAndRoundUp(FluidVolumeSize.Z, THREAD_GROUP_SIZE)));
	}

	void ComputeDivergence(FRDGBuilder& RDG, FGlobalShaderMap* ShaderMap, FIntVector FluidVolumeSize, float Halfrdx, FRDGTextureSRVRef VelocityFieldSRV, FRDGTextureUAVRef DivergenceFieldUAV)
	{
		TShaderMapRef<FDivergenceCS> DivergenceCS(ShaderMap);
		FDivergenceCS::FParameters* PassParameters = RDG.AllocParameters<FDivergenceCS::FParameters>();
		PassParameters->VelocityField = VelocityFieldSRV;
		PassParameters->RWDivergence = DivergenceFieldUAV;

		FComputeShaderUtils::AddPass(RDG, RDG_EVENT_NAME("ComputeDivergence"), DivergenceCS, PassParameters, FIntVector(FMath::DivideAndRoundUp(FluidVolumeSize.X, THREAD_GROUP_SIZE), FMath::DivideAndRoundUp(FluidVolumeSize.Y, THREAD_GROUP_SIZE), FMath::DivideAndRoundUp(FluidVolumeSize.Z, THREAD_GROUP_SIZE)));
	}

	// used to solve poisson equation
	void Jacobi(FRDGBuilder& RDG, FGlobalShaderMap* ShaderMap, FIntVector FluidVolumeSize, int32 IterationCount, FRDGTextureSRVRef x_SRVs[], FRDGTextureUAVRef x_UAVs[], FRDGTextureSRVRef b_SRV)
	{
		TShaderMapRef<FJacobiSolverCS> JacobiCS(ShaderMap);
		uint8 Switcher = 0;
		for (int32 i = 0; i < IterationCount; ++i)
		{
			FJacobiSolverCS::FParameters* PassParameters = RDG.AllocParameters<FJacobiSolverCS::FParameters>();
			PassParameters->PressureField = x_SRVs[Switcher];
			PassParameters->DivergenceField = b_SRV;
			PassParameters->RWPressureField = x_UAVs[(Switcher + 1) & 1];
			FComputeShaderUtils::AddPass(RDG, RDG_EVENT_NAME("JacobiIteration_%d", i), JacobiCS, PassParameters, FIntVector(FMath::DivideAndRoundUp(FluidVolumeSize.X, THREAD_GROUP_SIZE), FMath::DivideAndRoundUp(FluidVolumeSize.Y, THREAD_GROUP_SIZE), FMath::DivideAndRoundUp(FluidVolumeSize.Z, THREAD_GROUP_SIZE)));
			Switcher ^= 1;
		}
	}

	// The final step, u = w - (nabla)p, w is a velocity field with divergence, u is a divergence-free velocity field, now we have got p(pressure field),  
	void SubstarctPressureGradient(FRDGBuilder& RDG, FGlobalShaderMap* ShaderMap, FIntVector FluidVolumeSize, float Halfrdx, FRDGTextureSRVRef VelocityField, FRDGTextureSRVRef PressureField, FRDGTextureUAVRef RWVelocityField)
	{
		TShaderMapRef<FSubstractGradientCS> SubstractGradientCS(ShaderMap);
		FSubstractGradientCS::FParameters* PassParameters = RDG.AllocParameters<FSubstractGradientCS::FParameters>();
		PassParameters->Halfrdx = Halfrdx;
		PassParameters->VelocityField = VelocityField;
		PassParameters->PressureField = PressureField;
		PassParameters->RWVelocityField = RWVelocityField;

		FComputeShaderUtils::AddPass(RDG, RDG_EVENT_NAME("SubstarctPressureGradient"), SubstractGradientCS, PassParameters, FIntVector(FMath::DivideAndRoundUp(FluidVolumeSize.X, THREAD_GROUP_SIZE), FMath::DivideAndRoundUp(FluidVolumeSize.Y, THREAD_GROUP_SIZE), FMath::DivideAndRoundUp(FluidVolumeSize.Z, THREAD_GROUP_SIZE)));
	}
}

//extern void RenderFluidVolume(FRHICommandListImmediate& RHICmdList, const FVolumeFluidProxy& ResourceParam, FTextureRHIRef FluidColor, const FViewInfo* InView);

void UpdateFluid3D(FRHICommandListImmediate& RHICmdList, const FVolumeFluidProxy& ResourceParam, FViewInfo* InView)
{
	check(IsInRenderingThread());

	//if(Scene->GetFrameNumber() <= 1) return;
	
	FPooledRenderTargetDesc FluidVloumeDesc = FPooledRenderTargetDesc::CreateVolumeDesc(ResourceParam.FluidVolumeSize.X, ResourceParam.FluidVolumeSize.Y, ResourceParam.FluidVolumeSize.Z, EPixelFormat::PF_A32B32G32R32F, FClearValueBinding::None, ETextureCreateFlags::TexCreate_None, ETextureCreateFlags::TexCreate_UAV | ETextureCreateFlags::TexCreate_ShaderResource, false);
	FPooledRenderTargetDesc FluidVloumeSingleDesc = FPooledRenderTargetDesc::CreateVolumeDesc(ResourceParam.FluidVolumeSize.X, ResourceParam.FluidVolumeSize.Y, ResourceParam.FluidVolumeSize.Z, EPixelFormat::PF_R32_FLOAT, FClearValueBinding::None, ETextureCreateFlags::TexCreate_None, ETextureCreateFlags::TexCreate_UAV | ETextureCreateFlags::TexCreate_ShaderResource, false);
	TRefCountPtr<IPooledRenderTarget> VelocityTexture3D_0, VelocityTexture3D_1, PressureTexture3D_0, PressureTexture3D_1, ColorTexture3D_0, ColorTexture3D_1, VorticityTexture3D, DivergenceTexture3D;
	GRenderTargetPool.FindFreeElement(RHICmdList, FluidVloumeDesc, VelocityTexture3D_0, TEXT("VelocityTexture3D_0"));
	GRenderTargetPool.FindFreeElement(RHICmdList, FluidVloumeDesc, VelocityTexture3D_1, TEXT("VelocityTexture3D_1"));
	GRenderTargetPool.FindFreeElement(RHICmdList, FluidVloumeSingleDesc, PressureTexture3D_0, TEXT("PressureTexture3D_0"));
	GRenderTargetPool.FindFreeElement(RHICmdList, FluidVloumeSingleDesc, PressureTexture3D_1, TEXT("PressureTexture3D_1"));
	GRenderTargetPool.FindFreeElement(RHICmdList, FluidVloumeDesc, ColorTexture3D_0, TEXT("ColorTexture3D_0"));
	GRenderTargetPool.FindFreeElement(RHICmdList, FluidVloumeDesc, ColorTexture3D_1, TEXT("ColorTexture3D_1"));
	GRenderTargetPool.FindFreeElement(RHICmdList, FluidVloumeDesc, VorticityTexture3D, TEXT("VorticityTexture3D"));
	GRenderTargetPool.FindFreeElement(RHICmdList, FluidVloumeSingleDesc, DivergenceTexture3D, TEXT("DivergenceTexture3D"));

	FRDGBuilder GraphBuilder(RHICmdList);

	FRDGTextureRef VelocityField_0 = GraphBuilder.RegisterExternalTexture(VelocityTexture3D_0, TEXT("VelocityField_0"), ERDGResourceFlags::MultiFrame);
	FRDGTextureRef VelocityField_1 = GraphBuilder.RegisterExternalTexture(VelocityTexture3D_1, TEXT("VelocityField_1"), ERDGResourceFlags::MultiFrame);
	FRDGTextureRef PressureField_0 = GraphBuilder.RegisterExternalTexture(PressureTexture3D_0, TEXT("PressureField_0"), ERDGResourceFlags::MultiFrame);
	FRDGTextureRef PressureField_1 = GraphBuilder.RegisterExternalTexture(PressureTexture3D_1, TEXT("PressureField_1"), ERDGResourceFlags::MultiFrame);
	FRDGTextureRef ColorField_0 = GraphBuilder.RegisterExternalTexture(ColorTexture3D_0, TEXT("ColorField_0"), ERDGResourceFlags::MultiFrame);
	FRDGTextureRef ColorField_1 = GraphBuilder.RegisterExternalTexture(ColorTexture3D_1, TEXT("ColorField_1"), ERDGResourceFlags::MultiFrame);
	FRDGTextureRef Vorticity = GraphBuilder.RegisterExternalTexture(VorticityTexture3D, TEXT("VorticityField"), ERDGResourceFlags::MultiFrame);
	FRDGTextureRef Divergence = GraphBuilder.RegisterExternalTexture(DivergenceTexture3D, TEXT("DivergenceField"), ERDGResourceFlags::MultiFrame);

	FRDGTextureSRVRef VelocityFieldSRV0 = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(VelocityField_0));
	FRDGTextureSRVRef VelocityFieldSRV1 = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(VelocityField_1));
	FRDGTextureUAVRef VelocityFieldUAV0 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(VelocityField_0));
	FRDGTextureUAVRef VelocityFieldUAV1 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(VelocityField_1));

	FRDGTextureSRVRef PressureFieldSRV0 = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(PressureField_0));
	FRDGTextureSRVRef PressureFieldSRV1 = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(PressureField_1));
	FRDGTextureUAVRef PressureFieldUAV0 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(PressureField_0));
	FRDGTextureUAVRef PressureFieldUAV1 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(PressureField_1));

	FRDGTextureSRVRef ColorFieldSRV0 = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(ColorField_0));
	FRDGTextureSRVRef ColorFieldSRV1 = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(ColorField_1));
	FRDGTextureUAVRef ColorFieldUAV0 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ColorField_0));
	FRDGTextureUAVRef ColorFieldUAV1 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ColorField_1));

	FRDGTextureSRVRef VorticitySRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(Vorticity));
	FRDGTextureUAVRef VorticityUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(Vorticity));

	FRDGTextureSRVRef DivergenceSRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(Divergence));
	FRDGTextureUAVRef DivergenceUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(Divergence));

	//UE_LOG(LogTemp, Log, TEXT("Current Feature Level: %d"), (int32)ResourceParam.FeatureLevel);

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(ResourceParam.FeatureLevel);
	// The main steps may have some difference with fluid 2D, because this time we don't need to compute Viscous, so we can reduce a jacobi iteration
	
	// 1. Advect velocity field and color 
	// #TODO it may use a high-order advection scheme which is called MacCormack Advection Scheme 
	FluidSimulation3D::ComputeAdvect(GraphBuilder, ShaderMap, ResourceParam.FluidVolumeSize, ResourceParam.TimeStep, VelocityFieldSRV0, VelocityFieldSRV0, VelocityFieldUAV1);
	FluidSimulation3D::ComputeAdvect(GraphBuilder, ShaderMap, ResourceParam.FluidVolumeSize, ResourceParam.TimeStep, VelocityFieldSRV0, ColorFieldSRV0, ColorFieldUAV1);

	// 2. Apply VorticityConfinement
	FluidSimulation3D::ComputeVorticity(GraphBuilder, ShaderMap, ResourceParam.FluidVolumeSize, 0.5f, VelocityFieldSRV1, VorticityUAV);
	FluidSimulation3D::ComputeVorticityForce(GraphBuilder, ShaderMap, ResourceParam.FluidVolumeSize, 0.5f, ResourceParam.TimeStep, ResourceParam.VorticityScale, VorticitySRV, VelocityFieldSRV1, VelocityFieldUAV0);

	// 3. Apply external force
	FVector4 ForceParam(0, 80, 0, 0);
	FIntVector ForcePos(ResourceParam.FluidVolumeSize.X / 2, 20, ResourceParam.FluidVolumeSize.Z / 2);
	float ForceRadius = 20.f;
	FluidSimulation3D::AddImpluse(GraphBuilder, ShaderMap, ResourceParam.FluidVolumeSize, ForceParam, ForcePos, ForceRadius, VelocityFieldSRV0, VelocityFieldUAV1);
	ForceParam = FVector4(1.f, 1.f, 1.6f, 0.f);
	FluidSimulation3D::AddImpluse(GraphBuilder, ShaderMap, ResourceParam.FluidVolumeSize, ForceParam, ForcePos, ForceRadius, ColorFieldSRV1, ColorFieldUAV0);

	// 4. Compute velocity divergence
	FluidSimulation3D::ComputeDivergence(GraphBuilder, ShaderMap, ResourceParam.FluidVolumeSize, 0.5f, VelocityFieldSRV1, DivergenceUAV);

	// 5. Compute pressure by jacobi iteration
	FRDGTextureSRVRef x_SRVs[2] = { PressureFieldSRV0, PressureFieldSRV1 };
	FRDGTextureUAVRef x_UAVs[2] = { PressureFieldUAV0, PressureFieldUAV1 };
	FluidSimulation3D::Jacobi(GraphBuilder, ShaderMap, ResourceParam.FluidVolumeSize, ResourceParam.IterationCount & ~0x1, x_SRVs, x_UAVs, DivergenceSRV);

	// 6. Project velocity to free-divergence
	FluidSimulation3D::SubstarctPressureGradient(GraphBuilder, ShaderMap, ResourceParam.FluidVolumeSize, 0.5f, VelocityFieldSRV1, PressureFieldSRV0, VelocityFieldUAV0);

	GraphBuilder.Execute();
	
	RenderFluidVolume(RHICmdList, ResourceParam, ColorTexture3D_0->GetRenderTargetItem().TargetableTexture, InView);
}

// After we compute the velocity or density of fluid, we need to render it to screen, but it is more complex than fluid 2D.
//void RenderFluidVolume()
//{
//	
//}

TGlobalResource<FFluidSmiulationManager> GFluidSmiulationManager;

void FVolumeFluidSceneViewExtension::PreRenderView_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView)
{
	FViewInfo* ViewInfo = static_cast<FViewInfo*>(&InView);
	for (int32 i = 0; i < GFluidSmiulationManager.AllFluidProxys.Num(); ++i)
	{
		if (GFluidSmiulationManager.AllFluidProxys[i].IsValid())
		{
			UpdateFluid3D(RHICmdList, *GFluidSmiulationManager.AllFluidProxys[i].Pin(), ViewInfo);
		}
		else
		{
			GFluidSmiulationManager.AllFluidProxys.RemoveAt(i);
			--i;
		}
	}
}

void FVolumeFluidSceneViewExtension::PostRenderBasePass_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView)
{
	
}
