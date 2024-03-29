// Fill out your copyright notice in the Description page of Project Settings.


#include "FFTWaveSimulator.h"
#include "ProceduralMeshComponent.h"
#include "KismetProceduralMeshLibrary.h"
#include "RHICommandList.h"
#include "PhysicsEngine/BodySetup.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "DrawDebugHelpers.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/StaticMeshComponent.h"

#define GRAVITY 9.8f

extern void ComputeRandomTable(int32 Size, TArray<FVector2D>& OutTable);
extern void ComputeButterflyLookuptable(int32 Size, int32 Passes, TArray<float>& OutTable);
//extern int32 GHZBOcclusion;

TMap<TSubclassOf<AFFTWaveSimulator>, TArray<AFFTWaveSimulator*>> GlobalRunningFFTWave;

bool bIsWaveBegun = false;

void CreateWaveGridMesh(int32 HoriTiles, int32 VertTiles, int32 NumX, int32 NumY, TArray<int32>& Triangles, TArray<FVector>& Vertices, TArray<FVector2D>& UVs, float GridSpacing)
{
	Triangles.Empty();
	Vertices.Empty();
	UVs.Empty();

	if (NumX >= 2 && NumY >= 2)
	{
		int HoriNum = (NumX - 1) * HoriTiles + 1;
		int VertNum = (NumY - 1) * VertTiles + 1;
		FVector2D Extent = FVector2D((HoriNum - 1)* GridSpacing, (VertNum - 1) * GridSpacing) / 2;
		
		for (int i = 0; i < VertNum; i++)
		{
			for (int j = 0; j < HoriNum; j++)
			{
				Vertices.Add(FVector((float)j * GridSpacing - Extent.X, (float)i * GridSpacing - Extent.Y, 0));
				UVs.Add(FVector2D((float)HoriTiles * (float)j / ((float)HoriNum - 1), (float)VertTiles * (float)i / ((float)VertNum - 1)));
			}
		}

		for (int i = 0; i < VertNum - 1; i++)
		{
			for (int j = 0; j < HoriNum - 1; j++)
			{
				int idx = j + (i * HoriNum);
				Triangles.Add(idx);
				Triangles.Add(idx + HoriNum);
				Triangles.Add(idx + 1);

				Triangles.Add(idx + 1);
				Triangles.Add(idx + HoriNum);
				Triangles.Add(idx + HoriNum + 1);
			}
		}
	}
}

// Sets default values
AFFTWaveSimulator::AFFTWaveSimulator():
	WaveMesh(nullptr),
	bUseStaticMesh(false),
	WaveStaticMeshGridSize(100.f),
	HorizontalTileCount(1),
	VerticalTileCount(1),
	MeshGridLength(100.f),
	TimeRate(2.f),
	WaveSize(64),
	PatchLength(1.f),
	WaveHeightMapRenderTarget(nullptr),
	DrawNormal(false),
	bHasInit(false)
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	Spectrum = new FRWBuffer();
	SpectrumConj = new FRWBuffer();

	HeightBuffer = new FTextureRWBuffer2D();
	SlopeBuffer = new FTextureRWBuffer2D();
	DisplacementBuffer = new FTextureRWBuffer2D();

	WindSpeed = FVector(10.f, 10.f, 0.f);
	WaveAmplitude = 0.05f;
	//if (bUseStaticMesh && GridMaterial)
		WaveMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WaveMesh"));
	//else
		//WaveMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("WaveMesh"));
}

// Called when the game starts or when spawned
void AFFTWaveSimulator::BeginPlay()
{
	Super::BeginPlay();
	
	if (!bIsWaveBegun)
	{
		GlobalRunningFFTWave.Reset();
		bIsWaveBegun = true;
	}
	//Try to open hzb occlusion cull, or it will has some artifact
	auto ConsoleResult = IConsoleManager::Get().FindConsoleVariable(TEXT("r.HZBOcclusion"));
	if (ConsoleResult)
	{
		int32 CurState = ConsoleResult->GetInt();
		if (CurState == 0 && GetWorld())
		{
			if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0))
			{
				PlayerController->ConsoleCommand(TEXT("r.HZBOcclusion 1"));
			}
		}
	}
	InitWaveResource();

	//DrawNormal = true;
}

