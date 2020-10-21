// Fill out your copyright notice in the Description page of Project Settings.


#include "SubSystem/InteractiveWaterSubsystem.h"
#include "Components/InteractiveWaterComponent.h"
#include "Components/StaticMeshComponent.h"

void UInteractiveWaterSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{

}

void UInteractiveWaterSubsystem::Deinitialize()
{

}

void UInteractiveWaterSubsystem::SetCurrentWaterMesh(class UStaticMeshComponent* WaterMesh)
{
	CurWaterMesh = WaterMesh;
	if(InteractiveWaterComponent)
		InteractiveWaterComponent->SetCurrentWaterMesh(WaterMesh);
}

class UStaticMeshComponent* UInteractiveWaterSubsystem::GetCurrentWaterMesh()
{
	if(InteractiveWaterComponent)
		return InteractiveWaterComponent->GetCurrentWaterMesh();
	return nullptr;
}

void UInteractiveWaterSubsystem::SetInteractiveWaterComponent(class UInteractiveWaterComponent* WaterComponent)
{
	InteractiveWaterComponent = WaterComponent;
}

void UInteractiveWaterSubsystem::AddForcePos(const TArray<FApplyForceParam>& NewPos)
{
	ForcePos.Append(NewPos);
}

void UInteractiveWaterSubsystem::ResetPos()
{
	ForcePos.Reset();
}

void UInteractiveWaterSubsystem::UpdateInteractivePoint(class UStaticMeshComponent* WaterMesh, FApplyForceParam InForcePos)
{
	if (!InteractiveWaterComponent && !CheckWaterMesh(WaterMesh)) return;
	auto Mesh = GetCurrentWaterMesh();
	if(Mesh != WaterMesh && InteractiveWaterComponent->CanUpdateWaterMesh()) return;

	if (InteractiveWaterComponent->CheckPosInSimulateArea(InForcePos.ForcePos))
	{
		ForcePos.Add(InForcePos);
		if(Mesh == nullptr)
			InteractiveWaterComponent->SetCurrentWaterMesh(WaterMesh);
	}
}

bool UInteractiveWaterSubsystem::ShouldSimulateWater()
{
	if(InteractiveWaterComponent)
		return InteractiveWaterComponent->ShouldSimulateWater();
	return false;
}

bool UInteractiveWaterSubsystem::CheckWaterMesh(class UStaticMeshComponent* WaterMesh)
{
	if (InteractiveWaterComponent && WaterMesh)
	{
		if(WaterMesh->GetBodyInstance()->GetSimplePhysicalMaterial() == InteractiveWaterComponent->WaterPhysicMaterial)
			return true;
	}
	return false;
}
