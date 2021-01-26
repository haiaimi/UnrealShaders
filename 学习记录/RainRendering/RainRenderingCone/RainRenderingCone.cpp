// Fill out your copyright notice in the Description page of Project Settings.


#include "Weather/Rain/RainRenderingCone.h"
#include "ProceduralMeshComponent.h"
#include "UnrealClient.h"
#include "RenderTargetPool.h"
#include "Engine/Texture2D.h"
#include "AssetRegistryModule.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/KismetSystemLibrary.h"
#include "EngineUtils.h"
#include "Components/RainDepthComponent.h"
#include "Kismet/GameplayStatics.h"

#if WITH_EDITOR
#include "FileHelpers.h"
#include "EditorViewportClient.h"
#endif

static TAutoConsoleVariable<int32> CVarRenderRain(
	TEXT("r.RenderRain"), 1,
	TEXT("Weather render rain.\n"),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarRenderRainLevel3(
	TEXT("r.RenderRainLevel3"), 1,
	TEXT("Weather rendering thrid level of rain.\n"),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarRainDensity(
	TEXT("r.RainDensity"), 0.4,
	TEXT("Rain Density.\n"),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarRainDropSpeedLevel1(
	TEXT("r.RainDropSpeedLevel1"), -0.2f,
	TEXT("Rain Drop Speed Level 1.\n"),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarRainDropSpeedLevel2(
	TEXT("r.RainDropSpeedLevel2"), -0.1f,
	TEXT("Rain Drop Speed Level 2.\n"),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarRainSplashSequenceSpeed(
	TEXT("r.RainSplashSequenceSpeed"), 20.f,
	TEXT("Rain Splash Sequence Speed\n"),
	ECVF_Default);


// Sets default values
ARainRenderingCone::ARainRenderingCone() : 
	CurrentSplashOffset(ForceInitToZero),
	PreViewLocation(ForceInitToZero),
	PreViewYaw(0.f)

{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = ETickingGroup::TG_PostUpdateWork;
	ConeMeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ProceduralConeMesh"));
	RainSplashMeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("RainSplashMesh"));
	RootComponent = ConeMeshComponent;

	RainDepthStoragePath = TEXT("/Game/Arts/Effects/Texture/");
}

// Called when the game starts or when spawned
void ARainRenderingCone::BeginPlay()
{
	Super::BeginPlay();
	
	GenerateRainSplashMesh();
	MakeConeProceduralMesh();
}

// Called every frame
void ARainRenderingCone::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	RainSplashMeshComponent->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);

	if (CVarRenderRain.GetValueOnGameThread())
	{
		ConeMeshComponent->SetVisibility(true);
	}
	else
	{
		ConeMeshComponent->SetVisibility(false);
	}

	FVector ViewPos;
	FRotator ViewRot;
#if WITH_EDITOR
	FViewport* ActiveViewport = GEditor->GetActiveViewport();
	FEditorViewportClient* EditorViewClient = (ActiveViewport != nullptr) ? (FEditorViewportClient*)ActiveViewport->GetClient() : nullptr;
	
	const bool bIsInPIE = GetWorld()->IsPlayInEditor();
	if (EditorViewClient && bDebugInEditorViewport && !bIsInPIE)
	{
		ViewPos = EditorViewClient->GetViewLocation();
		ViewRot = EditorViewClient->GetViewRotation();
	}
	else if(bIsInPIE)
	{
		APlayerCameraManager* CurrentCamera = UGameplayStatics::GetPlayerCameraManager(this, 0);
		if (CurrentCamera)
		{
			ViewPos = CurrentCamera->GetCameraLocation();
			ViewRot = CurrentCamera->GetCameraRotation();
		}
	}
#else
	APlayerCameraManager* CurrentCamera = UGameplayStatics::GetPlayerCameraManager(this, 0);
	if (CurrentCamera)
	{
		ViewPos = CurrentCamera->GetCameraLocation();
		ViewRot = CurrentCamera->GetCameraRotation();
	}
#endif

	if (ConeMeshComponent)
	{
		ConeMeshComponent->SetWorldLocation(ViewPos + ViewRot.Vector().GetSafeNormal2D() * ViewOffset);
			
		ConeMeshComponent->SetWorldRotation(ViewRot.Vector().GetSafeNormal2D().ToOrientationRotator());
	}

	if (PreViewLocation == FVector::ZeroVector)
	{
		PreViewLocation = ViewPos;
	}

	if (!CurrentRainCapture.IsValid())
	{
		for (TActorIterator<ARainDepthCapture> Iter(GetWorld()); Iter; ++Iter)
		{
			CurrentRainCapture = *Iter;
			CaptureViewProjectionMatrix = CurrentRainCapture->GetComponent()->GetCaptureProjectMatrix();
			break;
		}
	}

	float Sin = 0.f, Cos = 0.f;
	FMath::SinCos(&Sin, &Cos, FMath::DegreesToRadians(ViewRot.Yaw + 180.f));
	FVector2D RotationOffset = FVector2D(Cos, Sin) * -0.5f;

	FMath::SinCos(&Sin, &Cos, FMath::DegreesToRadians(PreViewYaw + 180.f));
	FVector2D PreRotationOffset = FVector2D(Cos, Sin) * -0.5f;

	float DeltaViewYaw = FRotator::NormalizeAxis(ViewRot.Yaw - PreViewYaw);
	FVector Offset = (FRotator(0.f, ViewRot.Yaw, 0.f).Vector() - FRotator(0.f, PreViewYaw, 0.f).Vector()) * 0.5f;
	FVector DeltaPos = (ViewPos - PreViewLocation);
	float OffsetX = DeltaPos.X / (RainSplashRadius * 2.f);
	float OffsetY = DeltaPos.Y / (RainSplashRadius * 2.f);

	CurrentSplashOffset += FVector2D(OffsetX, OffsetY);
	CurrentSplashOffset += FVector2D(Offset.X, Offset.Y);
	CurrentSplashOffset.X = FMath::Frac(CurrentSplashOffset.X);
	CurrentSplashOffset.Y = FMath::Frac(CurrentSplashOffset.Y);
	
	if (!RainDepthTexture2D)
	{
		FString MapName = GetWorld()->GetMapName();
		if (MapName.Contains(TEXT("UEDPIE_")))
		{
			FString Tmp;
			MapName.Split(TEXT("_"), &Tmp, &MapName);
			MapName.Split(TEXT("_"), &Tmp, &MapName);
		}

		RainDepthTexture2D = LoadObject<UTexture2D>(nullptr, *(RainDepthStoragePath + TEXT("TX_RainDepth_") + MapName));
	}

	if (RainSplashMeshComponent)
	{
		UMaterialInstanceDynamic* MaterialInst = RainSplashMeshComponent->CreateAndSetMaterialInstanceDynamic(0);
		if (MaterialInst)
		{
			MaterialInst->SetVectorParameterValue(TEXT("MovingOffset"), FLinearColor(CurrentSplashOffset.X, CurrentSplashOffset.Y, 0.f, 0.f));
			MaterialInst->SetVectorParameterValue(TEXT("RotationOffset"), FLinearColor(RotationOffset.X, RotationOffset.Y, 0.f, 0.f));
			MaterialInst->SetScalarParameterValue(TEXT("Time"), GetWorld()->TimeSeconds);
			MaterialInst->SetScalarParameterValue(TEXT("SequenceAnimSpeed"), CVarRainSplashSequenceSpeed.GetValueOnGameThread());
			MaterialInst->SetScalarParameterValue(TEXT("RainSplashRadius"), RainSplashRadius);

			if (CurrentRainCapture.IsValid())
				MaterialInst->SetVectorParameterValue(TEXT("DepthViewSizePos"), FLinearColor(CurrentRainCapture.Get()->GetComponent()->CaptureViewWidth, CurrentRainCapture.Get()->GetComponent()->CaptureViewHeight, CurrentRainCapture->GetActorLocation().X, CurrentRainCapture->GetActorLocation().Y));

			if (RainDepthTexture2D)
			{
				MaterialInst->SetTextureParameterValue(TEXT("RainDepthTexture"), RainDepthTexture2D);
			}
		}
	}

	if (ConeMeshComponent)
	{
		UMaterialInstanceDynamic* MaterialInst = ConeMeshComponent->CreateAndSetMaterialInstanceDynamic(0);
		if (MaterialInst)
		{
			if (CurrentRainCapture.IsValid())
			{
				MaterialInst->SetVectorParameterValue(TEXT("DepthViewSizePos"), FLinearColor(CurrentRainCapture.Get()->GetComponent()->CaptureViewWidth, CurrentRainCapture.Get()->GetComponent()->CaptureViewHeight, CurrentRainCapture->GetActorLocation().X, CurrentRainCapture->GetActorLocation().Y));
				MaterialInst->SetVectorParameterValue(TEXT("CaptureMatrixRow0"), FLinearColor(CaptureViewProjectionMatrix.M[0][0], CaptureViewProjectionMatrix.M[0][1], CaptureViewProjectionMatrix.M[0][2], CaptureViewProjectionMatrix.M[0][3]));
				MaterialInst->SetVectorParameterValue(TEXT("CaptureMatrixRow1"), FLinearColor(CaptureViewProjectionMatrix.M[1][0], CaptureViewProjectionMatrix.M[1][1], CaptureViewProjectionMatrix.M[1][2], CaptureViewProjectionMatrix.M[1][3]));
				MaterialInst->SetVectorParameterValue(TEXT("CaptureMatrixRow2"), FLinearColor(CaptureViewProjectionMatrix.M[2][0], CaptureViewProjectionMatrix.M[2][1], CaptureViewProjectionMatrix.M[2][2], CaptureViewProjectionMatrix.M[2][3]));
				MaterialInst->SetVectorParameterValue(TEXT("CaptureMatrixRow3"), FLinearColor(CaptureViewProjectionMatrix.M[3][0], CaptureViewProjectionMatrix.M[3][1], CaptureViewProjectionMatrix.M[3][2], CaptureViewProjectionMatrix.M[3][3]));
			}

			MaterialInst->SetScalarParameterValue(TEXT("RainDensity"), CVarRainDensity.GetValueOnGameThread());
			MaterialInst->SetScalarParameterValue(TEXT("RainDropSpeedLevel1"), CVarRainDropSpeedLevel1.GetValueOnGameThread());
			MaterialInst->SetScalarParameterValue(TEXT("RainDropSpeedLevel2"), CVarRainDropSpeedLevel2.GetValueOnGameThread());

			if (RainDepthTexture2D)
			{
				MaterialInst->SetTextureParameterValue(TEXT("RainDepthTexture"), RainDepthTexture2D);
			}
		}
	}

	PreViewLocation = ViewPos;
	PreViewYaw = ViewRot.Yaw;
}

inline float NormalDistributionRandom2(float CenterValue, float StandardDev)
{
	float U = 0.0, V = 0.0, W = 0.0, C = 0.0;
	do {
		U = FMath::RandRange(0.f, 1.f) * 2 - 1.0;
		V = FMath::RandRange(0.f, 1.f) * 2 - 1.0;
		W = U * U + V * V;
	} while (W == 0.0 || W >= 1.0);
	C = FMath::Sqrt((-2 * FMath::Loge(W)) / W);

	return CenterValue + U * C * StandardDev;
}

void GenerateRainDropTexture(const FString& Savepath, int32 RainDropNum, int32 MaxRainDropPixelCount, int32 MotionBlurRadius)
{
	FIntPoint TextureSize = FIntPoint(512, 512);

	TArray<FFloat16Color> RainDropData;
	TArray<FFloat16Color> RainDropDataResult;
	RainDropData.SetNumZeroed(TextureSize.X * TextureSize.Y);
	RainDropDataResult.SetNumZeroed(TextureSize.X * TextureSize.Y);
	
	for (int32 i = 0; i < RainDropNum; ++i)
	{
		float U = FMath::RandRange(0.f, 1.f);
		float V = FMath::RandRange(0.f, 1.f);
		float RainDropIntensity = FMath::Max(0.f, NormalDistributionRandom2(0.4f, 0.3f));
		RainDropIntensity = FMath::Pow(RainDropIntensity, 4.f);
		//float RainDropVirtualDepth = FMath::Clamp(NormalDistributionRandom2(0.5f, 0.5f), 0.f, 1.f);
		float RainDropVirtualDepth = FMath::RandRange(0.f, 1.f);
		int32 RainDropPixelNum = FMath::Rand() % MaxRainDropPixelCount;
		int32 CoordX = U * (float)TextureSize.X;
		int32 CoordY = V * (float)TextureSize.Y;

		for (int32 j = 0; j <= RainDropPixelNum; ++j)
		{
			if (CoordY + j < TextureSize.Y)
			{
				RainDropData[(CoordY + j) * TextureSize.X + CoordX].R = FFloat16(RainDropIntensity);
				RainDropData[(CoordY + j) * TextureSize.X + CoordX].B = RainDropVirtualDepth;
			}
		}
	}

	for (int32 i = 0; i < TextureSize.Y; ++i)
	{
		for (int32 j = 0; j < TextureSize.X; ++j)
		{
			float IntensitySum = 0;
			float DepthSum = 0;
			int32 DepthCount = 0;
			IntensitySum += RainDropData[i * TextureSize.X + j].R;
			for (int32 k = 1; k <= MotionBlurRadius; ++k)
			{
				if ((i - k) >= 0)
				{
					IntensitySum += RainDropData[(i - k) * TextureSize.X + j].R;
					DepthSum += RainDropData[(i - k) * TextureSize.X + j].B;
					if(RainDropData[(i - k) * TextureSize.X + j].B > 0.f)
						DepthCount++;
				}

				if ((i + k) < TextureSize.Y)
				{
					IntensitySum += RainDropData[(i + k) * TextureSize.X + j].R;
					DepthSum += RainDropData[(i + k) * TextureSize.X + j].B;
					if(RainDropData[(i + k) * TextureSize.X + j].B > 0.f)
						DepthCount++;
				}
			}

			const float CurVirtualDepth = FMath::Clamp(DepthSum / (float)(DepthCount), 0.f, 1.f); 
			RainDropDataResult[i * TextureSize.X + j].R = FMath::Clamp(IntensitySum / (float)(1 + MotionBlurRadius * 2), 0.f, 1.f);
			RainDropDataResult[i * TextureSize.X + j].B = FMath::Max(RainDropDataResult[i * TextureSize.X + j].B.GetFloat(), CurVirtualDepth);
			if (j - 1 >= 0)
			{
				RainDropDataResult[i * TextureSize.X + (j - 1)].B = FMath::Max(RainDropDataResult[i * TextureSize.X + (j - 1)].B.GetFloat(), CurVirtualDepth);
			}
			if ((j + 1) < TextureSize.X)
			{
				RainDropDataResult[i * TextureSize.X + (j + 1)].B = FMath::Max(RainDropDataResult[i * TextureSize.X + (j + 1)].B.GetFloat(), CurVirtualDepth);
			}

		}
	}
#if WITH_EDITOR
	FString TextureName = TEXT("TX_RainDrop01");
	FString PackageName = Savepath + TextureName;
	UPackage* Package = CreatePackage(NULL, *PackageName);
	Package->FullyLoad();
	UTexture2D* RainDropTextureSrc = NewObject<UTexture2D>(Package, *TextureName, RF_Public | RF_Standalone | RF_MarkAsRootSet);
	RainDropTextureSrc->AddToRoot();
	RainDropTextureSrc->PlatformData = new FTexturePlatformData();
	RainDropTextureSrc->PlatformData->SizeX = TextureSize.X;
	RainDropTextureSrc->PlatformData->SizeY = TextureSize.Y;
	RainDropTextureSrc->PlatformData->SetNumSlices(1);
	RainDropTextureSrc->PlatformData->PixelFormat = EPixelFormat::PF_FloatRGBA;
	RainDropTextureSrc->CompressionQuality = ETextureCompressionQuality::TCQ_Medium;
	RainDropTextureSrc->MipGenSettings = TextureMipGenSettings::TMGS_NoMipmaps;
	RainDropTextureSrc->SRGB = false;
	RainDropTextureSrc->CompressionSettings = TC_HDR;

	int32 Index = 0;
	//if (!CurTexture)
	{
		Index = RainDropTextureSrc->PlatformData->Mips.Add(new FTexture2DMipMap());
	}
	FTexture2DMipMap* Mip = &RainDropTextureSrc->PlatformData->Mips[Index];
	Mip->SizeX = TextureSize.X;
	Mip->SizeY = TextureSize.Y;
	Mip->SizeZ = 1;

	// Lock the texture so it can be modified
	Mip->BulkData.Lock(LOCK_READ_WRITE);
	uint8* TextureData = (uint8*)Mip->BulkData.Realloc(TextureSize.X * TextureSize.Y * 1 * sizeof(FFloat16Color));
	FMemory::Memcpy(TextureData, RainDropDataResult.GetData(), RainDropDataResult.Num() * sizeof(FFloat16Color));

	Mip->BulkData.Unlock();
	RainDropTextureSrc->Source.Init(TextureSize.X, TextureSize.Y, 1, 1, ETextureSourceFormat::TSF_RGBA16F, (uint8*)RainDropDataResult.GetData());
	RainDropTextureSrc->UpdateResource();
	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(RainDropTextureSrc);

	//FlushRenderingCommands();
	FString PackageFileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	TArray<UPackage*> Packages = { Package };
	FEditorFileUtils::PromptForCheckoutAndSave(Packages, true, /*bPromptToSave=*/ false);
#endif
}

void ARainRenderingCone::MakeConeProceduralMesh()
{
	//static const uint32 ConeEdgeVertexCount = 10;
	if (ConeMeshComponent)
	{
		static TArray<int32> Triangles; 
		static TArray<FVector> Vertices;
		static TArray<FVector2D> UV1s;
		static TArray<FVector2D> UV2s;

		Vertices.Empty();
		Vertices.Reserve(ConeEdgeVertexCount * 3 + 1);
		Triangles.Empty();
		Triangles.Reserve(ConeEdgeVertexCount * 2 * 3);
		UV1s.Empty();
		UV1s.Reserve(ConeEdgeVertexCount * 3 + 1);

		UV2s.Empty();
		UV2s.Reserve(ConeEdgeVertexCount * 3 + 1);
		
		 const float DeltaUV = 1.f / ConeEdgeVertexCount;
		// Add Top Vertices
		for (int32 i = 0; i < ConeEdgeVertexCount; ++i)
		{
			Vertices.Add(FVector(0.f, 0.f, 5.f));
			UV1s.Add(FVector2D(DeltaUV * (i + 1), 0.f));
			UV2s.Add(FVector2D(RainFadeOutAlpha, 0.f));
		}

		// Add Middle Vertices
		const float DeltaAngle = 2 * PI / ConeEdgeVertexCount;
		for (int32 i = 0; i < ConeEdgeVertexCount + 1; ++i)
		{
			float X = 0, Y = 0;
			FMath::SinCos(&X, &Y, DeltaAngle * i);
			Vertices.Add(FVector(X, Y, 0.f));
			UV1s.Add(FVector2D(DeltaUV * i, 0.5f));
			UV2s.Add(FVector2D(1.f, 0.f));
		}

		// Add Bottom Vertices
		for (int32 i = 0; i < ConeEdgeVertexCount; ++i)
		{
			Vertices.Add(FVector(0.f, 0.f, -5.f));
			UV1s.Add(FVector2D(DeltaUV * (i + 1), 1.f));
			UV2s.Add(FVector2D(RainFadeOutAlpha, 0.f));
		}

		// Add Vertex Indices
		for (int32 i = 0; i < ConeEdgeVertexCount; ++i)
		{
			Triangles.Add(i);
			Triangles.Add(i + ConeEdgeVertexCount + 1);
			Triangles.Add(i + ConeEdgeVertexCount);

			Triangles.Add(i + ConeEdgeVertexCount);
			Triangles.Add(i + ConeEdgeVertexCount + 1);
			Triangles.Add(i + 2 * ConeEdgeVertexCount + 1);
		}

		TArray<FVector> Normals;
		TArray<FColor> Colors;
		TArray<FProcMeshTangent> Tangents;
		static TArray<FVector2D> EmptyUVs;
		ConeMeshComponent->CreateMeshSection(0, Vertices, Triangles, Normals, UV1s, UV2s, EmptyUVs, EmptyUVs, Colors, Tangents, false);
	}
}

void ARainRenderingCone::SaveRainDepthTexture()
{
	
}

#if WITH_EDITOR
void ARainRenderingCone::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);

	if(PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_STRING_CHECKED(ARainRenderingCone, ConeEdgeVertexCount)
	   || PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_STRING_CHECKED(ARainRenderingCone, RainFadeOutAlpha))
	{
		MakeConeProceduralMesh();
	}

	if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_STRING_CHECKED(ARainRenderingCone, bGenerateRainDrop)
		|| PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_STRING_CHECKED(ARainRenderingCone, MaxRainDropPixelCount)
		|| PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_STRING_CHECKED(ARainRenderingCone, RainDropMotionBlurRadius)
		|| PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_STRING_CHECKED(ARainRenderingCone, RainDropNum))
	{
		GenerateRainDropTexture(RainDepthStoragePath, RainDropNum, MaxRainDropPixelCount, RainDropMotionBlurRadius);
	}
	
	if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_STRING_CHECKED(ARainRenderingCone, RainSplashCount)
	    || PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_STRING_CHECKED(ARainRenderingCone, RainSplashRadius))
	{
		GenerateRainSplashMesh();
	}
}
#endif

void ARainRenderingCone::GenerateNoiseForRainDrop()
{
	if (NoiseMaterial && NoiseRenderTarget)
	{
		UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, NoiseRenderTarget, NoiseMaterial);
	}
}

void ARainRenderingCone::GenerateRainSplashMesh()
{
	if (RainSplashMeshComponent)
	{
		static TArray<int32> Triangles;
		static TArray<FVector> Vertices;
		static TArray<FVector2D> UV1s;
		static TArray<FVector2D> UV2s;
		static TArray<FVector2D> UV3s;

		Vertices.Empty();
		Vertices.Reserve(4);
		Triangles.Empty();
		Triangles.Reserve(6);
		UV1s.Empty();
		UV1s.Reserve(4);
		UV2s.Empty();
		UV2s.Reserve(4);
		UV3s.Empty(4);
		UV3s.Reserve(4);

		for (int32 i = 0; i < RainSplashCount; ++i)
		{
			float PosX = FMath::RandRange(-10.f, 10.f);
			float PosY = FMath::RandRange(-10.f, 10.f);
			float SequenceAnimOffset = FMath::RandRange(0.f, 1000.f);

			float SplashSize = FMath::Clamp(NormalDistributionRandom2(0.8f, 0.1f), 0.6f, 1.f);

			Vertices.Add(FVector(PosX, PosY, 0.f));
			Vertices.Add(FVector(PosX, PosY, 0.f));
			Vertices.Add(FVector(PosX, PosY, 0.f));
			Vertices.Add(FVector(PosX, PosY, 0.f));

			UV1s.Add(FVector2D(0.f, 0.f));
			UV1s.Add(FVector2D(1.f, 0.f));
			UV1s.Add(FVector2D(0.f, 1.f));
			UV1s.Add(FVector2D(1.f, 1.f));

			UV2s.Add(FVector2D(-SplashSize, SplashSize));
			UV2s.Add(FVector2D(SplashSize, SplashSize));
			UV2s.Add(FVector2D(-SplashSize, -SplashSize));
			UV2s.Add(FVector2D(SplashSize, -SplashSize));

			UV3s.Add(FVector2D(SequenceAnimOffset, 0.f));
			UV3s.Add(FVector2D(SequenceAnimOffset, 0.f));
			UV3s.Add(FVector2D(SequenceAnimOffset, 0.f));
			UV3s.Add(FVector2D(SequenceAnimOffset, 0.f));

			Triangles.Add(i * 4 + 0);
			Triangles.Add(i * 4 + 2);
			Triangles.Add(i * 4 + 1);
			Triangles.Add(i * 4 + 1);
			Triangles.Add(i * 4 + 2);
			Triangles.Add(i * 4 + 3);
		}

		TArray<FVector> Normals;
		TArray<FColor> Colors;
		TArray<FProcMeshTangent> Tangents;
		TArray<FVector2D> EmptyUV;
		RainSplashMeshComponent->CreateMeshSection(0, Vertices, Triangles, Normals, UV1s, UV2s, UV3s, EmptyUV, Colors, Tangents, false);
		FProcMeshSection* MeshSection = RainSplashMeshComponent->GetProcMeshSection(0);
		MeshSection->SectionLocalBox = FBox(GetActorLocation() + FVector(1.f, 1.f, 1.f) * (-RainSplashRadius), GetActorLocation() + FVector(1.f, 1.f, 1.f) * RainSplashRadius);
		RainSplashMeshComponent->SetProcMeshSection(0, *MeshSection);
	}
}

void ARainRenderingCone::ComputeRainSplashOffset()
{
	
}