void AFFTWaveSimulator::PostInitializeComponents()
{
	Super::PostInitializeComponents();
}

void AFFTWaveSimulator::PostActorCreated()
{
	Super::PostActorCreated();
}

void AFFTWaveSimulator::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	if (EndPlayReason == EEndPlayReason::EndPlayInEditor)
	{
		bIsWaveBegun = false;
		GlobalRunningFFTWave.Reset();
	}

	ReleaseResource();
}

void AFFTWaveSimulator::BeginDestroy()
{
	ReleaseResource();

	Super::BeginDestroy();
}

void AFFTWaveSimulator::Destroyed()
{
	auto CurClass = GetClass();
	TArray<AFFTWaveSimulator*>* Result = GlobalRunningFFTWave.Find(CurClass);
	if (Result)
	{
		int32 OutIndex = INDEX_NONE;
		if ((*Result).Find(this, OutIndex))
		{
			(*Result).RemoveAt(OutIndex);
			if ((*Result).Num() == 0)
				GlobalRunningFFTWave.Remove(CurClass);
		}
	}
	ReleaseResource();

	Super::Destroyed();
}

void AFFTWaveSimulator::ReleaseResource()
{
	ENQUEUE_RENDER_COMMAND(FRelseaseWaveResource)([=](FRHICommandListImmediate& RHICmdList)
	{
		if(Spectrum)delete Spectrum;
		if(SpectrumConj)delete SpectrumConj;
		if(HeightBuffer)delete HeightBuffer;
		if(SlopeBuffer)delete SlopeBuffer;
		if(DisplacementBuffer)delete DisplacementBuffer;
	});
	Spectrum = nullptr;
	SpectrumConj = nullptr;
	HeightBuffer = nullptr;
	SlopeBuffer = nullptr;
	DisplacementBuffer = nullptr;
}

// Called every frame
void AFFTWaveSimulator::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	InitWaveResource();

	if (GetWorld())
	{
		auto CurClass = GetClass();
		TArray<AFFTWaveSimulator*>* Result = GlobalRunningFFTWave.Find(CurClass);
		if (Result && (*Result).Num() > 0 && (*Result)[0] == this)
		{
			EvaluateWavesFFT(GetWorld()->TimeSeconds);
			
			if (DrawNormal)
			{
				for (int32 i = 0; i < WaveVertices.Num(); ++i)
				{
					DrawDebugDirectionalArrow(GetWorld(), GetActorLocation() + WaveVertices[i] * (MeshGridLength / PatchLength), GetActorLocation() + WaveVertices[i] * (MeshGridLength / PatchLength) + WaveNormals[i] * 100.f, 20.f, FColor::Red, false, -1.f, 0, 5.f);
				}

				TArray<FColor> Colors;
				TArray<FProcMeshTangent> Tangents;
				UProceduralMeshComponent* ProcMesh = Cast<UProceduralMeshComponent>(WaveMesh);
				if (ProcMesh)
					ProcMesh->UpdateMeshSection(0, WavePosition, WaveNormals, UVs, Colors, Tangents);
			}
				
		}
		if (Result)
			(*Result).AddUnique(this);
		else
			GlobalRunningFFTWave.Add(CurClass, { this });
	}
}

void AFFTWaveSimulator::InitWaveResource()
{
	if (bHasInit && WaveMesh)return;

	if (WaveMesh)
		WaveMesh->Bounds.BoxExtent.Z = 0.f;
	CreateWaveGrid();
	ComputeSpectrum();

	bHasInit = true;
}

