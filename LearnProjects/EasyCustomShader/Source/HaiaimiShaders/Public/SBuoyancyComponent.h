// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/SphereComponent.h"
#include "SBuoyancyComponent.generated.h"


UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class HAIAIMISHADERS_API USBuoyancyComponent : public USphereComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	USBuoyancyComponent();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

	void UpdatePhysics(float DeltaTime, FBodyInstance* BodyInstance);

	float GetWaterSurfaceHeight(FVector DetectPos = FVector::ZeroVector);

public:	
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
		
	void SetUpdatedComponent(class UPrimitiveComponent* PrimComp);

private:
	FCalculateCustomPhysics CustomPhysics;

	UPROPERTY()
	class UPrimitiveComponent* UpdatedComponent;
};
