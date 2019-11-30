// Fill out your copyright notice in the Description page of Project Settings.


#include "FFTWaveSimulator.h"
#include "ProceduralMeshComponent.h"
#include "KismetProceduralMeshLibrary.h"
#include "RHICommandList.h"

#define GRAVITY 9.8f

// Sets default values
AFFTWaveSimulator::AFFTWaveSimulator():
	WaveMesh(nullptr),
	WaveSize(64),
	GridLength(100.f)
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	WindDirection = FVector(1.f, 1.f, 0.f);
	WaveMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("WaveMesh"));
}

// Called when the game starts or when spawned
void AFFTWaveSimulator::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void AFFTWaveSimulator::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

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
	TArray<FVector2D> UVs;
	TArray<FVector2D> UV1;
	TArray<FColor> Colors;
	TArray<FProcMeshTangent> Tangents;
	UKismetProceduralMeshLibrary::CreateGridMeshSplit(WaveSize + 1, WaveSize + 1, Triangles, WaveVertices, UVs, UV1, GridLength);

	WaveNormals.SetNum((WaveSize + 1) * (WaveSize + 1));
	WavePosition.SetNum((WaveSize + 1) * (WaveSize + 1));
	DispersionTable.SetNum((WaveSize + 1) * (WaveSize + 1));

	for (int32 i = 0; i < WaveSize + 1; ++i)
		for (int32 j = 0; i < WaveSize + 1; ++j)
		{
			int32 Index = i * (WaveSize + 1) + j;
			WaveNormals[Index] = FVector(0.f, 0.f, 1.f);
			WavePosition[Index] = WaveVertices[Index];
			WavePosition[Index].Z = 0.f;
		}

	if (WaveMesh)
	{
		WaveMesh->CreateMeshSection(0, WaveVertices, Triangles, WaveNormals, UVs, Colors, Tangents, true);
	}
}

void AFFTWaveSimulator::CreateResources()
{
	FRHIResourceCreateInfo RHIResourceCreateInfo;
	HeightBuffer = RHICreateTexture2D(2, WaveSize * WaveSize, PF_G32R32F, 1, 1, TexCreate_ShaderResource | TexCreate_UAV, RHIResourceCreateInfo); //float2
	SlopeBuffer = RHICreateTexture2D(2, WaveSize * WaveSize, PF_A32B32G32R32F, 1, 1, TexCreate_ShaderResource | TexCreate_UAV, RHIResourceCreateInfo); //float4
	DisplacementBuffer = RHICreateTexture2D(2, WaveSize * WaveSize, PF_A32B32G32R32F, 1, 1, TexCreate_ShaderResource | TexCreate_UAV, RHIResourceCreateInfo); //float4
	//FUnorderedAccessViewRHIRef TempTextureUAV = RHICreateUnorderedAccessView(TempTexture);
}

void AFFTWaveSimulator::ComputePositionAndNormal()
{
	//FProcMeshSection* MeshSection = WaveMesh->GetProcMeshSection(0);
	//if (MeshSection)
	//{

	//}

	if (HeightBuffer->GetSizeY() >= WaveSize * WaveSize && 
		SlopeBuffer->GetSizeY() >= WaveSize * WaveSize && 
		DisplacementBuffer->GetSizeY() >= WaveSize * WaveSize)
	{
		uint32 Stride;
		FVector2D* HeightBufferData = static_cast<FVector2D*>(GDynamicRHI->RHILockTexture2D(HeightBuffer, 0, EResourceLockMode::RLM_ReadOnly, Stride, false));
		FVector* SlopeBufferData = static_cast<FVector*>(GDynamicRHI->RHILockTexture2D(SlopeBuffer, 0, EResourceLockMode::RLM_ReadOnly, Stride, false));
		FVector* DisplacementBufferData = static_cast<FVector*>(GDynamicRHI->RHILockTexture2D(DisplacementBuffer, 0, EResourceLockMode::RLM_ReadOnly, Stride, false));

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
				WaveVertices[Index1].Z = HeightBufferData[WaveSize * WaveSize + Index].X * Sign;

				// Get displacement
				WaveVertices[Index1].X = WavePosition[Index1].X + DisplacementBufferData[WaveSize * WaveSize + Index].X * Lambda * Sign;
				WaveVertices[Index1].Y = WavePosition[Index1].Y + DisplacementBufferData[WaveSize * WaveSize + Index].Y * Lambda * Sign;
				
				// Get normal
				FVector Normal(-SlopeBufferData[WaveSize * WaveSize + Index].X *Sign, -SlopeBufferData[WaveSize * WaveSize + Index].Y *Sign, 1.f);
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

				WaveVertices[TileIndex].Z = HeightBufferData[WaveSize * WaveSize + Index].X * Sign;
				WaveVertices[TileIndex].X = WavePosition[TileIndex].X + DisplacementBufferData[WaveSize * WaveSize + Index].X * Lambda * Sign;
				WaveVertices[TileIndex].Y = WavePosition[TileIndex].Y + DisplacementBufferData[WaveSize * WaveSize + Index].Z * Lambda * Sign;
				
				WaveNormals[TileIndex].X = Normal.X;
				WaveNormals[TileIndex].Y = Normal.Y;
				WaveNormals[TileIndex].Z = Normal.Z;
			}
		}
	}
}

#if WITH_EDITOR
void AFFTWaveSimulator::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif
