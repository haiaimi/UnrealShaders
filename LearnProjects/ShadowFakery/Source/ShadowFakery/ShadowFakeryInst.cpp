// Fill out your copyright notice in the Description page of Project Settings.


#include "ShadowFakeryInst.h"
#include "Components/StaticMeshComponent.h"

// Sets default values
AShadowFakeryInst::AShadowFakeryInst()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	ShadowMeshCompent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Shadow"));
}

// Called when the game starts or when spawned
void AShadowFakeryInst::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void AShadowFakeryInst::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UMaterialInstanceDynamic* MaterialInst = ShadowMeshCompent->CreateDynamicMaterialInstance(0);
}