FVector2D AFFTWaveSimulator::InitSpectrum(float TimeSeconds, int32 n, int32 m)
{
	int32 Index = m * (WaveSize + 1) + n;
	float Omegat = DispersionTable[Index] * TimeSeconds;

	float Cos = FMath::Cos(Omegat);
	float Sin = FMath::Sin(Omegat);

	//uint32 Stride;
	//if (Spectrum->GetSizeX() > (uint32)Index && SpectrumConj->GetSizeX() > (uint32)Index)
	//{
	//	FVector2D* SpectrumData = static_cast<FVector2D*>(RHILockTexture2D(Spectrum, 0, EResourceLockMode::RLM_ReadOnly, Stride, false));
	//	//SpectrumData += m * Stride / sizeof(FVector2D);
	//	float C0a = SpectrumData[Index].X*Cos - SpectrumData[Index].Y*Sin;
	//	float C0b = SpectrumData[Index].X*Sin - SpectrumData[Index].Y*Cos;

	//	TArray<FVector2D> AllData;
	//	AllData.SetNum((WaveSize + 1)*(WaveSize + 1));
	//	for (int32 i = 0; i < AllData.Num(); ++i)
	//	{
	//		AllData[i] = SpectrumData[i];
	//	}

	//	FVector2D* SpectrumConjData = static_cast<FVector2D*>(RHILockTexture2D(SpectrumConj, 0, EResourceLockMode::RLM_ReadOnly, Stride, false));
	//	//SpectrumConjData += m * Stride / sizeof(FVector2D);
 //		float C1a = SpectrumConjData[Index].X*Cos - SpectrumConjData[Index].Y*-Sin;
	//	float C1b = SpectrumConjData[Index].X*-Sin - SpectrumConjData[Index].Y*Cos;

	//	//Unlock
	//	RHIUnlockTexture2D(Spectrum, 0, false);
	//	RHIUnlockTexture2D(SpectrumConj, 0, false);

	//	return FVector2D(C0a + C1a, C0b + C1b);
	//}

	return FVector2D::ZeroVector;
}

float AFFTWaveSimulator::Dispersion(int32 n, int32 m)
{
	//float W_0 = 2.0f * PI / 200.f;  // Use this value, time will be slow, so that the wave will be slow too 
	float W_0 = 1.f;
	float KX = PI * (2 * n - WaveSize) / PatchLength; //k=2*PI*n/L
	float KY = PI * (2 * m - WaveSize) / PatchLength;
	// w=sqrt(g*|k|)
	return FMath::FloorToFloat(FMath::Sqrt(GRAVITY * FMath::Sqrt(KX * KX + KY * KY) / W_0)) * W_0;
}

