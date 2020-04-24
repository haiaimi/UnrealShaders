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
#include "RHIUtilities.h"

#define WAVE_GROUP_THREAD_COUNTS 8

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
		RandomTable.Bind(Initializer.ParameterMap, TEXT("RandomTable"));
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
		OutEnvironment.SetDefine(TEXT("THREAD_GROUP_SIZE"), WAVE_GROUP_THREAD_COUNTS);
	}

	void SetParameters(
		FRHICommandListImmediate& RHICmdList,
		int32 InWaveSize,
		float InGridLength,
		float InWaveAmplitude,
		FVector InWindSpeed,
		FRHIShaderResourceView* RandomTableSRV,
		FRHIUnorderedAccessView* InSpectrum,
		FRHIUnorderedAccessView* InSpectrumConj
	)
	{
		SetShaderValue(RHICmdList, GetComputeShader(), WaveSize, InWaveSize);
		SetShaderValue(RHICmdList, GetComputeShader(), GridLength, InGridLength);
		SetShaderValue(RHICmdList, GetComputeShader(), WaveAmplitude, InWaveAmplitude);
		SetShaderValue(RHICmdList, GetComputeShader(), WindSpeed, InWindSpeed);
		SetSRVParameter(RHICmdList, GetComputeShader(), RandomTable, RandomTableSRV);
		//if (RWSpectrum.IsBound())
			RHICmdList.SetUAVParameter(GetComputeShader(), RWSpectrum.GetBaseIndex(), InSpectrum);
		//if (RWSpectrumConj.IsBound())
			RHICmdList.SetUAVParameter(GetComputeShader(), RWSpectrumConj.GetBaseIndex(), InSpectrumConj);
	}

	void UnbindUAV(FRHICommandList& RHICmdList)
	{
		RHICmdList.SetUAVParameter(GetComputeShader(), RWSpectrum.GetBaseIndex(), nullptr);
		RHICmdList.SetUAVParameter(GetComputeShader(), RWSpectrumConj.GetBaseIndex(), nullptr);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << WaveSize;
		Ar << GridLength;
		Ar << WaveAmplitude;
		Ar << WindSpeed;
		Ar << RandomTable;
		Ar << RWSpectrum;
		Ar << RWSpectrumConj;

		return bShaderHasOutdatedParameters;
	}

