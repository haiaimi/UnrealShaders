// Fill out your copyright notice in the Description page of Project Settings.


#include "ShadowFakeryInst.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Engine/DirectionalLight.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GenerateDistanceFieldTexture.h"
#include "ShadowFakeryStaticMeshComponent.h"
#include "InstancedFoliageActor.h"
#include "ShadowFakeryFoliageSMComponent.h"

float GSunYaw = 0.f;
FVector GLightDirWithSize;
FName GSunYawName = TEXT("SunYaw");
FName GSunDirectionName = TEXT("SunForwardDirection");

static TAutoConsoleVariable<bool> CVarUseShadowFakery(
	TEXT("r.UseShadowFakery"),
	true,
	TEXT("b"),
	ECVF_Default);

// Sets default values
AShadowFakeryInst::AShadowFakeryInst()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	ObjectMeshCompent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Object"));
	//ShadowMeshCompent = CreateDefaultSubobject<UShadowFakeryStaticMeshComponent>(TEXT("Shadow"));
	RootComponent = ObjectMeshCompent;
	//ShadowMeshCompent->SetupAttachment(RootComponent);
	ShadowDistanceFieldSize = 512;
	SceneLight = nullptr;
	ShadowMaskCutOffset = 90.f;
	SunYawParam = TEXT("SunYaw");
	SunDirectionParam = TEXT("SunForwardDirection");
	bHasInit = false;
	bSwitchShadowFakery = false;
}

// Called when the game starts or when spawned
void AShadowFakeryInst::BeginPlay()
{
	Super::BeginPlay();
	
}

#if WITH_EDITOR
void AShadowFakeryInst::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

// Called every frame
void AShadowFakeryInst::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	AInstancedFoliageActor* FoliageActor = AInstancedFoliageActor::GetInstancedFoliageActorForCurrentLevel(GetWorld(), true);
	TUniqueObj<FFoliageInfo>* CurFoliageInfo = FoliageActor->FoliageInfos.Find(CurFoliageType);
	
	if (CurFoliageInfo && !bHasInit && ShadowMeshCompentType)
	{
		FBox Box(FVector(-25200.f, -25200.f, 0.f), FVector(25200.f, 25200.f, 10000.f));
		TArray<FTransform> OutTransforms;
		FoliageActor->GetOverlappingBoxTransforms(CurFoliageType, Box, OutTransforms);
		ShadowFakeryStaticMesh = NewObject<UShadowFakeryStaticMeshComponent>(this, ShadowMeshCompentType);
		ShadowFakeryStaticMesh->RegisterComponent();
		ShadowFakeryStaticMesh->NumCustomDataFloats = 5;
		ShadowFakeryStaticMesh->SetWorldLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);

		for (auto& Iter : OutTransforms)
		{
			ShadowFakeryStaticMesh->AddInstance(FTransform(Iter.GetRotation(), Iter.GetLocation() + FVector::UpVector * 20.f));
		}
		bHasInit = true;
	}

	UShadowFakeryFoliageSMComponent* CurFoliage = FoliageActor->FindComponentByClass<UShadowFakeryFoliageSMComponent>();
	if (CurFoliage && bSwitchShadowFakery != CVarUseShadowFakery.GetValueOnGameThread())
	{
		if (!CVarUseShadowFakery.GetValueOnGameThread())
		{
			CurFoliage->CastShadow = true;
			CurFoliage->MarkRenderStateDirty();
			CurFoliage->bCastDynamicShadow = true;
		}
		else
		{
			CurFoliage->CastShadow = false;
			CurFoliage->MarkRenderStateDirty();
			CurFoliage->bCastDynamicShadow = false;
		}
		bSwitchShadowFakery = CVarUseShadowFakery.GetValueOnGameThread();
		for (auto Iter : AllShadowStaticMesh)
			Iter->SetVisibility(bSwitchShadowFakery);
	}

	//UMaterialInstanceDynamic* MaterialInst = ShadowMeshCompent->CreateDynamicMaterialInstance(0);

	if (!SceneLight)
	{
		for (TActorIterator<ADirectionalLight> Iter(GetWorld()); Iter; ++Iter)
		{
			SceneLight = *Iter;
			break;
		}
	}

	if (SceneLight/* && MaterialInst*/)
	{
		const float OffsetRadian = FMath::DegreesToRadians(ShadowMaskCutOffset);
		MaskCutDir = FVector(FMath::Cos(OffsetRadian), FMath::Sin(OffsetRadian), 0.f);
		const FVector CutDirWS = GetActorTransform().TransformVectorNoScale(MaskCutDir);
		const FVector UpDirWS = GetActorUpVector();
		const FVector RightDir = FVector::CrossProduct(CutDirWS, UpDirWS);
		
		FRotator Rotation = SceneLight->GetActorRotation();
		const FVector LightDir = Rotation.Vector();
		float SunYaw = (90.f - FMath::Clamp(FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(-LightDir, UpDirWS))), 0.f, 90.f)) * FMath::Sign(FVector::DotProduct(-LightDir, CutDirWS));

		if (FMath::IsNearlyZero(FMath::Abs(SunYaw) - 90.f))
			SunYaw = FMath::Sign(SunYaw) * 89.000f;

		//UE_LOG(LogTemp, Log, TEXT("Current Sun Yaw: %4.4f"), SunYaw);
		
		const FVector LightSize = LightDir.GetSafeNormal2D() * FMath::Abs(FMath::Tan(FMath::DegreesToRadians(90.f - FMath::Abs(SunYaw))));
		//ShadowMeshCompent->UpdateShadowState(LightSize, 1500, 1500);

		const TArray<float> CustomData = { LightSize.X, LightSize.Y, SunYaw, 3000.f, 0 };
		if (ShadowFakeryStaticMesh)
		{
			for (int32 i = 0; i < ShadowFakeryStaticMesh->GetInstanceCount(); ++i)
			{
				ShadowFakeryStaticMesh->SetCustomData(i, CustomData, true);
			}
		}
		
		GSunYaw = SunYaw;
		GLightDirWithSize = LightSize;
		/*MaterialInst->SetScalarParameterValue(SunYawParam, SunYaw);
		MaterialInst->SetVectorParameterValue(SunDirectionParam, FLinearColor(LightDir.GetSafeNormal2D()) * FMath::Abs(1.f / FMath::Tan(FMath::DegreesToRadians(FMath::Abs(SunYaw)))));*/
		//MaterialInst->SetVectorParameterValue(SunDirectionParam, FLinearColor(LightDir.GetSafeNormal2D()) * (1.f - FMath::Abs(SunYaw) / 90.f) * 5.f);
	}
}

void AShadowFakeryInst::GenerateShadowDistanceField()
{
	if (ObjectMeshCompent)
		UGenerateDistanceFieldTexture::GenerateDistanceFieldTexture(this, ObjectMeshCompent->GetStaticMesh(), nullptr, ShadowDistanceFieldSize, ShadowMaskCutOffset, 16.f, true);
}

