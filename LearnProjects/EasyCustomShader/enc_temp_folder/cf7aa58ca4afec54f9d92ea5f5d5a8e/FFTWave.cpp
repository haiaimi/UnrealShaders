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
		WaveAmplitude.Bind(Initializer.ParameterMap, TEXT("WaveAmplitude"));
		WindSpeed.Bind(Initializer.ParameterMap, TEXT("WindSpeed"));
		Spectrum.Bind(Initializer.ParameterMap, TEXT("Spectrum"));
		SpectrumConj.Bind(Initializer.ParameterMap, TEXT("SpectrumConj"));
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
		float InWaveAmplitude,
		FVector InWindSpeed,
		FUnorderedAccessViewRHIRef SpectrumUAV,
		FUnorderedAccessViewRHIRef SpectrumConjUAV
	)
	{
		SetShaderValue(RHICmdList, GetComputeShader(), WaveSize, InWaveSize);
		SetShaderValue(RHICmdList, GetComputeShader(), WaveAmplitude, InWaveAmplitude);
		SetShaderValue(RHICmdList, GetComputeShader(), WindSpeed, InWindSpeed);
		if (Spectrum.IsBound())
			RHICmdList.SetUAVParameter(GetComputeShader(), Spectrum.GetBaseIndex(), SpectrumUAV);
		if (SpectrumConj.IsBound())
			RHICmdList.SetUAVParameter(GetComputeShader(), SpectrumConj.GetBaseIndex(), SpectrumConjUAV);
	}

	void UnbindUAV(FRHICommandList& RHICmdList)
	{
		RHICmdList.SetUAVParameter(GetComputeShader(), Spectrum.GetBaseIndex(), FUnorderedAccessViewRHIParamRef());
		RHICmdList.SetUAVParameter(GetComputeShader(), SpectrumConj.GetBaseIndex(), FUnorderedAccessViewRHIParamRef());
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << WaveSize;
		Ar << WaveAmplitude;
		Ar << WindSpeed;
		Ar << Spectrum;
		Ar << SpectrumConj;

		return bShaderHasOutdatedParameters;
	}

private:
	FShaderParameter WaveSize;
	FShaderParameter WaveAmplitude;
	FShaderParameter WindSpeed;

	FShaderResourceParameter Spectrum;
	FShaderResourceParameter SpectrumConj;
};

IMPLEMENT_SHADER_TYPE(, FPhillipsSpectrumCS,  TEXT("/Plugins/Shaders/Private/FFTWave.usf"), TEXT("PhillipsSpectrumCS"), SF_Compute)

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