private:
	FShaderParameter WaveSize;
	FShaderParameter GridLength;
	FShaderParameter WaveAmplitude;
	FShaderParameter WindSpeed;

	FShaderResourceParameter RandomTable;
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
		OutEnvironment.SetDefine(TEXT("THREAD_GROUP_SIZE"), WAVE_GROUP_THREAD_COUNTS);
	}

	void SetParameters(
		FRHICommandListImmediate& RHICmdList,
		float InTimeSeconds,
		int32 InWaveSize,
		float InGridLength,
		FRHIShaderResourceView* DispersionTableSRV,
		FRHIShaderResourceView* InSpectrum,
		FRHIShaderResourceView* InSpectrumConj,
		FUnorderedAccessViewRHIRef HeightBufferUAV, 
		FUnorderedAccessViewRHIRef SlopeBufferUAV, 
		FUnorderedAccessViewRHIRef DisplacementBufferUAV
	)
	{
		SetShaderValue(RHICmdList, GetComputeShader(), WaveSize, InWaveSize);
		SetShaderValue(RHICmdList, GetComputeShader(), GridLength, InGridLength);
		SetShaderValue(RHICmdList, GetComputeShader(), TimeSeconds, InTimeSeconds);

		SetSRVParameter(RHICmdList, GetComputeShader(), DispersionTable, DispersionTableSRV);
	
		SetSRVParameter(RHICmdList, GetComputeShader(), Spectrum, InSpectrum);
		SetSRVParameter(RHICmdList, GetComputeShader(), SpectrumConj, InSpectrumConj);

		if (RWHeightBuffer.IsBound())
			RHICmdList.SetUAVParameter(GetComputeShader(), RWHeightBuffer.GetBaseIndex(), HeightBufferUAV);
		if (RWSlopeBuffer.IsBound())
			RHICmdList.SetUAVParameter(GetComputeShader(), RWSlopeBuffer.GetBaseIndex(), SlopeBufferUAV);
		if (RWDisplacementBuffer.IsBound())
			RHICmdList.SetUAVParameter(GetComputeShader(), RWDisplacementBuffer.GetBaseIndex(), DisplacementBufferUAV);
	}

	void UnbindUAV(FRHICommandList& RHICmdList)
	{
		RHICmdList.SetUAVParameter(GetComputeShader(), RWHeightBuffer.GetBaseIndex(), nullptr);
		RHICmdList.SetUAVParameter(GetComputeShader(), RWSlopeBuffer.GetBaseIndex(), nullptr);
		RHICmdList.SetUAVParameter(GetComputeShader(), RWDisplacementBuffer.GetBaseIndex(), nullptr);
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

		OutEnvironment.SetDefine(TEXT("THREAD_GROUP_SIZE"), WAVE_GROUP_THREAD_COUNTS);
	}

	void SetParameters(
		FRHICommandListImmediate& RHICmdList, 
		float InTimeSeconds,
		int32 InWaveSize,
		int32 InStartIndex,
		FRHIShaderResourceView*  InButterflyLookupTable,
		FUnorderedAccessViewRHIRef HeightBufferUAV, 
		FUnorderedAccessViewRHIRef SlopeBufferUAV, 
		FUnorderedAccessViewRHIRef DisplacementBufferUAV
		)
	{
		SetShaderValue(RHICmdList, GetComputeShader(), TimeSeconds, InTimeSeconds);
		SetShaderValue(RHICmdList, GetComputeShader(), WaveSize, InWaveSize);
		SetShaderValue(RHICmdList, GetComputeShader(), StartIndex, InStartIndex);
		//SetShaderValueArray(RHICmdList, GetComputeShader(), ButterflyLookupTable, InButterflyLookupTable.GetData(), InButterflyLookupTable.Num());
		/*FRHIUnorderedAccessView* OutUAVs[] = { RWHeightBuffer.UAV, RWSlopeBuffer.UAV, RWDisplacementBuffer.UAV };
		RHICmdList.TransitionResources(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EComputeToCompute, OutUAVs, UE_ARRAY_COUNT(OutUAVs));*/
		
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
		/*FRHIUnorderedAccessView* OutUAVs[] = { RWHeightBuffer.UAV, RWSlopeBuffer.UAV, RWDisplacementBuffer.UAV };
		RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, OutUAVs, UE_ARRAY_COUNT(OutUAVs));*/

		if (RWHeightBuffer.IsBound())
			SetUAVParameter(RHICmdList, GetComputeShader(), RWHeightBuffer, nullptr);
		if (RWSlopeBuffer.IsBound())
			SetUAVParameter(RHICmdList, GetComputeShader(), RWSlopeBuffer, nullptr);
		if (RWDisplacementBuffer.IsBound())
			SetUAVParameter(RHICmdList, GetComputeShader(), RWDisplacementBuffer, nullptr);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << TimeSeconds;
		Ar << WaveSize; 
		Ar << StartIndex;
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

	FShaderResourceParameter RWHeightBuffer;
	FShaderResourceParameter RWSlopeBuffer;
	FShaderResourceParameter RWDisplacementBuffer;
};

IMPLEMENT_SHADER_TYPE(template<>, FWaveFFTCS<1>,  TEXT("/Plugins/Shaders/Private/FFTWave.usf"), TEXT("PerformFFTCS_Horizontal"), SF_Compute)
IMPLEMENT_SHADER_TYPE(template<>, FWaveFFTCS<2>,  TEXT("/Plugins/Shaders/Private/FFTWave.usf"), TEXT("PerformFFTCS_Vertical"), SF_Compute)

class FComputePosAndNormalShader : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FComputePosAndNormalShader, Global)

