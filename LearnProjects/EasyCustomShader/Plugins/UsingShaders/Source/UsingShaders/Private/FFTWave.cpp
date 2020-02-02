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
#include "FFTWaveSimulator.h"
#include "RHI.h"
#include "PipelineStateCache.h"
#include "Common.h"
#include "Engine/TextureRenderTarget.h"

#define WAVE_GROUP_THREAD_COUNTS 4

//BEGIN_SHADER_PARAMETER_STRUCT(FWaveBuffer, )
//SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
//END_SHADER_PARAMETER_STRUCT()
//
//IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FWaveBuffer, "FWaveBuffer"); 

class FPhillipsSpectrumCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPhillipsSpectrumCS, Global)

public:
	FPhillipsSpectrumCS() {};

	FPhillipsSpectrumCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		WaveSize.Bind(Initializer.ParameterMap, TEXT("WaveSize"));
		GridLength.Bind(Initializer.ParameterMap, TEXT("GridLength"));
		WaveAmplitude.Bind(Initializer.ParameterMap, TEXT("WaveAmplitude"));
		WindSpeed.Bind(Initializer.ParameterMap, TEXT("WindSpeed"));
		RWSpectrum.Bind(Initializer.ParameterMap, TEXT("RWSpectrum"));
		RWSpectrumConj.Bind(Initializer.ParameterMap, TEXT("RWSpectrumConj"));
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
		int32 InWaveSize,
		float InGridLength,
		float InWaveAmplitude,
		FVector InWindSpeed,
		FUnorderedAccessViewRHIRef SpectrumUAV,
		FUnorderedAccessViewRHIRef SpectrumConjUAV
	)
	{
		SetShaderValue(RHICmdList, GetComputeShader(), WaveSize, InWaveSize);
		SetShaderValue(RHICmdList, GetComputeShader(), GridLength, InGridLength);
		SetShaderValue(RHICmdList, GetComputeShader(), WaveAmplitude, InWaveAmplitude);
		SetShaderValue(RHICmdList, GetComputeShader(), WindSpeed, InWindSpeed);
		if (RWSpectrum.IsBound())
			RHICmdList.SetUAVParameter(GetComputeShader(), RWSpectrum.GetBaseIndex(), SpectrumUAV);
		if (RWSpectrumConj.IsBound())
			RHICmdList.SetUAVParameter(GetComputeShader(), RWSpectrumConj.GetBaseIndex(), SpectrumConjUAV);
	}

	void UnbindUAV(FRHICommandList& RHICmdList)
	{
		RHICmdList.SetUAVParameter(GetComputeShader(), RWSpectrum.GetBaseIndex(), FUnorderedAccessViewRHIParamRef());
		RHICmdList.SetUAVParameter(GetComputeShader(), RWSpectrumConj.GetBaseIndex(), FUnorderedAccessViewRHIParamRef());
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << WaveSize;
		Ar << GridLength;
		Ar << WaveAmplitude;
		Ar << WindSpeed;
		Ar << RWSpectrum;
		Ar << RWSpectrumConj;

		return bShaderHasOutdatedParameters;
	}

private:
	FShaderParameter WaveSize;
	FShaderParameter GridLength;
	FShaderParameter WaveAmplitude;
	FShaderParameter WindSpeed;

	FShaderResourceParameter RWSpectrum;
	FShaderResourceParameter RWSpectrumConj;
};

IMPLEMENT_SHADER_TYPE(, FPhillipsSpectrumCS,  TEXT("/Plugins/Shaders/Private/FFTWave.usf"), TEXT("PhillipsSpectrumCS"), SF_Compute)

class FPrepareFFTCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPrepareFFTCS, Global)

