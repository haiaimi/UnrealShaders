// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "SVehicleWatercraft.generated.h"

UCLASS()
class HAIAIMISHADERS_API ASVehicleWatercraft : public APawn
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ASVehicleWatercraft();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	float GetWaterSurfaceHeight(FVector DetectPos = FVector::ZeroVector);

	void UpdatePhysics(float, FBodyInstance*);

	UFUNCTION(BlueprintCallable, Server, Unreliable, WithValidation)
	void SetThrottle(float AxisValue);

	UFUNCTION(BlueprintCallable, Server, Unreliable, WithValidation)
	void SetSteer(float AxisValue);

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

private:
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, meta = (AllowPrivateAccess = "true"))
	class UStaticMeshComponent* WatercraftMesh;
};