extern void ComputeButterflyLookuptable(int32 Size, int32 Passes, TArray<float>& OutTable)
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
    float WaveAmplitude,
	FVector WindSpeed,
	FUnorderedAccessViewRHIRef Spectrum,
	FUnorderedAccessViewRHIRef SpectrumConj)
{
	TShaderMapRef<FPhillipsSpectrumCS> PhillipsSpecturmShader(GetGlobalShaderMap(FeatureLevel));

	RHICmdList.SetComputeShader(PhillipsSpecturmShader->GetComputeShader());
	PhillipsSpecturmShader->SetParameters(RHICmdList, WaveSize, WaveAmplitude, WindSpeed, Spectrum, SpectrumConj);
	DispatchComputeShader(RHICmdList, *PhillipsSpecturmShader, FMath::DivideAndRoundUp(WaveSize + 1, WAVE_GROUP_THREAD_COUNTS), FMath::DivideAndRoundUp(WaveSize + 1, WAVE_GROUP_THREAD_COUNTS), 1); 
	PhillipsSpecturmShader->UnbindUAV(RHICmdList);
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
	if (!WaveSimulator.IsValid())
		return;
	TShaderMapRef<FWaveFFTCS<1>> WaveFFTCS1(GetGlobalShaderMap(FeatureLevel));
	TShaderMapRef<FWaveFFTCS<2>> WaveFFTCS2(GetGlobalShaderMap(FeatureLevel));

	FShaderResourceViewRHIRef IndirectShadowCapsuleShapesSRV;

	int32 Passes = FMath::RoundToInt(FMath::Log2(WaveSize));

	/*RHICmdList.SetComputeShader(WaveFFTCS1->GetComputeShader());
	WaveFFTCS1->SetParameters(RHICmdList, TimeSeconds, WaveSize, 0, WaveSimulator->ButterflyLookupTableSRV, HeightBuffer, SlopeBuffer, DisplacementBuffer, HeightBufferUAV, SlopeBufferUAV, DisplacementBufferUAV);
	DispatchComputeShader(RHICmdList, *WaveFFTCS1, 1, 1, 1);
	WaveFFTCS1->UnbindUAV(RHICmdList);*/
	//uint32 Stride;
	//FVector2D* HeightBufferData = static_cast<FVector2D*>(RHILockTexture2D(HeightBuffer, 0, EResourceLockMode::RLM_ReadOnly, Stride, false));
	//TArray<FVector2D> Result;
	///*for (int32 i = 0; i < 2; ++i)
	//{
	//	for (int32 j = 0; j < WaveSize; ++j)
	//	{
	//		for (int32 k = 0; k < WaveSize; ++k)
	//			Result.Add(HeightBufferData[j * WaveSize + k]);
	//	}
	//	HeightBufferData += Stride / sizeof(FVector2D);
	//}*/

	//for (int32 i = 0; i < 4 * 4; ++i)
	//{
	//	Result.Add(HeightBufferData[i]);
	//}
	//HeightBufferData += Stride / sizeof(FVector2D);
	//for (int32 i = 0; i < 4 * 4; ++i)
	//{
	//	Result.Add(HeightBufferData[i]);
	//}
	//
	//RHIUnlockTexture2D(HeightBuffer, 0, false);
	for (int32 i = 0; i < Passes; ++i)
	{
		RHICmdList.SetComputeShader(WaveFFTCS1->GetComputeShader());
		WaveFFTCS1->SetParameters(RHICmdList, TimeSeconds, WaveSize, i, WaveSimulator->ButterflyLookupTableSRV, HeightBuffer, SlopeBuffer, DisplacementBuffer, HeightBufferUAV, SlopeBufferUAV, DisplacementBufferUAV);
		DispatchComputeShader(RHICmdList, *WaveFFTCS1, FMath::DivideAndRoundUp(WaveSize, WAVE_GROUP_THREAD_COUNTS), FMath::DivideAndRoundUp(WaveSize, WAVE_GROUP_THREAD_COUNTS), 1);
		WaveFFTCS1->UnbindUAV(RHICmdList);

		//HeightBufferData = static_cast<FVector2D*>(RHILockTexture2D(HeightBuffer, 0, EResourceLockMode::RLM_ReadOnly, Stride, false));
		//Result.Reset();
		///*for (int32 i = 0; i < 2; ++i)
		//{
		//	for (int32 j = 0; j < WaveSize; ++j)
		//	{
		//		for (int32 k = 0; k < WaveSize; ++k)
		//			Result.Add(HeightBufferData[j * WaveSize + k]);
		//	}
		//	HeightBufferData += Stride / sizeof(FVector2D);
		//}*/

		//for (int32 i = 0; i < 4 * 4; ++i)
		//{
		//	Result.Add(HeightBufferData[i]);
		//}
		//HeightBufferData += Stride / sizeof(FVector2D);
		//for (int32 i = 0; i < 4 * 4; ++i)
		//{
		//	Result.Add(HeightBufferData[i]);
		//}

		//RHIUnlockTexture2D(HeightBuffer, 0, false);
	}

	for (int32 i = 0; i < Passes; ++i)
	{
		RHICmdList.SetComputeShader(WaveFFTCS2->GetComputeShader());
		WaveFFTCS2->SetParameters(RHICmdList, TimeSeconds, WaveSize, i, WaveSimulator->ButterflyLookupTableSRV, HeightBuffer, SlopeBuffer, DisplacementBuffer, HeightBufferUAV, SlopeBufferUAV, DisplacementBufferUAV);
		DispatchComputeShader(RHICmdList, *WaveFFTCS2, FMath::DivideAndRoundUp(WaveSize, WAVE_GROUP_THREAD_COUNTS), FMath::DivideAndRoundUp(WaveSize, WAVE_GROUP_THREAD_COUNTS), 1);
		WaveFFTCS2->UnbindUAV(RHICmdList);
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
		ComputePhillipsSpecturm_RenderThread(RHICmdList, FeatureLevel, WaveSize, WaveAmplitude, WindSpeed, SpectrumUAV, SpectrumConjUAV);

		uint32 Stride;
		FVector2D* SpectrumData = static_cast<FVector2D*>(RHILockTexture2D(Spectrum, 0, EResourceLockMode::RLM_ReadOnly, Stride, false));
		TArray<FVector2D> Result;
		/*for (int32 i = 0; i < 2; ++i)
		{
			for (int32 j = 0; j < WaveSize; ++j)
			{
				for (int32 k = 0; k < WaveSize; ++k)
					Result.Add(HeightBufferData[j * WaveSize + k]);
			}
			HeightBufferData += Stride / sizeof(FVector2D);
		}*/

		for (int32 i = 0; i < 5 * 5; ++i)
		{
			Result.Add(SpectrumData[i]);
		}
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
			WaveSimulatorPtr->PrepareForFFT(TimeSeconds);

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
			EvaluateWavesFFT_RenderThread(RHICmdList, FeatureLevel, TimeSeconds, WaveSimulatorPtr->WaveSize, 0, WaveSimulatorPtr->HeightBuffer, WaveSimulatorPtr->SlopeBuffer,WaveSimulatorPtr->DisplacementBuffer, HeightBufferUAV, SlopeBufferUAV, DisplacementBufferUAV, WaveSimulatorPtr);

			WaveSimulatorPtr->ComputePositionAndNormal();
		}
	});
}