public:
	FComputePosAndNormalShader() {};

	FComputePosAndNormalShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		WaveSize.Bind(Initializer.ParameterMap, TEXT("WaveSize"));
		GridLength.Bind(Initializer.ParameterMap, TEXT("GridLength"));
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
		float InGridLength,
		FTexture2DRHIRef InHeightBuffer,
		FTexture2DRHIRef InSlopeBuffer,
		FTexture2DRHIRef InDisplacementBuffer
	)
	{
		SetShaderValue(RHICmdList, GetPixelShader(), WaveSize, InWaveSize);
		SetShaderValue(RHICmdList, GetPixelShader(), GridLength, InGridLength);
		SetSamplerParameter(RHICmdList, GetPixelShader(), TextureSampler, TStaticSamplerState<SF_AnisotropicLinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
		SetTextureParameter(RHICmdList, GetComputeShader(), HeightBuffer, InHeightBuffer);
		SetTextureParameter(RHICmdList, GetComputeShader(), SlopeBuffer, InSlopeBuffer);
		SetTextureParameter(RHICmdList, GetComputeShader(), DisplacementBuffer, InDisplacementBuffer);
	}

	void UnbindUAV(FRHICommandList& RHICmdList)
	{
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);

		Ar << WaveSize;
		Ar << GridLength;
		Ar << HeightBuffer;
		Ar << SlopeBuffer;
		Ar << DisplacementBuffer;

		return bShaderHasOutdatedParameters;
	}

private:
	FShaderParameter WaveSize;
	FShaderParameter GridLength;

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

extern void ComputeRandomTable(int32 Size, TArray<FVector2D>& OutTable)
{
	OutTable.SetNum(Size * Size);
	for (int32 i = 0; i < OutTable.Num(); ++i)
	{
		float x1, x2, w;
		do
		{
			x1 = 2.0f * FMath::FRand() - 1.0f;
			x2 = 2.0f * FMath::FRand() - 1.0f;
			w = x1 * x1 + x2 * x2;
		} while (w >= 1.0f);

		w = sqrt((-2.0f * log(w)) / w);
		OutTable[i] = FVector2D(x1 * w, x2 * w);
	}
}

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
	FRHIShaderResourceView*  RandomTableSRV,
	FUnorderedAccessViewRHIRef Spectrum,
	FUnorderedAccessViewRHIRef SpectrumConj)
{
	RHICmdList.BeginComputePass(TEXT("ComputePhillipsSpecturmPass"));
	TShaderMapRef<FPhillipsSpectrumCS> PhillipsSpecturmShader(GetGlobalShaderMap(FeatureLevel));

	RHICmdList.SetComputeShader(PhillipsSpecturmShader->GetComputeShader());
	PhillipsSpecturmShader->SetParameters(RHICmdList, WaveSize, GridLength, WaveAmplitude, WindSpeed, RandomTableSRV, Spectrum, SpectrumConj);
	DispatchComputeShader(RHICmdList, *PhillipsSpecturmShader, FMath::DivideAndRoundUp(WaveSize + 1, WAVE_GROUP_THREAD_COUNTS), FMath::DivideAndRoundUp(WaveSize + 1, WAVE_GROUP_THREAD_COUNTS), 1); 
	PhillipsSpecturmShader->UnbindUAV(RHICmdList);
	RHICmdList.EndComputePass();
}

static void PrepareFFT_RenderThread(
	FRHICommandListImmediate& RHICmdList,
	ERHIFeatureLevel::Type FeatureLevel,
	float TimeSeconds,
	int32 WaveSize,
	float GridLength,
	FRHIShaderResourceView*  DispersionTableSRV,
	FRWBuffer& Spectrum,
	FRWBuffer& SpectrumConj,
	FTextureRWBuffer2D& HeightBuffer, 
	FTextureRWBuffer2D& SlopeBuffer, 
	FTextureRWBuffer2D& DisplacementBuffer
)
{
	check(IsInRenderingThread());

	RHICmdList.BeginComputePass(TEXT("PrepareFFTPass"));
	TShaderMapRef<FPrepareFFTCS> PrepareFFTShader(GetGlobalShaderMap(FeatureLevel));

	RHICmdList.SetComputeShader(PrepareFFTShader->GetComputeShader());
	PrepareFFTShader->SetParameters(RHICmdList, TimeSeconds, WaveSize, GridLength, DispersionTableSRV, Spectrum.SRV, SpectrumConj.SRV, HeightBuffer.UAV, SlopeBuffer.UAV, DisplacementBuffer.UAV);
	DispatchComputeShader(RHICmdList, *PrepareFFTShader, FMath::DivideAndRoundUp(WaveSize, WAVE_GROUP_THREAD_COUNTS), FMath::DivideAndRoundUp(WaveSize, WAVE_GROUP_THREAD_COUNTS), 1); 
	PrepareFFTShader->UnbindUAV(RHICmdList);
	RHICmdList.EndComputePass();
}