public:
	FPrepareFFTCS() {};

	FPrepareFFTCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		TimeSeconds.Bind(Initializer.ParameterMap, TEXT("TimeSeconds"));
		WaveSize.Bind(Initializer.ParameterMap, TEXT("WaveSize"));
		GridLength.Bind(Initializer.ParameterMap, TEXT("GridLength"));
		DispersionTable.Bind(Initializer.ParameterMap, TEXT("DispersionTable"));
		Spectrum.Bind(Initializer.ParameterMap, TEXT("Spectrum"));
		SpectrumConj.Bind(Initializer.ParameterMap, TEXT("SpectrumConj"));
		RWHeightBuffer.Bind(Initializer.ParameterMap, TEXT("RWHeightBuffer"));
		RWSlopeBuffer.Bind(Initializer.ParameterMap, TEXT("RWSlopeBuffer"));
		RWDisplacementBuffer.Bind(Initializer.ParameterMap, TEXT("RWDisplacementBuffer"));
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
		float InTimeSeconds,
		int32 InWaveSize,
		float InGridLength,
		FShaderResourceViewRHIParamRef DispersionTableSRV,
		FTextureRHIParamRef InSpectrum,
		FTextureRHIParamRef InSpectrumConj,
		FUnorderedAccessViewRHIRef HeightBufferUAV, 
		FUnorderedAccessViewRHIRef SlopeBufferUAV, 
		FUnorderedAccessViewRHIRef DisplacementBufferUAV
	)
	{
		SetShaderValue(RHICmdList, GetComputeShader(), WaveSize, InWaveSize);
		SetShaderValue(RHICmdList, GetComputeShader(), GridLength, InGridLength);
		SetShaderValue(RHICmdList, GetComputeShader(), TimeSeconds, InTimeSeconds);

		SetSRVParameter(RHICmdList, GetComputeShader(), DispersionTable, DispersionTableSRV);
		SetTextureParameter(RHICmdList, GetComputeShader(), Spectrum, InSpectrum);
		SetTextureParameter(RHICmdList, GetComputeShader(), SpectrumConj, InSpectrumConj);

		if (RWHeightBuffer.IsBound())
			RHICmdList.SetUAVParameter(GetComputeShader(), RWHeightBuffer.GetBaseIndex(), HeightBufferUAV);
		if (RWSlopeBuffer.IsBound())
			RHICmdList.SetUAVParameter(GetComputeShader(), RWSlopeBuffer.GetBaseIndex(), SlopeBufferUAV);
		if (RWDisplacementBuffer.IsBound())
			RHICmdList.SetUAVParameter(GetComputeShader(), RWDisplacementBuffer.GetBaseIndex(), DisplacementBufferUAV);
	}

	void UnbindUAV(FRHICommandList& RHICmdList)
	{
		RHICmdList.SetUAVParameter(GetComputeShader(), RWHeightBuffer.GetBaseIndex(), FUnorderedAccessViewRHIParamRef());
		RHICmdList.SetUAVParameter(GetComputeShader(), RWSlopeBuffer.GetBaseIndex(), FUnorderedAccessViewRHIParamRef());
		RHICmdList.SetUAVParameter(GetComputeShader(), RWDisplacementBuffer.GetBaseIndex(), FUnorderedAccessViewRHIParamRef());
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << TimeSeconds;
		Ar << WaveSize;
		Ar << GridLength;
		Ar << DispersionTable;
		Ar << Spectrum;
		Ar << SpectrumConj;
		Ar << RWHeightBuffer;
		Ar << RWSlopeBuffer;
		Ar << RWDisplacementBuffer;

		return bShaderHasOutdatedParameters;
	}

private:
	FShaderParameter TimeSeconds;
	FShaderParameter WaveSize;
	FShaderParameter GridLength;

	FShaderResourceParameter DispersionTable;
	FShaderResourceParameter Spectrum;
	FShaderResourceParameter SpectrumConj;
	FShaderResourceParameter RWHeightBuffer;
	FShaderResourceParameter RWSlopeBuffer;
	FShaderResourceParameter RWDisplacementBuffer;
};

