// Fill out your copyright notice in the Description page of Project Settings.


#include "ShadowFakeryFoliageSMComponent.h"
#include "ShadowFakeryStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ConstructorHelpers.h"
#include "Engine/StaticMesh.h"

extern float GSunYaw;
extern FVector GLightDirWithSize;
extern FName GSunYawName;
extern FName GSunDirectionName;



UShadowFakeryFoliageSMComponent::UShadowFakeryFoliageSMComponent()
{
}

void UShadowFakeryFoliageSMComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	//if(!CVarUseShadowFakery.GetValueOnGameThread())
	//{ 
	//	CastShadow = true;
	//	MarkRenderStateDirty();
	//	bCastDynamicShadow = true;
	//}
	//else
	//{
	//	CastShadow = false;
	//	MarkRenderStateDirty();
	//	bCastDynamicShadow = false;
	//}
}

void UShadowFakeryFoliageSMComponent::BeginPlay()
{
	Super::BeginPlay();
}
