// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Common/FluidSimulationCommon.h"
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

	void SetCurrentWaterMesh(class UStaticMeshComponent* WaterMesh);

	class UStaticMeshComponent* GetCurrentWaterMesh();

	void SetInteractiveWaterComponent(class UInteractiveWaterComponent* WaterComponent);

	void AddForcePos(const TArray<FApplyForceParam>& NewPos);

	const TArray<FApplyForceParam>& GetForcePos() const { return ForcePos; }

	void ResetPos();

	void UpdateInteractivePoint(class UStaticMeshComponent* WaterMesh, FApplyForceParam ForcePos);

	bool ShouldSimulateWater();

	bool CheckWaterMesh(class UStaticMeshComponent* WaterMesh);

private:
	TArray<struct FApplyForceParam> ForcePos;

	class UStaticMeshComponent* CurWaterMesh;

	class UInteractiveWaterComponent* InteractiveWaterComponent;
};