static void EvaluateWavesFFT_RenderThread(
	FRHICommandListImmediate& RHICmdList,
	ERHIFeatureLevel::Type FeatureLevel,
	float TimeSeconds,
	int32 WaveSize,
	int32 StartIndex,
	FRHIShaderResourceView*  ButterflyLookupTableSRV,
	FTextureRWBuffer2D& HeightBuffer,
	FTextureRWBuffer2D& SlopeBuffer,
	FTextureRWBuffer2D& DisplacementBuffer)
{
	check(IsInRenderingThread());

	TShaderMapRef<FWaveFFTCS<1>> WaveFFTCS1(GetGlobalShaderMap(FeatureLevel));
	TShaderMapRef<FWaveFFTCS<2>> WaveFFTCS2(GetGlobalShaderMap(FeatureLevel));

	FShaderResourceViewRHIRef IndirectShadowCapsuleShapesSRV;

	int32 Passes = FMath::RoundToInt(FMath::Log2(WaveSize));
	RHICmdList.BeginComputePass(TEXT("EvaluateFFTPass"));
	for (int32 i = 0; i < Passes; ++i)
	{
		RHICmdList.SetComputeShader(WaveFFTCS1->GetComputeShader());
		WaveFFTCS1->SetParameters(RHICmdList, TimeSeconds, WaveSize, i, ButterflyLookupTableSRV, HeightBuffer.UAV, SlopeBuffer.UAV, DisplacementBuffer.UAV);
		DispatchComputeShader(RHICmdList, *WaveFFTCS1, FMath::DivideAndRoundUp(WaveSize, WAVE_GROUP_THREAD_COUNTS), FMath::DivideAndRoundUp(WaveSize, WAVE_GROUP_THREAD_COUNTS), 1);
		WaveFFTCS1->UnbindUAV(RHICmdList);
	}

	for (int32 i = 0; i < Passes; ++i)
	{
		RHICmdList.SetComputeShader(WaveFFTCS2->GetComputeShader());
		WaveFFTCS2->SetParameters(RHICmdList, TimeSeconds, WaveSize, i, ButterflyLookupTableSRV, HeightBuffer.UAV, SlopeBuffer.UAV, DisplacementBuffer.UAV);
		DispatchComputeShader(RHICmdList, *WaveFFTCS2, FMath::DivideAndRoundUp(WaveSize, WAVE_GROUP_THREAD_COUNTS), FMath::DivideAndRoundUp(WaveSize, WAVE_GROUP_THREAD_COUNTS), 1);
		WaveFFTCS2->UnbindUAV(RHICmdList);
	}
	RHICmdList.EndComputePass();
}

static void ComputePosAndNormal_RenderThread(
	FRHICommandListImmediate& RHICmdList,
	ERHIFeatureLevel::Type FeatureLevel,
	FTextureRenderTargetResource* OutputPosRenderTargetResource,
	FTextureRenderTargetResource* OutputNormalRenderTargetResource,
	int32 InWaveSize,
	float InGridLength,
	FTextureRWBuffer2D& HeightBuffer,
	FTextureRWBuffer2D& SlopeBuffer,
	FTextureRWBuffer2D& DisplacementBuffer
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

		ComputePosAndNormalPS->SetParameters(RHICmdList, InWaveSize, InGridLength, HeightBuffer.Buffer, SlopeBuffer.Buffer, DisplacementBuffer.Buffer);

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
	ENQUEUE_RENDER_COMMAND(FComputeFFT)([this](FRHICommandListImmediate& RHICmdList)
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

		CreateResources();
		ComputePhillipsSpecturm_RenderThread(RHICmdList, FeatureLevel, WaveSize, GridLength, WaveAmplitude, WindSpeed, RandomTableSRV, Spectrum.UAV, SpectrumConj.UAV);
	});
}

