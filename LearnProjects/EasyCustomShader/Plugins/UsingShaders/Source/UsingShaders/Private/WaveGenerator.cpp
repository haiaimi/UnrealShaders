// Fill out your copyright notice in the Description page of Project Settings.


#include "WaveGenerator.h"
#include "FFTWaveSimulator.h"
#include "Engine/World.h"
#include "Runtime/Engine/Classes/Components/SceneComponent.h"

// Sets default values
AWaveGenerator::AWaveGenerator():
	HorizontalTileCount(1),
	VerticalTileCount(1)
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	BaseScene = CreateDefaultSubobject<USceneComponent>(TEXT("BaseScene"));
	RootComponent = BaseScene;
}

// Called when the game starts or when spawned
void AWaveGenerator::BeginPlay()
{
	Super::BeginPlay();
	
	CreateWaveSimulators();
}

// Called every frame
void AWaveGenerator::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

void AWaveGenerator::Destroyed()
{
	Super::Destroyed();

	for (auto Iter : WaveSimulatorCache)
	{
		if (IsValid(Iter))
			Iter->Destroy();
	}
	WaveSimulatorCache.Reset();
}

void AWaveGenerator::CreateWaveSimulators()
{
	for (auto Iter : WaveSimulatorCache)
	{
		if (IsValid(Iter))
			Iter->Destroy();
	}
	WaveSimulatorCache.Reset();

	if (WaveClassType)
	{
		if (AFFTWaveSimulator* DefaultWave = Cast<AFFTWaveSimulator>(WaveClassType->GetDefaultObject()))
		{
			const FVector2D WaveDim = DefaultWave->GetWaveDimension();
			FVector AllWaveExtent(WaveDim.X * VerticalTileCount / 2.f, WaveDim.Y * HorizontalTileCount / 2.f, 0.f);

			for (int32 i = 0; i < VerticalTileCount; ++i)
			{
				for (int32 j = 0; j < HorizontalTileCount; ++j)
				{
					const FVector RelSpawnPos = FVector(WaveDim.X * i + WaveDim.X / 2.f, WaveDim.Y * j + WaveDim.Y / 2.f, 0.f) - AllWaveExtent;
					AFFTWaveSimulator* Simulator = GetWorld()->SpawnActor<AFFTWaveSimulator>(WaveClassType, GetActorLocation() + RelSpawnPos, GetActorRotation());
					if (Simulator)
					{
						WaveSimulatorCache.Add(Simulator);
						Simulator->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
					}
				}
			}
		}
	}
}

static FName Name_WaveClassType = GET_MEMBER_NAME_CHECKED(AWaveGenerator, WaveClassType);
static FName Name_HorizontalTileCount_Gen = GET_MEMBER_NAME_CHECKED(AWaveGenerator, HorizontalTileCount);
static FName Name_VerticalTileCount_Gen = GET_MEMBER_NAME_CHECKED(AWaveGenerator, VerticalTileCount);

#if WITH_EDITOR
void AWaveGenerator::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	UProperty* MemberPropertyThatChanged = PropertyChangedEvent.MemberProperty;
	const FName MemberPropertyName = MemberPropertyThatChanged != NULL ? MemberPropertyThatChanged->GetFName() : NAME_None;

	bool bWaveProperyChanged = MemberPropertyName == Name_WaveClassType ||
							   MemberPropertyName == Name_HorizontalTileCount_Gen ||
							   MemberPropertyName == Name_VerticalTileCount_Gen;
	if (bWaveProperyChanged)
	{
		CreateWaveSimulators();
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

