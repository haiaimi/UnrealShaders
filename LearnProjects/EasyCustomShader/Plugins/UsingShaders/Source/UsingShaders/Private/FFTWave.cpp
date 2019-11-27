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
		SHADER_PARAMETER(int32, StartIndex)
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

int32 BitReverse(int32 i, int32 Size)
{
	int32 j = i;
	int32 Sum = 0;
	int32 W = 1;
	int32 M = Size >> 1;
	while (M!= 0)
	{
		j = ((i&M) > M - 1) ? 1 : 0;
		Sum += j * W;
		W <<= 1;
		M >>= 1;
	}
	return Sum;
}

void ComputeButterflyLookuptable(int32 Size, int32 Passes, TArray<float>& OutTable)
{
	OutTable.Reset();
	OutTable.SetNum(Size * Passes * 4);

	for (int32 i = 0; i < Passes; ++i)
	{
		int32 Blocks = FMath::Pow(2, Passes - 1 - i);
		int32 HInputs = FMath::Pow(2, i);

		for (int32 j = 0; j < Blocks; ++j)
		{
			for (int32 k = 0; k < HInputs; ++k)
			{
				int32 i1, i2, j1, j2;
				if (i == 0)
				{
					i1 = j * HInputs * 2 + k;
					i2 = j * HInputs * 2 + HInputs + k;
					j1 = BitReverse(i1, Size);
					j2 = BitReverse(i2, Size);
				}
				else
				{
					i1 = j * HInputs * 2 + k;
					i2 = j * HInputs * 2 + HInputs + k;
					j1 = i1;
					j2 = i2;
				}

				float WR = FMath::Cos(2.f * PI * float(k * Blocks) / Size);
				float WI = FMath::Cos(2.f * PI * float(k * Blocks) / Size);

				int32 Offset1 = 4 * (i1 + i * Size);

				OutTable[Offset1 + 0] = j1;
				OutTable[Offset1 + 1] = j2;
				OutTable[Offset1 + 2] = WR;
				OutTable[Offset1 + 3] = WI;

				int32 Offset2 = 4 * (i2 + i * Size);

				OutTable[Offset2 + 0] = j1;
				OutTable[Offset2 + 1] = j2;
				OutTable[Offset2 + 2] = -WR;
				OutTable[Offset2 + 3] = -WI;
			}
		}
	}
}