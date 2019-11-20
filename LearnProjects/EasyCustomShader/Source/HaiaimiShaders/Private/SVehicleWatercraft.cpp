// Fill out your copyright notice in the Description page of Project Settings.


#include "SVehicleWatercraft.h"
#include "PhysicsPublic.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Public/SBuoyancyComponent.h"

// Sets default values
ASVehicleWatercraft::ASVehicleWatercraft()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	WatercraftMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WatercraftMesh"));
	WatercraftMesh->SetSimulatePhysics(true);
}

// Called when the game starts or when spawned
void ASVehicleWatercraft::BeginPlay()
{
	Super::BeginPlay();

	TArray<USBuoyancyComponent*> AllBuoyancy;
	GetComponents<USBuoyancyComponent, FDefaultAllocator>(AllBuoyancy);

	for (auto Iter : AllBuoyancy)
	{
		Iter->SetUpdatedComponent(WatercraftMesh);
	}
}

float ASVehicleWatercraft::GetWaterSurfaceHeight(FVector DetectPos /*= FVector::ZeroVector*/)
{
	return 20.f;
}

void ASVehicleWatercraft::UpdatePhysics(float Force, FBodyInstance* BodyInstance)
{

}

void ASVehicleWatercraft::SetSteer_Implementation(float AxisValue)
{
	if (WatercraftMesh)
	{
		WatercraftMesh->GetBodyInstance()->AddImpulseAtPosition(-GetActorRotation().Quaternion().GetRightVector()*AxisValue*30.f, WatercraftMesh->GetSocketLocation("EngineSocket"));
	}
}

bool ASVehicleWatercraft::SetSteer_Validate(float AxisValue)
{
	return true;
}

void ASVehicleWatercraft::SetThrottle_Implementation(float AxisValue)
{
	if (WatercraftMesh)
	{
		WatercraftMesh->GetBodyInstance()->AddImpulseAtPosition(GetActorRotation().Quaternion().GetForwardVector()*AxisValue*100.f, WatercraftMesh->GetSocketLocation("EngineSocket"));
	}
}

bool ASVehicleWatercraft::SetThrottle_Validate(float AxisValue)
{
	return true;
}

// Called every frame
void ASVehicleWatercraft::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}