IMPLEMENT_SHADER_TYPE(, FPrepareFFTCS,  TEXT("/Plugins/Shaders/Private/FFTWave.usf"), TEXT("PrepareFFTCS"), SF_Compute)

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

		HeightBuffer.Bind(Initializer.ParameterMap, TEXT("HeightBuffer"));
		SlopeBuffer.Bind(Initializer.ParameterMap, TEXT("SlopeBuffer"));
		DisplacementBuffer.Bind(Initializer.ParameterMap, TEXT("DisplacementBuffer"));
		InputSampler.Bind(Initializer.ParameterMap, TEXT("BufferSampler"));

		RWHeightBuffer.Bind(Initializer.ParameterMap, TEXT("RWHeightBuffer"));
		RWSlopeBuffer.Bind(Initializer.ParameterMap, TEXT("RWSlopeBuffer"));
		RWDisplacementBuffer.Bind(Initializer.ParameterMap, TEXT("RWDisplacementBuffer"));

		ButterflyLookupTable.Bind(Initializer.ParameterMap, TEXT("ButterflyLookUpTable"));
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
		FShaderResourceViewRHIParamRef InButterflyLookupTable,
		FTextureRHIParamRef InHeightBuffer,
		FTextureRHIParamRef InSlopeBuffer,
		FTextureRHIParamRef InDisplacementBuffer,
		FUnorderedAccessViewRHIRef HeightBufferUAV, 
		FUnorderedAccessViewRHIRef SlopeBufferUAV, 
		FUnorderedAccessViewRHIRef DisplacementBufferUAV
		)
	{
		SetShaderValue(RHICmdList, GetComputeShader(), TimeSeconds, InTimeSeconds);
		SetShaderValue(RHICmdList, GetComputeShader(), WaveSize, InWaveSize);
		SetShaderValue(RHICmdList, GetComputeShader(), StartIndex, InStartIndex);
		//SetShaderValueArray(RHICmdList, GetComputeShader(), ButterflyLookupTable, InButterflyLookupTable.GetData(), InButterflyLookupTable.Num());

		SetTextureParameter(RHICmdList, GetComputeShader(), HeightBuffer, InputSampler, TStaticSamplerState<SF_AnisotropicLinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), InHeightBuffer);
		SetTextureParameter(RHICmdList, GetComputeShader(), SlopeBuffer, InputSampler, TStaticSamplerState<SF_AnisotropicLinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), InSlopeBuffer);
		SetTextureParameter(RHICmdList, GetComputeShader(), DisplacementBuffer, InputSampler, TStaticSamplerState<SF_AnisotropicLinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), InDisplacementBuffer);

		if (RWHeightBuffer.IsBound())
			SetUAVParameter(RHICmdList, GetComputeShader(), RWHeightBuffer, HeightBufferUAV);
		if (RWSlopeBuffer.IsBound())
			SetUAVParameter(RHICmdList, GetComputeShader(), RWSlopeBuffer, SlopeBufferUAV);
		if (RWDisplacementBuffer.IsBound())
			SetUAVParameter(RHICmdList, GetComputeShader(), RWDisplacementBuffer, DisplacementBufferUAV);

		SetSRVParameter(RHICmdList, GetComputeShader(), ButterflyLookupTable, InButterflyLookupTable);
	}

	//UAV需要解绑，以便其他地方使用
	void UnbindUAV(FRHICommandList& RHICmdList)
	{
		if (RWHeightBuffer.IsBound())
			RHICmdList.SetUAVParameter(GetComputeShader(), RWHeightBuffer.GetBaseIndex(), FUnorderedAccessViewRHIParamRef());
		if (RWSlopeBuffer.IsBound())
			RHICmdList.SetUAVParameter(GetComputeShader(), RWSlopeBuffer.GetBaseIndex(), FUnorderedAccessViewRHIParamRef());
		if (RWDisplacementBuffer.IsBound())
			RHICmdList.SetUAVParameter(GetComputeShader(), RWDisplacementBuffer.GetBaseIndex(), FUnorderedAccessViewRHIParamRef());
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << TimeSeconds;
		Ar << WaveSize; 
		Ar << StartIndex;
		Ar << HeightBuffer;
		Ar << SlopeBuffer;
		Ar << DisplacementBuffer;
		Ar << RWHeightBuffer;
		Ar << RWSlopeBuffer;
		Ar << RWDisplacementBuffer;
		Ar << ButterflyLookupTable;
		return bShaderHasOutdatedParameters;
	}

private:
	FShaderParameter TimeSeconds;
	FShaderParameter WaveSize;
	FShaderParameter StartIndex;

	FShaderResourceParameter ButterflyLookupTable;

	FShaderResourceParameter HeightBuffer;
	FShaderResourceParameter SlopeBuffer;
	FShaderResourceParameter DisplacementBuffer;
	FShaderResourceParameter InputSampler;

	FShaderResourceParameter RWHeightBuffer;
	FShaderResourceParameter RWSlopeBuffer;
	FShaderResourceParameter RWDisplacementBuffer;
};