void AFFTWaveSimulator::CreateWaveGrid()
{
	float MeshScale = 1.f;
	const bool bCanUseStaticMesh = bUseStaticMesh && WaveStaticMesh;
	if (bCanUseStaticMesh)
	{
		if (WaveMesh && WaveMesh->IsA<UProceduralMeshComponent>())
			WaveMesh->DestroyComponent();
		UStaticMeshComponent* NewMesh = nullptr;
		if (IsValid(WaveMesh) && WaveMesh->IsA<UStaticMeshComponent>())
		{
			NewMesh = Cast<UStaticMeshComponent>(WaveMesh);
		}
		else
		{
			NewMesh = NewObject<UStaticMeshComponent>(this);
			NewMesh->RegisterComponent();
		}
		NewMesh->SetStaticMesh(WaveStaticMesh);
		WaveMesh = NewMesh;
		SetRootComponent(WaveMesh);

		MeshScale = MeshGridLength / WaveStaticMeshGridSize;
	}
	else
	{
		if (WaveMesh && WaveMesh->IsA<UStaticMeshComponent>())
			WaveMesh->DestroyComponent();
		UProceduralMeshComponent* NewMesh = nullptr;
		if (IsValid(WaveMesh) && WaveMesh->IsA<UProceduralMeshComponent>())
		{
			NewMesh = Cast<UProceduralMeshComponent>(WaveMesh);
		}
		else
		{
			NewMesh = NewObject<UProceduralMeshComponent>(this);
			NewMesh->RegisterComponent();
		}
		TArray<int32> Triangles;
		TArray<FColor> Colors;
		TArray<FProcMeshTangent> Tangents;
		if (WaveMesh && WaveMesh->GetBodySetup())
			WaveMesh->GetBodySetup()->bNeverNeedsCookedCollisionData = true;
		CreateWaveGridMesh(HorizontalTileCount, VerticalTileCount, WaveSize + 1, WaveSize + 1, Triangles, WaveVertices, UVs, PatchLength);

		WaveNormals.SetNum((WaveSize + 1) * (WaveSize + 1));
		WavePosition.SetNum((WaveSize + 1) * (WaveSize + 1));

		if (NewMesh)
		{
			if (NewMesh->GetNumSections() > 0)
			{
				NewMesh->ClearAllMeshSections();
				NewMesh->CreateMeshSection(0, WaveVertices, Triangles, WaveNormals, UVs, Colors, Tangents, false);
			}
			else
				NewMesh->CreateMeshSection(0, WaveVertices, Triangles, WaveNormals, UVs, Colors, Tangents, false);
		}
		WaveMesh = NewMesh;
		SetRootComponent(WaveMesh);
		MeshScale = MeshGridLength / PatchLength;
	}
	if (GridMaterial && WaveMesh)
		WaveMesh->SetMaterial(0, GridMaterial);
	WaveMesh->SetWorldScale3D(MeshScale * FVector(1.f, 1.f, 1.f));
	if (UMaterialInstanceDynamic* DynMaterial = WaveMesh->CreateAndSetMaterialInstanceDynamic(0))
	{
		DynMaterial->SetScalarParameterValue(WaveDisplacementScale, MeshGridLength / PatchLength);
		DynMaterial->SetScalarParameterValue(WaveTexelOffset, 1.f / WaveSize);
		DynMaterial->SetScalarParameterValue(WaveGradientZ, PatchLength);
	}

	DispersionTable.SetNum((WaveSize + 1) * (WaveSize + 1));

	for (int32 i = 0; i < WaveSize + 1; ++i)
	{
		for (int32 j = 0; j < WaveSize + 1; ++j)
		{
			int32 Index = i * (WaveSize + 1) + j;
			DispersionTable[Index] = Dispersion(j, i);
		}
	}
}

void AFFTWaveSimulator::CreateResources()
{
	if(Spectrum) Spectrum->Initialize(sizeof(float) * 2, (WaveSize + 1) * (WaveSize + 1), EPixelFormat::PF_G32R32F, BUF_Static);
	if(SpectrumConj) SpectrumConj->Initialize(sizeof(float) * 2, (WaveSize + 1) * (WaveSize + 1), EPixelFormat::PF_G32R32F, BUF_Static);
	if(HeightBuffer) HeightBuffer->Initialize(sizeof(float) * 2, WaveSize, WaveSize * 2, EPixelFormat::PF_G32R32F);
	if(SlopeBuffer) SlopeBuffer->Initialize(sizeof(float) * 4, WaveSize, WaveSize * 2, EPixelFormat::PF_A32B32G32R32F);
	if(DisplacementBuffer) DisplacementBuffer->Initialize(sizeof(float) * 4, WaveSize, WaveSize * 2, EPixelFormat::PF_A32B32G32R32F);
	//FUnorderedAccessViewRHIRef TempTextureUAV = RHICreateUnorderedAccessView(TempTexture);

	ComputeRandomTable(WaveSize + 1, RandomTable);
	ComputeButterflyLookuptable(WaveSize, (int32)FMath::Log2(WaveSize), ButterflyLookupTable);

	RandomTableVB.SafeRelease();
	RandomTableSRV.SafeRelease();
	ButterflyLookupTableVB.SafeRelease();
	ButterflyLookupTableSRV.SafeRelease();
	DispersionTableVB.SafeRelease();
	DispersionTableSRV.SafeRelease();
	FRHIResourceCreateInfo CreateInfo;
	RandomTableVB = RHICreateVertexBuffer(RandomTable.Num() * sizeof(FVector2D), BUF_ShaderResource, CreateInfo);
	RandomTableSRV = RHICreateShaderResourceView(RandomTableVB, sizeof(FVector2D), PF_G32R32F);

	ButterflyLookupTableVB = RHICreateVertexBuffer(ButterflyLookupTable.Num() * sizeof(float), BUF_ShaderResource, CreateInfo);
	ButterflyLookupTableSRV = RHICreateShaderResourceView(ButterflyLookupTableVB, sizeof(float), PF_R32_FLOAT);

	DispersionTableVB = RHICreateVertexBuffer(DispersionTable.Num() * sizeof(float), BUF_ShaderResource, CreateInfo);
	DispersionTableSRV = RHICreateShaderResourceView(DispersionTableVB, sizeof(float), PF_R32_FLOAT);

	void* RandomTableData = RHILockVertexBuffer(RandomTableVB, 0, RandomTable.Num() * sizeof(FVector2D), RLM_WriteOnly);
	FPlatformMemory::Memcpy(RandomTableData, RandomTable.GetData(), RandomTable.Num() * sizeof(FVector2D));
	RHIUnlockVertexBuffer(RandomTableVB);

	void* ButterflyLockedData = RHILockVertexBuffer(ButterflyLookupTableVB, 0, ButterflyLookupTable.Num() * sizeof(float), RLM_WriteOnly);
	FPlatformMemory::Memcpy(ButterflyLockedData, ButterflyLookupTable.GetData(), ButterflyLookupTable.Num() * sizeof(float));
	RHIUnlockVertexBuffer(ButterflyLookupTableVB);
	
	void* DispersionTableData = RHILockVertexBuffer(DispersionTableVB, 0, DispersionTable.Num() * sizeof(float), RLM_WriteOnly);
	FPlatformMemory::Memcpy(DispersionTableData, DispersionTable.GetData(), DispersionTable.Num() * sizeof(float));
	RHIUnlockVertexBuffer(DispersionTableVB);
}

