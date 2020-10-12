// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WaveTrigger_SkeletalMesh.generated.h"

USTRUCT(BlueprintType)
struct FTriggerBoneInfo
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly)
	FName BoneName;

	UPROPERTY(EditDefaultsOnly)
	float InfluenceRadius;

	UPROPERTY(EditDefaultsOnly)
	float OffsetTolerance = 1.f;
};

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class FLUIDSIMULATION_API UWaveTrigger_SkeletalMesh : public UActorComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UWaveTrigger_SkeletalMesh();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

public:
	UPROPERTY(EditDefaultsOnly)
	TArray<FTriggerBoneInfo> TriggerBones;

	UPROPERTY()
	TArray<class USphereComponent*> BoneSpheres;

private:
	UFUNCTION()
	void OnBoneSphereBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnBoneSphereEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

private:
	TArray<FVector> PreBonesLocation;

	TArray<int32> CurOverlappedSphereIndex;

	class UInteractiveWaterSubsystem* InteractiveWaterSubsystem;

	class USkeletalMeshComponent* SkeletalMesh;
};
