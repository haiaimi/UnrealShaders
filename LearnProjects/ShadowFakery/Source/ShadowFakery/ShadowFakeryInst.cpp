// Fill out your copyright notice in the Description page of Project Settings.


#include "ShadowFakeryInst.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Engine/DirectionalLight.h"
#include "Materials/MaterialInstanceDynamic.h"

// Sets default values
AShadowFakeryInst::AShadowFakeryInst()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	ObjectMeshCompent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Object"));
	ShadowMeshCompent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Shadow"));
	RootComponent = ObjectMeshCompent;
	SceneLight = nullptr;
	ShadowMaskCutOffset = 90.f;
	const float OffsetRadian = FMath::DegreesToRadians(ShadowMaskCutOffset);
	MaskCutDir = FVector(FMath::Cos(OffsetRadian), FMath::Sin(OffsetRadian), 0.f);
	SunYawParam = TEXT("SunYaw");
	SunDirectionParam = TEXT("SunForwardDirection");
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

	UMaterialInstanceDynamic* MaterialInst = ShadowMeshCompent->CreateDynamicMaterialInstance(0);

	if (!SceneLight)
	{
		for (TActorIterator<ADirectionalLight> Iter(GetWorld()); Iter; ++Iter)
		{
			SceneLight = *Iter;
			break;
		}
	}

	if (SceneLight && MaterialInst)
	{
		const FVector CutDirWS = GetActorTransform().TransformVectorNoScale(MaskCutDir);
		const FVector UpDirWS = GetActorUpVector();
		const FVector RightDir = FVector::CrossProduct(CutDirWS, UpDirWS);
		
		FRotator Rotation = SceneLight->GetActorRotation();
		const FVector LightDir = Rotation.Vector();
		float SunYaw = (90.f - FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(-LightDir, UpDirWS)))) * FMath::Sign(FVector::DotProduct(-LightDir, CutDirWS));

		if (FMath::IsNearlyZero(FMath::Abs(SunYaw) - 90.f))
			SunYaw = FMath::Sign(SunYaw) * 89.000f;

		UE_LOG(LogTemp, Log, TEXT("Current Sun Yaw: %4.4f"), SunYaw);
		
		MaterialInst->SetScalarParameterValue(SunYawParam, SunYaw);
		//MaterialInst->SetVectorParameterValue(SunDirectionParam, FLinearColor(LightDir.GetSafeNormal2D()) * FMath::Abs(FMath::Tan(FMath::DegreesToRadians(90.f - FMath::Abs(SunYaw)))));
		MaterialInst->SetVectorParameterValue(SunDirectionParam, FLinearColor(LightDir.GetSafeNormal2D()) * (1.f - FMath::Abs(SunYaw) / 90.f) * 5.f);
	}
}