void AFFTWaveSimulator::ComputePositionAndNormal()
{
	//if (DrawNormal && 
	//	(int32)HeightBuffer->GetSizeX() >= WaveSize * WaveSize &&
	//	(int32)SlopeBuffer->GetSizeX() >= WaveSize * WaveSize && 
	//	(int32)DisplacementBuffer->GetSizeX() >= WaveSize * WaveSize && 
	//	WaveVertices.Num() >= (WaveSize + 1) * (WaveSize + 1)
	//	)
	//{
	//	uint32 Stride;
	//	FVector2D* HeightBufferData = static_cast<FVector2D*>(RHILockTexture2D(HeightBuffer, 0, EResourceLockMode::RLM_ReadOnly, Stride, false));
	//	HeightBufferData += Stride / sizeof(FVector2D);
	//	FVector4* SlopeBufferData = static_cast<FVector4*>(RHILockTexture2D(SlopeBuffer, 0, EResourceLockMode::RLM_ReadOnly, Stride, false));
	//	SlopeBufferData += Stride / sizeof(FVector4);
	//	FVector4* DisplacementBufferData = static_cast<FVector4*>(RHILockTexture2D(DisplacementBuffer, 0, EResourceLockMode::RLM_ReadOnly, Stride, false));
	//	DisplacementBufferData += Stride / sizeof(FVector4);

	//	int32 Sign;
	//	static float Signs[2] = { 1.f,-1.f };
	//	float Lambda = -1.f;
	//	for (int32 m = 0; m < WaveSize; ++m)
	//	{
	//		for (int32 n = 0; n < WaveSize; ++n)
	//		{
	//			int32 Index = m * WaveSize + n;
	//			int32 Index1 = m * (WaveSize + 1) + n;

	//			Sign = (int32)Signs[(n + m) & 1];

	//			// Get height
	//			WaveVertices[Index1].Z = HeightBufferData[Index].X * Sign;

	//			// Get displacement
	//			WaveVertices[Index1].X = WavePosition[Index1].X + DisplacementBufferData[Index].Z * Lambda * Sign;
	//			WaveVertices[Index1].Y = WavePosition[Index1].Y + DisplacementBufferData[Index].X * Lambda * Sign;
	//			
	//			// Get normal
	//			FVector Normal(-SlopeBufferData[Index].Z *Sign, -SlopeBufferData[Index].X *Sign, 1.f);
	//			Normal.Normalize();

	//			WaveNormals[Index1].X = Normal.X;
	//			WaveNormals[Index1].Y = Normal.Y;
	//			WaveNormals[Index1].Z = Normal.Z;

	//			int32 TileIndex;
	//			//Handle tiling
	//			if (n == 0 && m == 0)
	//			{
	//				TileIndex = Index1 + WaveSize + (WaveSize + 1)*WaveSize;
	//			}
	//			else if (n == 0)
	//			{
	//				TileIndex = Index1 + WaveSize;
	//			}
	//			else if (m == 0)
	//			{
	//				TileIndex = Index1 + (WaveSize + 1) * WaveSize;
	//			}
	//			else
	//				continue;

	//			WaveVertices[TileIndex].Z = HeightBufferData[Index].X * Sign;
	//			WaveVertices[TileIndex].X = WavePosition[TileIndex].X + DisplacementBufferData[Index].Z * Lambda * Sign;
	//			WaveVertices[TileIndex].Y = WavePosition[TileIndex].Y + DisplacementBufferData[Index].X * Lambda * Sign;
	//			
	//			WaveNormals[TileIndex].X = Normal.X;
	//			WaveNormals[TileIndex].Y = Normal.Y;
	//			WaveNormals[TileIndex].Z = Normal.Z;
	//		}
	//	}

	//	// Unlock the buffers
	//	RHIUnlockTexture2D(HeightBuffer, 0, false);
	//	RHIUnlockTexture2D(SlopeBuffer, 0, false);
	//	RHIUnlockTexture2D(DisplacementBuffer, 0, false);
	//}
}

