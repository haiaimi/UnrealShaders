// Fill out your copyright notice in the Description page of Project Settings.


#include "FFTWaveSimulator.h"
#include "../Plugins/Runtime/ProceduralMeshComponent/Source/ProceduralMeshComponent/Public/ProceduralMeshComponent.h"

// Sets default values
AFFTWaveSimulator::AFFTWaveSimulator():
	WaveMesh(nullptr),
	SizeX(64),
	SizeY(64)
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

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

