// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/WaveTrigger_SkeletalMesh.h"
#include "Engine/GameInstance.h"
#include "SubSystem/InteractiveWaterSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Common/FluidSimulationCommon.h"

// Sets default values for this component's properties
UWaveTrigger_SkeletalMesh::UWaveTrigger_SkeletalMesh() : 
	InteractiveWaterSubsystem(nullptr),
	SkeletalMesh(nullptr)
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;
}


// Called when the game starts
void UWaveTrigger_SkeletalMesh::BeginPlay()
{
	Super::BeginPlay();

	PreBonesLocation.SetNum(TriggerBones.Num());
	BoneSpheres.SetNum(TriggerBones.Num());

	UWorld* World = GetWorld();
	if (UGameInstance* GI = World->GetGameInstance<UGameInstance>())
	{
		InteractiveWaterSubsystem = GI->GetSubsystem<UInteractiveWaterSubsystem>();
	}

	SkeletalMesh = GetOwner()->FindComponentByClass<USkeletalMeshComponent>();

	for(int32 i = 0; i < TriggerBones.Num(); ++i)
	{ 
		BoneSpheres[i] = NewObject<USphereComponent>(this); //CreateDefaultSubobject<USphereComponent>(TriggerBones[i].BoneName);
		BoneSpheres[i]->RegisterComponent();
		BoneSpheres[i]->SetSphereRadius(TriggerBones[i].InfluenceRadius);
		BoneSpheres[i]->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		BoneSpheres[i]->SetCollisionObjectType(ECC_WorldDynamic);
		BoneSpheres[i]->SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Overlap);
		BoneSpheres[i]->AttachToComponent(SkeletalMesh, FAttachmentTransformRules::KeepRelativeTransform, TriggerBones[i].BoneName);
		BoneSpheres[i]->SetRelativeLocation(FVector::ZeroVector);
		BoneSpheres[i]->bMultiBodyOverlap = true;

		BoneSpheres[i]->OnComponentBeginOverlap.AddDynamic(this, &UWaveTrigger_SkeletalMesh::OnBoneSphereBeginOverlap);
		BoneSpheres[i]->OnComponentEndOverlap.AddDynamic(this, &UWaveTrigger_SkeletalMesh::OnBoneSphereEndOverlap);

		TArray<UPrimitiveComponent*> AllPrimitives;
		BoneSpheres[i]->GetOverlappingComponents(AllPrimitives);

		for (auto Iter : AllPrimitives)
		{
			auto OverlappingMesh = Cast<UStaticMeshComponent>(Iter);
			if (InteractiveWaterSubsystem->GetCurrentWaterMesh() == nullptr && OverlappingMesh && Iter->GetBodyInstance()->GetSimplePhysicalMaterial() == WaterPhysicMaterial)
			{
				InteractiveWaterSubsystem->UpdateInteractivePoint(OverlappingMesh, FApplyForceParam(OverlappingMesh->GetComponentLocation(), TriggerBones[i].InfluenceRadius));
			}

			if (InteractiveWaterSubsystem->GetCurrentWaterMesh() == Iter)
			{
				CurOverlappedSphereIndex.AddUnique(i);
				PreBonesLocation[i] = SkeletalMesh->GetBoneLocation(TriggerBones[i].BoneName);
			}
		}
	}
}


// Called every frame
void UWaveTrigger_SkeletalMesh::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (SkeletalMesh/* && InteractiveWaterSubsystem->ShouldSimulateWater()*/)
	{
		UStaticMeshComponent* WaterMesh = InteractiveWaterSubsystem->GetCurrentWaterMesh();

		TArray<FApplyForceParam> ForcePos;
		for (int32 i = 0; i < CurOverlappedSphereIndex.Num(); ++i)
		{
			int32 Index = CurOverlappedSphereIndex[i];
			FVector CurLoc = BoneSpheres[Index]->GetComponentLocation();
			if ((CurLoc - PreBonesLocation[Index]).Size() > TriggerBones[Index].OffsetTolerance)
			{
				ForcePos.Add(FApplyForceParam(CurLoc + (CurLoc - PreBonesLocation[Index]), TriggerBones[i].InfluenceRadius));
			}
		}
		InteractiveWaterSubsystem->AddForcePos(ForcePos);

		//UE_LOG(LogTemp, Log, TEXT("Force num: %d"), ForcePos.Num());

		for (int32 i = 0; i < TriggerBones.Num(); ++i)
		{
			FVector BoneLoc = SkeletalMesh->GetBoneLocation(TriggerBones[i].BoneName);
			PreBonesLocation[i] = BoneLoc;
		}
	}
}

void UWaveTrigger_SkeletalMesh::OnBoneSphereBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	int32 CurIndex = BoneSpheres.Find(Cast<USphereComponent>(OverlappedComponent));
	UStaticMeshComponent* OvelappingMesh = Cast<UStaticMeshComponent>(OtherComp);
	if(OvelappingMesh && OvelappingMesh->GetBodyInstance()->GetSimplePhysicalMaterial() == WaterPhysicMaterial && CurIndex != INDEX_NONE)
		InteractiveWaterSubsystem->UpdateInteractivePoint(OvelappingMesh, FApplyForceParam(GetOwner()->GetActorLocation(), TriggerBones[CurIndex].InfluenceRadius));
	else
		return;

	UStaticMeshComponent* WaterMesh = InteractiveWaterSubsystem->GetCurrentWaterMesh();
	if (WaterMesh == OtherComp)
	{
		if (CurOverlappedSphereIndex.Find(CurIndex) == INDEX_NONE)
		{
			//if((PreBonesLocation[i] - OverlappedComponent->GetComponentLocation()).Size() < TriggerBones[CurIndex].OffsetTolerance)
			CurOverlappedSphereIndex.Add(CurIndex);
			PreBonesLocation[CurIndex] = SkeletalMesh->GetBoneLocation(TriggerBones[CurIndex].BoneName);
		}
	}
}

void UWaveTrigger_SkeletalMesh::OnBoneSphereEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	UStaticMeshComponent* WaterMesh = InteractiveWaterSubsystem->GetCurrentWaterMesh();
	if (WaterMesh == OtherComp)
	{
		int32 CurIndex = BoneSpheres.Find(Cast<USphereComponent>(OverlappedComponent));
		if (CurIndex != INDEX_NONE && CurOverlappedSphereIndex.Find(CurIndex) != INDEX_NONE)
		{
			CurOverlappedSphereIndex.Remove(CurIndex);
		}
	}
}

