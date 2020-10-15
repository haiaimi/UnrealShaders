// Fill out your copyright notice in the Description page of Project Settings.


#include "Common/FluidSimulationCommon.h"
#include "Engine/GameInstance.h"
#include "SubSystem/InteractiveWaterSubsystem.h"
#include "UObject/Object.h"
#include "Engine/World.h"

void UFluidSimulationCommon::AddForcePos(const UObject* WorldContextObject, class UStaticMeshComponent* CurWaterMesh, FApplyForceParam NewForce)
{
	if (UGameInstance* GI = WorldContextObject->GetWorld()->GetGameInstance<UGameInstance>())
	{
		UInteractiveWaterSubsystem* WaterSubSys = GI->GetSubsystem<UInteractiveWaterSubsystem>();
		WaterSubSys->UpdateInteractivePoint(CurWaterMesh, NewForce);
	}
}
