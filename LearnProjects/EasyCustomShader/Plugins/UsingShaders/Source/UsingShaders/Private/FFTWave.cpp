#include "FFTWave.h"
#include "GlobalShader.h"
#include "ShaderParameterUtils.h"
#include "RHIStaticStates.h"
#include "Engine/Texture.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "SceneInterface.h"
#include "RHICommandList.h"
#include "Engine/Engine.h"
#include "Engine/Public/SceneView.h"
#include "ShaderParameterStruct.h"

class FWaveFFTComputeShader : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FWaveFFTComputeShader, Global)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector2D, ViewResolution)
		SHADER_PARAMETER(float, TimeSeconds)
		SHADER_PARAMETER(int32, WaveSize)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, HeightBuffer)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, SlopeBuffer)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, DisplacementBuffer)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, Spectrum)
	END_SHADER_PARAMETER_STRUCT()

public:
	
	FWaveFFTComputeShader() {}

	FWaveFFTComputeShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		ButterflyLookupTable.Bind(Initializer.ParameterMap, TEXT("ButterflyLookUpTable"));
		InputSurface.Bind(Initializer.ParameterMap, TEXT("InputTexture"));
		InputSampler.Bind(Initializer.ParameterMap, TEXT("TextureSampler"));
		OutputSurface.Bind(Initializer.ParameterMap, TEXT("OutputTexture"));
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
		OutEnvironment.SetDefine(TEXT("BLUR_MICRO"), 1);
	}

	void SetParameters(FRHICommandListImmediate& RHICmdList, const FTextureRHIParamRef& InTexture, FUnorderedAccessViewRHIRef OutputUAV, const TArray<FVector4, TInlineAllocator<4>>& InButterflyLookupTable)
	{
		if (OutputSurface.IsBound())
			RHICmdList.SetUAVParameter(GetComputeShader(), OutputSurface.GetBaseIndex(), OutputUAV);


		SetShaderValueArray(RHICmdList, GetComputeShader(), ButterflyLookupTable, InButterflyLookupTable.GetData(), InButterflyLookupTable.Num());
		SetTextureParameter(RHICmdList, GetComputeShader(), InputSurface, InputSampler, TStaticSamplerState<SF_AnisotropicLinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), InTexture);

		//SetUniformBufferParameter(RHICmdList, GetPixelShader(), GetUniformBufferParameter<FBlurComputeShaderData>(), FBlurComputeShaderData::CreateUniformBuffer(BlurData, EUniformBufferUsage::UniformBuffer_SingleDraw));
	}

	//UAV需要解绑，以便其他地方使用
	void UnbindUAV(FRHICommandList& RHICmdList)
	{
		if (OutputSurface.IsBound())
			RHICmdList.SetUAVParameter(GetComputeShader(), OutputSurface.GetBaseIndex(), FUnorderedAccessViewRHIParamRef());
		if (InputSurface.IsBound())
			RHICmdList.SetShaderTexture(GetComputeShader(), InputSurface.GetBaseIndex(), FTextureRHIParamRef());
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << InputSurface << OutputSurface;
		return bShaderHasOutdatedParameters;
	}

private:
	FShaderParameter ButterflyLookupTable;

	FShaderResourceParameter InputSurface;

	FShaderResourceParameter InputSampler;

	FShaderResourceParameter OutputSurface;
};