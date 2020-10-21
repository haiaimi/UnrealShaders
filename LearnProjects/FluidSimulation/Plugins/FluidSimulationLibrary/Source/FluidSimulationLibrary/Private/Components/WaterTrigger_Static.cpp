// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/WaterTrigger_Static.h"
#include "Components/ShapeComponent.h"
#include "SubSystem/InteractiveWaterSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Common/FluidSimulationCommon.h"
#include "Components/PrimitiveComponent.h"

// Sets default values for this component's properties
UWaterTrigger_Static::UWaterTrigger_Static() : 
	WaterPhysicMaterial(nullptr),
	OffsetTolerance(1.f),
	WaterTriggerShape(nullptr),
	bInWater(false),
	TriggerRadius(0.f)
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;
}


// Called when the game starts
void UWaterTrigger_Static::BeginPlay()
{
	Super::BeginPlay();

	//WaterTriggerShape = GetOwner()->FindComponentByClass<UShapeComponent>();
	WaterTriggerShape = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent());
	if (WaterTriggerShape)
	{
		WaterTriggerShape->OnComponentBeginOverlap.AddDynamic(this, &UWaterTrigger_Static::OnShapeBeginOverlap);
		WaterTriggerShape->OnComponentEndOverlap.AddDynamic(this, &UWaterTrigger_Static::OnShapeEndOverlap);
		WaterTriggerShape->OnComponentHit.AddDynamic(this, &UWaterTrigger_Static::OnShapeBlocked);
	}

	UWorld* World = GetWorld();
	if (UGameInstance* GI = World->GetGameInstance<UGameInstance>())
	{
		InteractiveWaterSubsystem = GI->GetSubsystem<UInteractiveWaterSubsystem>();
	}

	TArray<UPrimitiveComponent*> AllPrimitives;
	WaterTriggerShape->GetOverlappingComponents(AllPrimitives);

	for (auto Iter : AllPrimitives)
	{
		auto OverlappingMesh = Cast<UStaticMeshComponent>(Iter);
		TriggerRadius = OverlappingMesh->CalcLocalBounds().SphereRadius;
		if (InteractiveWaterSubsystem->GetCurrentWaterMesh() == nullptr && OverlappingMesh && Iter->GetBodyInstance()->GetSimplePhysicalMaterial() == WaterPhysicMaterial)
		{
			InteractiveWaterSubsystem->UpdateInteractivePoint(OverlappingMesh, FApplyForceParam(OverlappingMesh->GetComponentLocation(), TriggerRadius));
		}
	}
}

// Called every frame
void UWaterTrigger_Static::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	
	if (bInWater && InteractiveWaterSubsystem->ShouldSimulateWater())
	{
		const FVector CurLocation = GetOwner()->GetActorLocation();
		if ((CurLocation - PreLocation).Size() > OffsetTolerance)
		{
			TArray<FApplyForceParam> Pos = { FApplyForceParam(CurLocation, TriggerRadius) };
			InteractiveWaterSubsystem->AddForcePos(Pos);
			PreLocation = CurLocation;
		}
	}
}

void UWaterTrigger_Static::OnShapeBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	UStaticMeshComponent* WaterMesh = Cast<UStaticMeshComponent>(OtherComp);

	TriggerRadius = WaterTriggerShape->CalcLocalBounds().SphereRadius;
	InteractiveWaterSubsystem->UpdateInteractivePoint(WaterMesh, FApplyForceParam(WaterTriggerShape->GetComponentLocation(), TriggerRadius));
	if(InteractiveWaterSubsystem->GetCurrentWaterMesh() == WaterMesh)
		bInWater = true;
}

void UWaterTrigger_Static::OnShapeEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if(InteractiveWaterSubsystem->GetCurrentWaterMesh() == OtherComp)
		bInWater = false;
}

void UWaterTrigger_Static::OnShapeBlocked(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	UStaticMeshComponent* WaterMesh = Cast<UStaticMeshComponent>(OtherComp);
	TriggerRadius = WaterTriggerShape->CalcLocalBounds().SphereRadius;
	InteractiveWaterSubsystem->UpdateInteractivePoint(WaterMesh, FApplyForceParam(WaterTriggerShape->GetComponentLocation(), TriggerRadius));
}