IMPLEMENT_SHADER_TYPE(template<>, FWaveFFTCS<1>,  TEXT("/Plugins/Shaders/Private/FFTWave.usf"), TEXT("PerformFFTCS1"), SF_Compute)
IMPLEMENT_SHADER_TYPE(template<>, FWaveFFTCS<2>,  TEXT("/Plugins/Shaders/Private/FFTWave.usf"), TEXT("PerformFFTCS2"), SF_Compute)

class FComputePosAndNormalShader : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FComputePosAndNormalShader, Global)

public:
	FComputePosAndNormalShader() {};

	FComputePosAndNormalShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		WaveSize.Bind(Initializer.ParameterMap, TEXT("WaveSize"));
		TextureSampler.Bind(Initializer.ParameterMap, TEXT("TextureSampler"));
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
	}

	void SetParameters(
		FRHICommandListImmediate& RHICmdList,
		int32 InWaveSize,
		FTexture2DRHIParamRef InHeightBuffer,
		FTexture2DRHIParamRef InSlopeBuffer,
		FTexture2DRHIParamRef InDisplacementBuffer
	)
	{
		SetShaderValue(RHICmdList, GetPixelShader(), WaveSize, WaveSize);
		SetSamplerParameter(RHICmdList, GetPixelShader(), TextureSampler, TStaticSamplerState<SF_AnisotropicLinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
		SetTextureParameter(RHICmdList, GetPixelShader(), HeightBuffer, InHeightBuffer);
		SetTextureParameter(RHICmdList, GetPixelShader(), SlopeBuffer, InSlopeBuffer);
		SetTextureParameter(RHICmdList, GetPixelShader(), DisplacementBuffer, InDisplacementBuffer);
	}

	void UnbindUAV(FRHICommandList& RHICmdList)
	{
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);

		Ar << WaveSize;
		Ar << HeightBuffer;
		Ar << SlopeBuffer;
		Ar << DisplacementBuffer;

		return bShaderHasOutdatedParameters;
	}

private:
	FShaderParameter WaveSize;

	FShaderResourceParameter HeightBuffer;
	FShaderResourceParameter SlopeBuffer;
	FShaderResourceParameter DisplacementBuffer;

	FShaderResourceParameter TextureSampler;
};

class FComputePosAndNormalVS :public FComputePosAndNormalShader
{
	DECLARE_SHADER_TYPE(FComputePosAndNormalVS, Global);

public:
	FComputePosAndNormalVS() {}

	FComputePosAndNormalVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FComputePosAndNormalShader(Initializer)
	{
	}
};

class FComputePosAndNormalPS :public FComputePosAndNormalShader
{
	DECLARE_SHADER_TYPE(FComputePosAndNormalPS, Global);

public:
	FComputePosAndNormalPS() {}

	FComputePosAndNormalPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FComputePosAndNormalShader(Initializer)
	{
	}
};

IMPLEMENT_SHADER_TYPE(, FComputePosAndNormalVS, TEXT("/Plugins/Shaders/Private/FFTWave.usf"), TEXT("ComputePosAndNormalVS"), SF_Vertex)
IMPLEMENT_SHADER_TYPE(, FComputePosAndNormalPS, TEXT("/Plugins/Shaders/Private/FFTWave.usf"), TEXT("ComputePosAndNormalPS"), SF_Pixel)

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

