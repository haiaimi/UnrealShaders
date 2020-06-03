// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "ShadowFakeryStaticMeshComponent.generated.h"

/**
 *	Static mesh used for shadow fakery 
 */
UCLASS(Blueprintable, ClassGroup=(Rendering, Common))
class SHADOWFAKERY_API UShadowFakeryStaticMeshComponent : public UStaticMeshComponent
{
	GENERATED_BODY()

public:
	/**We need to use custom bounds to prevent it from being culled*/
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;

	void UpdateShadowState(const FVector& NewLightDir, float ShadowLength, float ShadowWidth);

private:
	FVector LightDir;
	float CurShadowLength;
	float CurShadowWidth;
};
