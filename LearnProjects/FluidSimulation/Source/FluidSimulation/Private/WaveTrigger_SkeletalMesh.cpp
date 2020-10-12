// Fill out your copyright notice in the Description page of Project Settings.


#include "WaveTrigger_SkeletalMesh.h"
#include "Engine/GameInstance.h"
#include "InteractiveWaterSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"

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

		BoneSpheres[i]->OnComponentBeginOverlap.AddDynamic(this, &UWaveTrigger_SkeletalMesh::OnBoneSphereBeginOverlap);
		BoneSpheres[i]->OnComponentEndOverlap.AddDynamic(this, &UWaveTrigger_SkeletalMesh::OnBoneSphereEndOverlap);
	}
}


// Called every frame
void UWaveTrigger_SkeletalMesh::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (SkeletalMesh)
	{
		UStaticMeshComponent* WaterMesh = InteractiveWaterSubsystem->CurWaterMesh;

		TArray<FVector> ForcePos;
		for (int32 i = 0; i < CurOverlappedSphereIndex.Num(); ++i)
		{
			int32 Index = CurOverlappedSphereIndex[i];
			FVector CurLoc = BoneSpheres[Index]->GetComponentLocation();
			if ((CurLoc - PreBonesLocation[Index]).Size() > TriggerBones[Index].OffsetTolerance)
			{
				ForcePos.Add(CurLoc);
			}
		}
		InteractiveWaterSubsystem->ForcePos.Append(ForcePos);

		for (int32 i = 0; i < TriggerBones.Num(); ++i)
		{
			FVector BoneLoc = SkeletalMesh->GetBoneLocation(TriggerBones[i].BoneName);
			PreBonesLocation[i] = BoneLoc;
		}
	}
}

void UWaveTrigger_SkeletalMesh::OnBoneSphereBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	UStaticMeshComponent* WaterMesh = InteractiveWaterSubsystem->CurWaterMesh;
	if (WaterMesh == OtherComp)
	{
		int32 CurIndex = BoneSpheres.Find(Cast<USphereComponent>(OverlappedComponent));
		if (CurIndex != INDEX_NONE && CurOverlappedSphereIndex.Find(CurIndex) == INDEX_NONE)
		{
			//if((PreBonesLocation[i] - OverlappedComponent->GetComponentLocation()).Size() < TriggerBones[CurIndex].OffsetTolerance)
			CurOverlappedSphereIndex.Add(CurIndex);
		}
	}
}

void UWaveTrigger_SkeletalMesh::OnBoneSphereEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	UStaticMeshComponent* WaterMesh = InteractiveWaterSubsystem->CurWaterMesh;
	if (WaterMesh == OtherComp)
	{
		int32 CurIndex = BoneSpheres.Find(Cast<USphereComponent>(OverlappedComponent));
		if (CurIndex != INDEX_NONE && CurOverlappedSphereIndex.Find(CurIndex) != INDEX_NONE)
		{
			CurOverlappedSphereIndex.Remove(CurIndex);
		}
	}
}

