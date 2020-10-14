// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "InteractiveWaterSubsystem.generated.h"

/**
 * 
 */
UCLASS()
class UInteractiveWaterSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	virtual void Deinitialize() override;

public:
	TArray<FVector> ForcePos;

	class UStaticMeshComponent* CurWaterMesh;

	class UInteractiveWaterComponent* InteractiveWaterComponent;
};