FVector2D AFFTWaveSimulator::GetWaveDimension() const
{
	const bool bCanUseStaticMesh = bUseStaticMesh && WaveStaticMesh;
	if (bCanUseStaticMesh)
		return FVector2D(WaveStaticMeshGridSize * WaveSize, WaveStaticMeshGridSize * WaveSize);
	else
		return FVector2D(HorizontalTileCount * MeshGridLength * WaveSize, VerticalTileCount * MeshGridLength * WaveSize);
}

static FName Name_UseStaticMesh = GET_MEMBER_NAME_CHECKED(AFFTWaveSimulator, bUseStaticMesh);
static FName Name_HorizontalTileCount = GET_MEMBER_NAME_CHECKED(AFFTWaveSimulator, HorizontalTileCount);
static FName Name_VerticalTileCount = GET_MEMBER_NAME_CHECKED(AFFTWaveSimulator, VerticalTileCount);
static FName Name_MeshGridLength = GET_MEMBER_NAME_CHECKED(AFFTWaveSimulator, MeshGridLength);
static FName Name_WaveSize = GET_MEMBER_NAME_CHECKED(AFFTWaveSimulator, WaveSize);
static FName Name_PatchLength = GET_MEMBER_NAME_CHECKED(AFFTWaveSimulator, PatchLength);
static FName Name_WaveAmplitude = GET_MEMBER_NAME_CHECKED(AFFTWaveSimulator, WaveAmplitude);
static FName Name_WindSpeed = GET_MEMBER_NAME_CHECKED(AFFTWaveSimulator, WindSpeed);

static int32 MacroNum = (void(0), 1);

#if WITH_EDITOR
void AFFTWaveSimulator::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	UProperty* MemberPropertyThatChanged = PropertyChangedEvent.MemberProperty;
	const FName MemberPropertyName = MemberPropertyThatChanged != NULL ? MemberPropertyThatChanged->GetFName() : NAME_None;

	bool bWaveProperyChanged = MemberPropertyName == Name_UseStaticMesh ||
							   MemberPropertyName == Name_HorizontalTileCount ||
							   MemberPropertyName == Name_VerticalTileCount ||
							   MemberPropertyName == Name_MeshGridLength ||
							   MemberPropertyName == Name_WaveSize ||
							   MemberPropertyName == Name_PatchLength ||
							   MemberPropertyName == Name_WaveAmplitude ||
							   MemberPropertyName == Name_WindSpeed;
	if (bWaveProperyChanged)
	{
		bHasInit = false;
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif
