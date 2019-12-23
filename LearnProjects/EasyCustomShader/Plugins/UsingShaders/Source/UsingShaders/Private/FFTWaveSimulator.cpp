// Fill out your copyright notice in the Description page of Project Settings.


#include "FFTWaveSimulator.h"
#include "ProceduralMeshComponent.h"
#include "KismetProceduralMeshLibrary.h"
#include "RHICommandList.h"

#define GRAVITY 9.8f

extern void ComputeButterflyLookuptable(int32 Size, int32 Passes, TArray<float>& OutTable);

// Sets default values
AFFTWaveSimulator::AFFTWaveSimulator():
	WaveMesh(nullptr),
	WaveSize(64),
	GridLength(100.f)
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	WindSpeed = FVector(10.f, 10.f, 0.f);
	WaveAmplitude = 0.05f;
	WaveMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("WaveMesh"));

	if (GridMaterial)
		WaveMesh->SetMaterial(0, GridMaterial);
}

// Called when the game starts or when spawned
void AFFTWaveSimulator::BeginPlay()
{
	Super::BeginPlay();
	
	InitWaveResource();
}

// Called every frame
void AFFTWaveSimulator::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	FProcMeshSection* MeshSection = WaveMesh->GetProcMeshSection(0);
	if (MeshSection)
	{
		TArray<FColor> Colors;
		TArray<FProcMeshTangent> Tangents;
		WaveMesh->UpdateMeshSection(0, WaveVertices, WaveNormals, UVs, Colors, Tangents);
	}

	if (GetWorld())
		EvaluateWavesFFT(GetWorld()->TimeSeconds);
}

void AFFTWaveSimulator::InitWaveResource()
{
	CreateWaveGrid();
	ComputeSpectrum();
}

FVector2D AFFTWaveSimulator::InitSpectrum(float TimeSeconds, int32 n, int32 m)
{
	int32 Index = m * (WaveSize + 1) + n;
	float Omegat = DispersionTable[Index] * TimeSeconds;

	float Cos = FMath::Cos(Omegat);
	float Sin = FMath::Sin(Omegat);

	uint32 Stride;
	if (Spectrum->GetSizeX() > (uint32)Index && SpectrumConj->GetSizeX() > (uint32)Index)
	{
		FVector2D* SpectrumData = static_cast<FVector2D*>(RHILockTexture2D(Spectrum, 0, EResourceLockMode::RLM_ReadOnly, Stride, false));
		//SpectrumData += m * Stride / sizeof(FVector2D);
		float C0a = SpectrumData[Index].X*Cos - SpectrumData[Index].Y*Sin;
		float C0b = SpectrumData[Index].X*Sin - SpectrumData[Index].Y*Cos;

		TArray<FVector2D> AllData;
		AllData.SetNum((WaveSize + 1)*(WaveSize + 1));
		for (int32 i = 0; i < AllData.Num(); ++i)
		{
			AllData[i] = SpectrumData[i];
		}

		FVector2D* SpectrumConjData = static_cast<FVector2D*>(RHILockTexture2D(SpectrumConj, 0, EResourceLockMode::RLM_ReadOnly, Stride, false));
		//SpectrumConjData += m * Stride / sizeof(FVector2D);
 		float C1a = SpectrumConjData[Index].X*Cos - SpectrumConjData[Index].Y*-Sin;
		float C1b = SpectrumConjData[Index].X*-Sin - SpectrumConjData[Index].Y*Cos;

		//Unlock
		RHIUnlockTexture2D(Spectrum, 0, false);
		RHIUnlockTexture2D(SpectrumConj, 0, false);

		return FVector2D(C0a + C1a, C0b + C1b);
	}

	return FVector2D::ZeroVector;
}

float AFFTWaveSimulator::Dispersion(int32 n, int32 m)
{
	float W_0 = 2.0f * PI / 200.f;
	float KX = PI * (2 * n - WaveSize) / GridLength;
	float KY = PI * (2 * m - WaveSize) / GridLength;
	return FMath::FloorToFloat(FMath::Sqrt(GRAVITY * FMath::Sqrt(KX * KX + KY * KY) / W_0)) * W_0;
}

void AFFTWaveSimulator::CreateWaveGrid()
{
	TArray<int32> Triangles;
	TArray<FVector2D> UV1;
	TArray<FColor> Colors;
	TArray<FProcMeshTangent> Tangents;
	UKismetProceduralMeshLibrary::CreateGridMeshWelded(WaveSize + 1, WaveSize + 1, Triangles, WaveVertices, UVs, GridLength);

	WaveNormals.SetNum((WaveSize + 1) * (WaveSize + 1));
	WavePosition.SetNum((WaveSize + 1) * (WaveSize + 1));
	DispersionTable.SetNum((WaveSize + 1) * (WaveSize + 1));

	for (int32 i = 0; i < WaveSize + 1; ++i)
		for (int32 j = 0; j < WaveSize + 1; ++j)
		{
			int32 Index = i * (WaveSize + 1) + j;
			WaveNormals[Index] = FVector(0.f, 0.f, 1.f);
			WavePosition[Index] = WaveVertices[Index];
			WavePosition[Index].Z = 0.f;

			DispersionTable[Index] = Dispersion(j, i);
		}

	if (WaveMesh)
	{
		WaveMesh->CreateMeshSection(0, WaveVertices, Triangles, WaveNormals, UVs, Colors, Tangents, true);
	}
}

void AFFTWaveSimulator::CreateResources()
{
	FRHIResourceCreateInfo RHIResourceCreateInfo, RHIResourceCreateInfo1;
	
	Spectrum = RHICreateTexture2D((WaveSize + 1)*(WaveSize + 1), 1, PF_G32R32F, 1, 1, TexCreate_ShaderResource | TexCreate_UAV, RHIResourceCreateInfo);
	SpectrumConj = RHICreateTexture2D((WaveSize + 1)*(WaveSize + 1), 1, PF_G32R32F, 1, 1, TexCreate_ShaderResource | TexCreate_UAV, RHIResourceCreateInfo);
	HeightBuffer = RHICreateTexture2D(WaveSize * WaveSize, 2, PF_G32R32F, 1, 1, TexCreate_ShaderResource | TexCreate_UAV, RHIResourceCreateInfo1); //float2
	SlopeBuffer = RHICreateTexture2D(WaveSize * WaveSize, 2, PF_A32B32G32R32F, 1, 1, TexCreate_ShaderResource | TexCreate_UAV, RHIResourceCreateInfo); //float4
	DisplacementBuffer = RHICreateTexture2D(WaveSize * WaveSize, 2, PF_A32B32G32R32F, 1, 1, TexCreate_ShaderResource | TexCreate_UAV, RHIResourceCreateInfo); //float4
	//FUnorderedAccessViewRHIRef TempTextureUAV = RHICreateUnorderedAccessView(TempTexture);

	ComputeButterflyLookuptable(WaveSize, (int32)FMath::Log2(WaveSize), ButterflyLookupTable);

	ButterflyLookupTableVB.SafeRelease();
	ButterflyLookupTableSRV.SafeRelease();
	DispersionTableVB.SafeRelease();
	DispersionTableSRV.SafeRelease();
	FRHIResourceCreateInfo CreateInfo;
	ButterflyLookupTableVB = RHICreateVertexBuffer(ButterflyLookupTable.Num() * sizeof(float), BUF_Volatile | BUF_ShaderResource, CreateInfo);
	ButterflyLookupTableSRV = RHICreateShaderResourceView(ButterflyLookupTableVB, sizeof(float), PF_R32_FLOAT);

	DispersionTableVB = RHICreateVertexBuffer(DispersionTable.Num() * sizeof(float), BUF_Volatile | BUF_ShaderResource, CreateInfo);
	DispersionTableSRV = RHICreateShaderResourceView(DispersionTableVB, sizeof(float), PF_R32_FLOAT);

	void* ButterflyLockedData = RHILockVertexBuffer(ButterflyLookupTableVB, 0, ButterflyLookupTable.Num() * sizeof(float), RLM_WriteOnly);
	FPlatformMemory::Memcpy(ButterflyLockedData, ButterflyLookupTable.GetData(), ButterflyLookupTable.Num() * sizeof(float));
	RHIUnlockVertexBuffer(ButterflyLookupTableVB);
	
	void* DispersionTableData = RHILockVertexBuffer(DispersionTableVB, 0, DispersionTable.Num() * sizeof(float), RLM_WriteOnly);
	FPlatformMemory::Memcpy(DispersionTableData, DispersionTable.GetData(), DispersionTable.Num() * sizeof(float));
	RHIUnlockVertexBuffer(DispersionTableVB);
}