void AFFTWaveSimulator::PrepareForFFT(float TimeSeconds)
{
	//float KX, KY, Len, Lambda = -1.f;
	//int32 Index;
	//uint32 Stride;
	//FVector2D* HeightBufferData = static_cast<FVector2D*>(RHILockTexture2D(HeightBuffer, 0, EResourceLockMode::RLM_WriteOnly, Stride, false));
	//HeightBufferData += Stride / sizeof(FVector2D);
	//FVector4* SlopeBufferData = static_cast<FVector4*>(RHILockTexture2D(SlopeBuffer, 0, EResourceLockMode::RLM_WriteOnly, Stride, false));
	//SlopeBufferData += Stride / sizeof(FVector4);
	//FVector4* DisplacementBufferData = static_cast<FVector4*>(RHILockTexture2D(DisplacementBuffer, 0, EResourceLockMode::RLM_WriteOnly, Stride, false));
	//DisplacementBufferData += Stride / sizeof(FVector4);

	////Test buffer data
	//TArray<FVector2D> HeightData;
	//for (int32 i = 0; i < WaveSize * WaveSize; ++i)
	//{
	//	HeightData.Add(HeightBufferData[i]);
	//}

	//for (int32 m = 0; m < WaveSize; ++m)
	//{
	//	KY = PI * (2.f*m - WaveSize) / GridLength;
	//	for (int32 n = 0; n < WaveSize; ++n)
	//	{
	//		KX = PI * (2.f*n - WaveSize) / GridLength;
	//		Len = FMath::Sqrt(KX * KX + KY * KY);
	//		Index = m * WaveSize + n;

	//		FVector2D C = InitSpectrum(TimeSeconds, n, m);

	//		HeightBufferData[Index].X = C.X;
	//		HeightBufferData[Index].Y = C.Y;

	//		SlopeBufferData[Index].X = -C.Y * KX;
	//		SlopeBufferData[Index].Z = C.X * KX;
	//		SlopeBufferData[Index].Y = -C.Y * KY;
	//		SlopeBufferData[Index].Z = C.X * KY;

	//		if (Len < 0.000001f)
	//		{
	//			DisplacementBufferData[Index].X = 0.f;
	//			DisplacementBufferData[Index].Y = 0.f;
	//			DisplacementBufferData[Index].Z = 0.f;
	//			DisplacementBufferData[Index].W = 0.f;
	//		}
	//		else
	//		{
	//			DisplacementBufferData[Index].X = -C.Y * -(KX / Len);
	//			DisplacementBufferData[Index].Y = C.X * -(KX / Len);
	//			DisplacementBufferData[Index].Z = -C.Y * -(KY / Len);
	//			DisplacementBufferData[Index].W = C.X * -(KY / Len);
	//		}
	//	}
	//}

	//// Unlock the buffers
	//RHIUnlockTexture2D(HeightBuffer, 0, false);
	//RHIUnlockTexture2D(SlopeBuffer, 0, false);
	//RHIUnlockTexture2D(DisplacementBuffer, 0, false);
}

void AFFTWaveSimulator::EvaluateWavesFFT(float TimeSeconds)
{
	UWorld* World = GetWorld();
	ERHIFeatureLevel::Type FeatureLevel = World->Scene->GetFeatureLevel();

	TWeakObjectPtr<AFFTWaveSimulator> WaveSimulatorPtr(this);
	//GEngine->GetPreRenderDelegate().AddLambda();
	if (!GEngine->PreRenderDelegate.IsBoundToObject(this))
	{
		GEngine->PreRenderDelegate.AddWeakLambda(this, [FeatureLevel, this]() {
			if (!bHasInit)return;
			FRHICommandListImmediate& RHICmdList = GetImmediateCommandList_ForRenderCommand();
			PrepareFFT_RenderThread(RHICmdList, FeatureLevel, GetWorld()->TimeSeconds * TimeRate, WaveSize, GridLength, DispersionTableSRV, Spectrum, SpectrumConj, HeightBuffer, SlopeBuffer, DisplacementBuffer);
			EvaluateWavesFFT_RenderThread(RHICmdList, FeatureLevel, GetWorld()->TimeSeconds, WaveSize, 0, ButterflyLookupTableSRV, HeightBuffer, SlopeBuffer, DisplacementBuffer);

			ComputePositionAndNormal();
			if (WaveHeightMapRenderTarget && WaveNormalRenderTarget)
				ComputePosAndNormal_RenderThread(RHICmdList, FeatureLevel, WaveHeightMapRenderTarget->GetRenderTargetResource(), WaveNormalRenderTarget->GetRenderTargetResource(), WaveSize, GridLength, HeightBuffer, SlopeBuffer, DisplacementBuffer);
		});
	}
	

	/*ENQUEUE_RENDER_COMMAND(FComputeFFT)([FeatureLevel, TimeSeconds, WaveSimulatorPtr](FRHICommandListImmediate& RHICmdList)
	{

	});*/
}