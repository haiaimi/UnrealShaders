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

class FPhillipsSpectrumCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPhillipsSpectrumCS, Global)

public:
	FPhillipsSpectrumCS() {};

	FPhillipsSpectrumCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		WindDirection.Bind(Initializer.ParameterMap, TEXT("WindDirection"));
		Spectrum.Bind(Initializer.ParameterMap, TEXT("Spectrum"));
	}

	void SetParameters(
		FRHICommandListImmediate& RHICmdList,
		int32 InWaveSize,
		FVector InWindDirection,
		FUnorderedAccessViewRHIRef SpectrumUAV
	)
	{
		SetShaderValue(RHICmdList, GetComputeShader(), WindDirection, InWindDirection);
		if (Spectrum.IsBound())
			RHICmdList.SetUAVParameter(GetComputeShader(), Spectrum.GetBaseIndex(), SpectrumUAV);
	}

	void UnbindUAV(FRHICommandList& RHICmdList)
	{
			RHICmdList.SetUAVParameter(GetComputeShader(), Spectrum.GetBaseIndex(), FUnorderedAccessViewRHIParamRef());
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << Spectrum;

		return bShaderHasOutdatedParameters;
	}

private:
	FShaderResourceParameter Spectrum;
	FShaderParameter WindDirection;
};

template<int T>
class FWaveFFTCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FWaveFFTCS, Global)

	//BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
	//	SHADER_PARAMETER(FVector2D, ViewResolution)
	//	SHADER_PARAMETER(float, TimeSeconds)
	//	SHADER_PARAMETER(int32, WaveSize)
	//	SHADER_PARAMETER(int32, StartIndex)
	//	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	//	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, HeightBuffer)
	//	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, SlopeBuffer)
	//	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, DisplacementBuffer)
	//	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, Spectrum)
	//END_SHADER_PARAMETER_STRUCT()

public:
	
	FWaveFFTCS() {}

	FWaveFFTCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		TimeSeconds.Bind(Initializer.ParameterMap, TEXT("TimeSeconds"));
		WaveSize.Bind(Initializer.ParameterMap, TEXT("WaveSize"));
		StartIndex.Bind(Initializer.ParameterMap, TEXT("StartIndex"));
		ButterflyLookupTable.Bind(Initializer.ParameterMap, TEXT("ButterflyLookUpTable"));

		HeightBuffer.Bind(Initializer.ParameterMap, TEXT("HeightBuffer"));
		SlopeBuffer.Bind(Initializer.ParameterMap, TEXT("SlopeBuffer"));
		DisplacementBuffer.Bind(Initializer.ParameterMap, TEXT("DisplacementBuffer"));
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
		//OutEnvironment.SetDefine(TEXT("BLUR_MICRO"), 1);
	}

	void SetParameters(
		FRHICommandListImmediate& RHICmdList, 
		float InTimeSeconds,
		int32 InWaveSize,
		int32 InStartIndex,
		const TArray<FVector4, TInlineAllocator<4>>& InButterflyLookupTable,
		FUnorderedAccessViewRHIRef HeightBufferUAV, 
		FUnorderedAccessViewRHIRef SlopeBufferUAV, 
		FUnorderedAccessViewRHIRef DisplacementBufferUAV
		)
	{
		SetShaderValue(RHICmdList, GetComputeShader(), TimeSeconds, InTimeSeconds);
		SetShaderValue(RHICmdList, GetComputeShader(), WaveSize, InWaveSize);
		SetShaderValue(RHICmdList, GetComputeShader(), StartIndex, InStartIndex);
		SetShaderValueArray(RHICmdList, GetComputeShader(), ButterflyLookupTable, InButterflyLookupTable.GetData(), InButterflyLookupTable.Num());

		if (HeightBuffer.IsBound())
			RHICmdList.SetUAVParameter(GetComputeShader(), HeightBuffer.GetBaseIndex(), HeightBufferUAV);
		if (SlopeBuffer.IsBound())
			RHICmdList.SetUAVParameter(GetComputeShader(), SlopeBuffer.GetBaseIndex(), SlopeBufferUAV);
		if (DisplacementBuffer.IsBound())
			RHICmdList.SetUAVParameter(GetComputeShader(), DisplacementBuffer.GetBaseIndex(), DisplacementBufferUAV);
	}

	//UAV需要解绑，以便其他地方使用
	void UnbindUAV(FRHICommandList& RHICmdList)
	{
		if (HeightBuffer.IsBound())
			RHICmdList.SetUAVParameter(GetComputeShader(), HeightBuffer.GetBaseIndex(), FUnorderedAccessViewRHIParamRef());
		if (SlopeBuffer.IsBound())
			RHICmdList.SetUAVParameter(GetComputeShader(), SlopeBuffer.GetBaseIndex(), FUnorderedAccessViewRHIParamRef());
		if (DisplacementBuffer.IsBound())
			RHICmdList.SetUAVParameter(GetComputeShader(), DisplacementBuffer.GetBaseIndex(), FUnorderedAccessViewRHIParamRef());
		if (Spectrum.IsBound())
			RHICmdList.SetUAVParameter(GetComputeShader(), Spectrum.GetBaseIndex(), FUnorderedAccessViewRHIParamRef());
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << TimeSeconds;
		Ar << WaveSize; 
		Ar << StartIndex;
		Ar << ButterflyLookupTable;
		Ar << HeightBuffer;
		Ar << SlopeBuffer;
		Ar << DisplacementBuffer;
		return bShaderHasOutdatedParameters;
	}

private:
	FShaderParameter TimeSeconds;
	FShaderParameter WaveSize;
	FShaderParameter StartIndex;
	FShaderParameter ButterflyLookupTable;

	FShaderResourceParameter HeightBuffer;
	FShaderResourceParameter SlopeBuffer;
	FShaderResourceParameter DisplacementBuffer;
};

IMPLEMENT_SHADER_TYPE(template<>, FWaveFFTCS<1>,  TEXT("/Plugins/Shaders/Private/FFTWaveShader.usf"), TEXT("PerformFFTCS1"), SF_Compute)
IMPLEMENT_SHADER_TYPE(template<>, FWaveFFTCS<1>,  TEXT("/Plugins/Shaders/Private/FFTWaveShader.usf"), TEXT("PerformFFTCS2"), SF_Compute)

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