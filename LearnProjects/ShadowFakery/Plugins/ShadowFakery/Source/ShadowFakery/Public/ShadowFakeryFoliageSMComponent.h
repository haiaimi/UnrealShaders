// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "FoliageInstancedStaticMeshComponent.h"
#include "ShadowFakeryFoliageSMComponent.generated.h"

/**
 * 
 */
UCLASS(Blueprintable, ClassGroup=(Rendering, Common))
class SHADOWFAKERY_API UShadowFakeryFoliageSMComponent : public UFoliageInstancedStaticMeshComponent
{
	GENERATED_BODY()
	
public:
	UShadowFakeryFoliageSMComponent();

	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)override;

	virtual void BeginPlay()override;
};
