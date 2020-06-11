// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "ShadowFakeryStaticMeshComponent.generated.h"

/**
 *	Static mesh used for shadow fakery 
 */
UCLASS(Blueprintable, ClassGroup=(Rendering, Common))
class SHADOWFAKERY_API UShadowFakeryStaticMeshComponent : public UInstancedStaticMeshComponent
{
	GENERATED_BODY()

public:
	UShadowFakeryStaticMeshComponent();

	/**We need to use custom bounds to prevent it from being culled*/
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;

	void UpdateShadowState(const FVector& NewLightDir, float ShadowLength, float ShadowWidth);

	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)override;

private:
	FVector LightDir;
	float CurShadowLength;
	float CurShadowWidth;
};