void AFFTWaveSimulator::ComputePositionAndNormal()
{
	if ((int32)HeightBuffer->GetSizeX() >= WaveSize * WaveSize && 
		(int32)SlopeBuffer->GetSizeX() >= WaveSize * WaveSize && 
		(int32)DisplacementBuffer->GetSizeX() >= WaveSize * WaveSize)
	{
		uint32 Stride;
		FVector2D* HeightBufferData = static_cast<FVector2D*>(RHILockTexture2D(HeightBuffer, 0, EResourceLockMode::RLM_ReadOnly, Stride, false));
		HeightBufferData += Stride / sizeof(FVector2D);
		FVector4* SlopeBufferData = static_cast<FVector4*>(RHILockTexture2D(SlopeBuffer, 0, EResourceLockMode::RLM_ReadOnly, Stride, false));
		SlopeBufferData += Stride / sizeof(FVector4);
		FVector4* DisplacementBufferData = static_cast<FVector4*>(RHILockTexture2D(DisplacementBuffer, 0, EResourceLockMode::RLM_ReadOnly, Stride, false));
		DisplacementBufferData += Stride / sizeof(FVector4);

		int32 Sign;
		static float Signs[2] = { 1.f,-1.f };
		float Lambda = -1.f;
		for (int32 m = 0; m < WaveSize; ++m)
		{
			for (int32 n = 0; n < WaveSize; ++n)
			{
				int32 Index = m * WaveSize + n;
				int32 Index1 = m * (WaveSize + 1) + n;

				Sign = (int32)Signs[(n + m) & 1];

				// Get height
				WaveVertices[Index1].Z = HeightBufferData[Index].X * Sign / 500.f;

				// Get displacement
				WaveVertices[Index1].X = WavePosition[Index1].X + DisplacementBufferData[Index].X * Lambda * Sign/ 500.f;
				WaveVertices[Index1].Y = WavePosition[Index1].Y + DisplacementBufferData[Index].Y * Lambda * Sign / 500.f;
				
				// Get normal
				FVector Normal(-SlopeBufferData[Index].X *Sign, -SlopeBufferData[Index].Y *Sign, 1.f);
				Normal.Normalize();

				WaveNormals[Index1].X = Normal.X;
				WaveNormals[Index1].Y = Normal.Y;
				WaveNormals[Index1].Z = Normal.Z;

				int32 TileIndex;
				//Handle tiling
				if (n == 0 && m == 0)
				{
					TileIndex = Index1 + WaveSize + (WaveSize + 1)*WaveSize;
				}
				else if (n == 0)
				{
					TileIndex = Index1 + WaveSize;
				}
				else if (m == 0)
				{
					TileIndex = Index1 + (WaveSize + 1) * WaveSize;
				}
				else
					continue;

				//WaveVertices[TileIndex].Z = HeightBufferData[Index].X * Sign;
				////WaveVertices[TileIndex].X = WavePosition[TileIndex].X + DisplacementBufferData[Index].X * Lambda * Sign;
				////WaveVertices[TileIndex].Y = WavePosition[TileIndex].Y + DisplacementBufferData[Index].Z * Lambda * Sign;
				//
				//WaveNormals[TileIndex].X = Normal.X;
				//WaveNormals[TileIndex].Y = Normal.Y;
				//WaveNormals[TileIndex].Z = Normal.Z;
			}
		}
		// Unlock the buffers
		RHIUnlockTexture2D(HeightBuffer, 0, false);
		RHIUnlockTexture2D(SlopeBuffer, 0, false);
		RHIUnlockTexture2D(DisplacementBuffer, 0, false);
	}
}

#if WITH_EDITOR
void AFFTWaveSimulator::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif
