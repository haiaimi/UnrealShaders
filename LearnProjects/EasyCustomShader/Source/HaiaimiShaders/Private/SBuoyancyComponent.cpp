// Fill out your copyright notice in the Description page of Project Settings.


#include "SBuoyancyComponent.h"
#include "Components/SphereComponent.h"
#include "Kismet/KismetSystemLibrary.h"

// Sets default values for this component's properties
USBuoyancyComponent::USBuoyancyComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

	SphereRadius = 10.f;

	CustomPhysics.BindUObject(this, &USBuoyancyComponent::UpdatePhysics);
}


// Called when the game starts
void USBuoyancyComponent::BeginPlay()
{
	Super::BeginPlay();
}


void USBuoyancyComponent::UpdatePhysics(float DeltaTime, FBodyInstance* BodyInstance)
{
	//UKismetSystemLibrary::PrintString(this, TEXT("Update Physics"));
	if (GetOwnerRole() < ENetRole::ROLE_Authority)return;
	const FVector ForcePoint = GetComponentLocation();
	const float Height = GetWaterSurfaceHeight(ForcePoint);
	const float Radius = GetUnscaledSphereRadius();

	float BuoyancyScale = FMath::Clamp((ForcePoint.Z - Height) / Radius, -1.f, 1.f);
	float CurBuoyancy = 10.f - 10.f*BuoyancyScale;

	BodyInstance->AddImpulseAtPosition(FVector::UpVector*CurBuoyancy, ForcePoint);
}

float USBuoyancyComponent::GetWaterSurfaceHeight(FVector DetectPos /*= FVector::ZeroVector*/)
{
	return 20.f;
}

void USBuoyancyComponent::SetUpdatedComponent(class UPrimitiveComponent* PrimComp)
{
	UpdatedComponent = PrimComp;
}

// Called every frame
void USBuoyancyComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (UpdatedComponent)
		UpdatedComponent->GetBodyInstance()->AddCustomPhysics(CustomPhysics);
}