// Compute the butterfly lookup table for FFT
extern void ComputeButterflyLookuptable(int32 Size, int32 Passes, TArray<float>& OutTable)
{
	OutTable.Reset();
	OutTable.SetNum(Size * Passes * 4);

	for (int32 i = 0; i < Passes; ++i)
	{
		int32 Blocks = FMath::Pow(2, Passes - 1 - i);
		int32 HInputs = FMath::Pow(2, i);

		for (int32 j = 0; j < Blocks; ++j) //按奇偶分出的块数的一半
		{
			for (int32 k = 0; k < HInputs; ++k)  //每一块里面的数目
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
				float WI = FMath::Sin(2.f * PI * float(k * Blocks) / Size);
				
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

static void ComputePhillipsSpecturm_RenderThread(
	FRHICommandListImmediate& RHICmdList,
	ERHIFeatureLevel::Type FeatureLevel,
	int32 WaveSize,
	float GridLength,
    float WaveAmplitude,
	FVector WindSpeed,
	FUnorderedAccessViewRHIRef Spectrum,
	FUnorderedAccessViewRHIRef SpectrumConj)
{
	TShaderMapRef<FPhillipsSpectrumCS> PhillipsSpecturmShader(GetGlobalShaderMap(FeatureLevel));

	RHICmdList.SetComputeShader(PhillipsSpecturmShader->GetComputeShader());
	PhillipsSpecturmShader->SetParameters(RHICmdList, WaveSize, GridLength, WaveAmplitude, WindSpeed, Spectrum, SpectrumConj);
	DispatchComputeShader(RHICmdList, *PhillipsSpecturmShader, FMath::DivideAndRoundUp(WaveSize + 1, WAVE_GROUP_THREAD_COUNTS), FMath::DivideAndRoundUp(WaveSize + 1, WAVE_GROUP_THREAD_COUNTS), 1); 
	PhillipsSpecturmShader->UnbindUAV(RHICmdList);
}

static void PrepareFFT_RenderThread(
	FRHICommandListImmediate& RHICmdList,
	ERHIFeatureLevel::Type FeatureLevel,
	float TimeSeconds,
	int32 WaveSize,
	float GridLength,
	FShaderResourceViewRHIParamRef DispersionTableSRV,
	FTexture2DRHIParamRef Spectrum,
	FTexture2DRHIParamRef SpectrumConj,
	FUnorderedAccessViewRHIRef HeightBufferUAV, 
	FUnorderedAccessViewRHIRef SlopeBufferUAV, 
	FUnorderedAccessViewRHIRef DisplacementBufferUAV
)
{
	check(IsInRenderingThread());

	TShaderMapRef<FPrepareFFTCS> PrepareFFTShader(GetGlobalShaderMap(FeatureLevel));

	RHICmdList.SetComputeShader(PrepareFFTShader->GetComputeShader());
	PrepareFFTShader->SetParameters(RHICmdList, TimeSeconds, WaveSize, GridLength, DispersionTableSRV, Spectrum, SpectrumConj, HeightBufferUAV, SlopeBufferUAV, DisplacementBufferUAV);
	DispatchComputeShader(RHICmdList, *PrepareFFTShader, FMath::DivideAndRoundUp(WaveSize, WAVE_GROUP_THREAD_COUNTS), FMath::DivideAndRoundUp(WaveSize, WAVE_GROUP_THREAD_COUNTS), 1); 
	PrepareFFTShader->UnbindUAV(RHICmdList);
}

static void EvaluateWavesFFT_RenderThread(
	FRHICommandListImmediate& RHICmdList,
	ERHIFeatureLevel::Type FeatureLevel,
	float TimeSeconds,
	int32 WaveSize,
	int32 StartIndex,
	FTexture2DRHIParamRef HeightBuffer,
	FTexture2DRHIParamRef SlopeBuffer,
	FTexture2DRHIParamRef DisplacementBuffer,
	FUnorderedAccessViewRHIRef HeightBufferUAV,
	FUnorderedAccessViewRHIRef SlopeBufferUAV,
	FUnorderedAccessViewRHIRef DisplacementBufferUAV,
	TWeakObjectPtr<AFFTWaveSimulator> WaveSimulator)
{
	check(IsInRenderingThread());

	if (!WaveSimulator.IsValid())
		return;
	TShaderMapRef<FWaveFFTCS<1>> WaveFFTCS1(GetGlobalShaderMap(FeatureLevel));
	TShaderMapRef<FWaveFFTCS<2>> WaveFFTCS2(GetGlobalShaderMap(FeatureLevel));

	FShaderResourceViewRHIRef IndirectShadowCapsuleShapesSRV;

	int32 Passes = FMath::RoundToInt(FMath::Log2(WaveSize));
	for (int32 i = 0; i < Passes; ++i)
	{
		RHICmdList.SetComputeShader(WaveFFTCS1->GetComputeShader());
		WaveFFTCS1->SetParameters(RHICmdList, TimeSeconds, WaveSize, i, WaveSimulator->ButterflyLookupTableSRV, HeightBuffer, SlopeBuffer, DisplacementBuffer, HeightBufferUAV, SlopeBufferUAV, DisplacementBufferUAV);
		DispatchComputeShader(RHICmdList, *WaveFFTCS1, FMath::DivideAndRoundUp(WaveSize, WAVE_GROUP_THREAD_COUNTS), FMath::DivideAndRoundUp(WaveSize, WAVE_GROUP_THREAD_COUNTS), 1);
		WaveFFTCS1->UnbindUAV(RHICmdList);
	}

	for (int32 i = 0; i < Passes; ++i)
	{
		RHICmdList.SetComputeShader(WaveFFTCS2->GetComputeShader());
		WaveFFTCS2->SetParameters(RHICmdList, TimeSeconds, WaveSize, i, WaveSimulator->ButterflyLookupTableSRV, HeightBuffer, SlopeBuffer, DisplacementBuffer, HeightBufferUAV, SlopeBufferUAV, DisplacementBufferUAV);
		DispatchComputeShader(RHICmdList, *WaveFFTCS2, FMath::DivideAndRoundUp(WaveSize, WAVE_GROUP_THREAD_COUNTS), FMath::DivideAndRoundUp(WaveSize, WAVE_GROUP_THREAD_COUNTS), 1);
		WaveFFTCS2->UnbindUAV(RHICmdList);
	}
}

static void ComputePosAndNormal_RenderThread(
	FRHICommandListImmediate& RHICmdList,
	ERHIFeatureLevel::Type FeatureLevel,
	FTextureRenderTargetResource* OutputPosRenderTargetResource,
	FTextureRenderTargetResource* OutputNormalRenderTargetResource,
	int32 InWaveSize,
	FTexture2DRHIParamRef HeightBuffer,
	FTexture2DRHIParamRef SlopeBuffer,
	FTexture2DRHIParamRef DisplacementBuffer
)
{
	check(IsInRenderingThread());

	TShaderMapRef<FComputePosAndNormalVS> ComputePosAndNormalVS(GetGlobalShaderMap(FeatureLevel));
	TShaderMapRef<FComputePosAndNormalPS> ComputePosAndNormalPS(GetGlobalShaderMap(FeatureLevel));

	if (OutputPosRenderTargetResource)
	{
		FIntPoint RTSize = OutputPosRenderTargetResource->GetSizeXY();
		FRHIResourceCreateInfo RHIResourceCreateInfo;
		RHICmdList.SetViewport(0.f, 0.f, 0.f, RTSize.X, RTSize.Y, 1.f);
		FRHITexture* ColorRTs[2] = { OutputPosRenderTargetResource->GetRenderTargetTexture(), OutputNormalRenderTargetResource->GetRenderTargetTexture() };
		FRHIRenderPassInfo PassInfo(2, ColorRTs, ERenderTargetActions::Load_Store);
		//FRHIRenderPassInfo PassInfo(OutputRenderTargetResource->GetRenderTargetTexture(), ERenderTargetActions::Load_Store, OutputRenderTargetResource->TextureRHI);
		RHICmdList.BeginRenderPass(PassInfo, TEXT("ComputeWavePosPass"));
		FCommonVertexDeclaration VertexDeclaration;
		VertexDeclaration.InitRHI();

		FGraphicsPipelineStateInitializer GraphicPSPoint;
		RHICmdList.ApplyCachedRenderTargets(GraphicPSPoint);
		GraphicPSPoint.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		GraphicPSPoint.BlendState = TStaticBlendState<>::GetRHI();
		GraphicPSPoint.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicPSPoint.PrimitiveType = PT_TriangleList;        //绘制的图元类型
		GraphicPSPoint.BoundShaderState.VertexDeclarationRHI = VertexDeclaration.VertexDeclarationRHI;
		GraphicPSPoint.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*ComputePosAndNormalVS);
		GraphicPSPoint.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*ComputePosAndNormalPS);
		SetGraphicsPipelineState(RHICmdList, GraphicPSPoint);

		ComputePosAndNormalPS->SetParameters(RHICmdList, InWaveSize, HeightBuffer, SlopeBuffer, DisplacementBuffer);

		FUVertexInput Vertices[4];
		Vertices[0].Position.Set(-1.0f, 1.0f, 0, 1.0f);
		Vertices[1].Position.Set(1.0f, 1.0f, 0, 1.0f);
		Vertices[2].Position.Set(-1.0f, -1.0f, 0, 1.0f);
		Vertices[3].Position.Set(1.0f, -1.0f, 0, 1.0f);
		Vertices[0].UV = FVector2D(0.0f, 1.0f);
		Vertices[1].UV = FVector2D(1.0f, 1.0f);
		Vertices[2].UV = FVector2D(0.0f, 0.0f);
		Vertices[3].UV = FVector2D(1.0f, 0.0f);
		static const uint16 Indices[6] =  
		{  
			0, 1, 2,  
			2, 1, 3  
		};  

		DrawIndexedPrimitiveUP_Custom(RHICmdList, PT_TriangleList, 0, ARRAY_COUNT(Vertices), 2, Indices, sizeof(Indices[0]), Vertices, sizeof(Vertices[0]));

		RHICmdList.EndRenderPass();
	}
}

void AFFTWaveSimulator::ComputeSpectrum()
{
	ENQUEUE_RENDER_COMMAND(CaptureCommand)([this](FRHICommandListImmediate& RHICmdList)
	{
		FRHIResourceCreateInfo RHIResourceCreateInfo;
	
		/// Test texture stride
		/*FTexture2DRHIRef TestTexture = RHICreateTexture2D(256, 3, PF_R32_FLOAT, 1, 1, TexCreate_ShaderResource | TexCreate_UAV, RHIResourceCreateInfo);
		uint32 Stride;
		int32* Data = static_cast<int32*>(RHILockTexture2D(TestTexture, 0, EResourceLockMode::RLM_WriteOnly, Stride, false));
		*Data++ = 1;
		*Data++ = 2;
		*Data++ = 3;
		*Data++ = 4;
		RHIUnlockTexture2D(TestTexture, 0, false);

		int8* Data2 = static_cast<int8*>(RHILockTexture2D(TestTexture, 0, EResourceLockMode::RLM_ReadOnly, Stride, false));
		int32 a = *(int32*)Data2;
		int32 b = *(int32*)(Data2 + 4);
		Data2 += Stride;
		int32 c = *(int32*)Data2;
		int32 d = *(int32*)(Data2 + 4);
		RHIUnlockTexture2D(TestTexture, 0, false);*/

		UWorld* World = GetWorld();
		ERHIFeatureLevel::Type FeatureLevel = World->Scene->GetFeatureLevel();
		FUnorderedAccessViewRHIRef SpectrumUAV;
		FUnorderedAccessViewRHIRef SpectrumConjUAV;

		CreateResources();

		if (Spectrum->IsValid())
		{
			SpectrumUAV = RHICreateUnorderedAccessView(Spectrum);
		}

		if (SpectrumConj->IsValid())
		{
			SpectrumConjUAV = RHICreateUnorderedAccessView(SpectrumConj);
		}
		ComputePhillipsSpecturm_RenderThread(RHICmdList, FeatureLevel, WaveSize, GridLength, WaveAmplitude, WindSpeed, SpectrumUAV, SpectrumConjUAV);
	});
}

void AFFTWaveSimulator::PrepareForFFT(float TimeSeconds)
{
	float KX, KY, Len, Lambda = -1.f;
	int32 Index;
	uint32 Stride;
	FVector2D* HeightBufferData = static_cast<FVector2D*>(RHILockTexture2D(HeightBuffer, 0, EResourceLockMode::RLM_WriteOnly, Stride, false));
	HeightBufferData += Stride / sizeof(FVector2D);
	FVector4* SlopeBufferData = static_cast<FVector4*>(RHILockTexture2D(SlopeBuffer, 0, EResourceLockMode::RLM_WriteOnly, Stride, false));
	SlopeBufferData += Stride / sizeof(FVector4);
	FVector4* DisplacementBufferData = static_cast<FVector4*>(RHILockTexture2D(DisplacementBuffer, 0, EResourceLockMode::RLM_WriteOnly, Stride, false));
	DisplacementBufferData += Stride / sizeof(FVector4);

	//Test buffer data
	TArray<FVector2D> HeightData;
	for (int32 i = 0; i < WaveSize * WaveSize; ++i)
	{
		HeightData.Add(HeightBufferData[i]);
	}

	for (int32 m = 0; m < WaveSize; ++m)
	{
		KY = PI * (2.f*m - WaveSize) / GridLength;
		for (int32 n = 0; n < WaveSize; ++n)
		{
			KX = PI * (2.f*n - WaveSize) / GridLength;
			Len = FMath::Sqrt(KX * KX + KY * KY);
			Index = m * WaveSize + n;

			FVector2D C = InitSpectrum(TimeSeconds, n, m);

			HeightBufferData[Index].X = C.X;
			HeightBufferData[Index].Y = C.Y;

			SlopeBufferData[Index].X = -C.Y * KX;
			SlopeBufferData[Index].Z = C.X * KX;
			SlopeBufferData[Index].Y = -C.Y * KY;
			SlopeBufferData[Index].Z = C.X * KY;

			if (Len < 0.000001f)
			{
				DisplacementBufferData[Index].X = 0.f;
				DisplacementBufferData[Index].Y = 0.f;
				DisplacementBufferData[Index].Z = 0.f;
				DisplacementBufferData[Index].W = 0.f;
			}
			else
			{
				DisplacementBufferData[Index].X = -C.Y * -(KX / Len);
				DisplacementBufferData[Index].Y = C.X * -(KX / Len);
				DisplacementBufferData[Index].Z = -C.Y * -(KY / Len);
				DisplacementBufferData[Index].W = C.X * -(KY / Len);
			}
		}
	}

	// Unlock the buffers
	RHIUnlockTexture2D(HeightBuffer, 0, false);
	RHIUnlockTexture2D(SlopeBuffer, 0, false);
	RHIUnlockTexture2D(DisplacementBuffer, 0, false);
}

void AFFTWaveSimulator::EvaluateWavesFFT(float TimeSeconds)
{
	UWorld* World = GetWorld();
	ERHIFeatureLevel::Type FeatureLevel = World->Scene->GetFeatureLevel();

	TWeakObjectPtr<AFFTWaveSimulator> WaveSimulatorPtr(this);

	ENQUEUE_RENDER_COMMAND(CaptureCommand)([FeatureLevel, TimeSeconds, WaveSimulatorPtr](FRHICommandListImmediate& RHICmdList)
	{
		if (WaveSimulatorPtr.Get())
		{
			FUnorderedAccessViewRHIRef HeightBufferUAV;
			FUnorderedAccessViewRHIRef SlopeBufferUAV;
			FUnorderedAccessViewRHIRef DisplacementBufferUAV;
			if (WaveSimulatorPtr->HeightBuffer.GetReference())
			{
				HeightBufferUAV = RHICreateUnorderedAccessView(WaveSimulatorPtr->HeightBuffer);
			}

			if (WaveSimulatorPtr->SlopeBuffer.GetReference())
			{
				SlopeBufferUAV = RHICreateUnorderedAccessView(WaveSimulatorPtr->SlopeBuffer);
			}

			if (WaveSimulatorPtr->SlopeBuffer.GetReference())
			{
				DisplacementBufferUAV = RHICreateUnorderedAccessView(WaveSimulatorPtr->DisplacementBuffer);
			}
			PrepareFFT_RenderThread(RHICmdList, FeatureLevel, TimeSeconds, WaveSimulatorPtr->WaveSize, WaveSimulatorPtr->GridLength, WaveSimulatorPtr->DispersionTableSRV, WaveSimulatorPtr->Spectrum, WaveSimulatorPtr->SpectrumConj, HeightBufferUAV, SlopeBufferUAV, DisplacementBufferUAV);
			EvaluateWavesFFT_RenderThread(RHICmdList, FeatureLevel, TimeSeconds, WaveSimulatorPtr->WaveSize, 0, WaveSimulatorPtr->HeightBuffer, WaveSimulatorPtr->SlopeBuffer,WaveSimulatorPtr->DisplacementBuffer, HeightBufferUAV, SlopeBufferUAV, DisplacementBufferUAV, WaveSimulatorPtr);
		
			if (WaveSimulatorPtr->WaveHeightMapRenderTarget && WaveSimulatorPtr->WaveNormalRenderTarget)
				ComputePosAndNormal_RenderThread(RHICmdList, FeatureLevel, WaveSimulatorPtr->WaveHeightMapRenderTarget->GetRenderTargetResource(), WaveSimulatorPtr->WaveNormalRenderTarget->GetRenderTargetResource(), WaveSimulatorPtr->WaveSize, WaveSimulatorPtr->HeightBuffer, WaveSimulatorPtr->SlopeBuffer, WaveSimulatorPtr->DisplacementBuffer);
		}
	});
}