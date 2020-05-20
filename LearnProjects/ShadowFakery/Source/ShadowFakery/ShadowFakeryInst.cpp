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

	ShadowMeshCompent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Shadow"));
	SceneLight = nullptr;

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
		FRotator Rotation = SceneLight->GetActorRotation();

		float SunYaw = -Rotation.Pitch;
		if (FMath::IsNearlyZero(FMath::Abs(SunYaw) - 90.f))
			SunYaw = FMath::Sign(SunYaw) * 89.000f;

		Rotation.Normalize();
		Rotation.Pitch = -SunYaw;
		const FVector LightDir = Rotation.Vector();
		MaterialInst->SetScalarParameterValue(SunYawParam, SunYaw);
		MaterialInst->SetVectorParameterValue(SunDirectionParam, FLinearColor(LightDir) * -FMath::Loge(1.f - FMath::Cos(FMath::DegreesToRadians(SunYaw))));
	}
}

