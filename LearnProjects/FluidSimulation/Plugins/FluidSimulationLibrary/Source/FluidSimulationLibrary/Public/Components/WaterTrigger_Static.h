// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WaterTrigger_Static.generated.h"


UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class UWaterTrigger_Static : public UActorComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UWaterTrigger_Static();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditDefaultsOnly)
	class UPhysicalMaterial* WaterPhysicMaterial;

	UPROPERTY(EditDefaultsOnly)
	float OffsetTolerance;

private:
	UPROPERTY()
	class UPrimitiveComponent* WaterTriggerShape;

	class UInteractiveWaterSubsystem* InteractiveWaterSubsystem;

	bool bInWater;

	FVector PreLocation;

	float TriggerRadius;

protected:
	UFUNCTION()
	void OnShapeBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnShapeEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	UFUNCTION()
	void OnShapeBlocked(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);
};
