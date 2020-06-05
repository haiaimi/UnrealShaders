// Fill out your copyright notice in the Description page of Project Settings.


#include "ShadowFakeryStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"

extern float GSunYaw;
extern FVector GLightDirWithSize;
extern FName GSunYawName;
extern FName GSunDirectionName;

UShadowFakeryStaticMeshComponent::UShadowFakeryStaticMeshComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

FBoxSphereBounds UShadowFakeryStaticMeshComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	FBoxSphereBounds CurBounds = Super::CalcBounds(LocalToWorld);

	if (GetStaticMesh())
	{
		FVector TempLightDir, Origin, Extent;
		if (LightDir.Size() * CurShadowLength < CurShadowWidth)
		{
			TempLightDir = LightDir.GetSafeNormal2D() * CurShadowWidth;
			FVector NewDir = LocalToWorld.InverseTransformVector(TempLightDir);
			Origin = LocalToWorld.GetLocation();
			Extent = FVector(CurShadowWidth * 0.5f);
		}
		else
		{
			TempLightDir = FMath::Clamp(CurShadowLength * LightDir.Size2D(), CurShadowWidth, 10000.f) * LightDir.GetSafeNormal2D();
			FVector NewDir = LocalToWorld.InverseTransformVector(TempLightDir);
			Extent = NewDir / 2.f;
			Extent.Z = CurShadowWidth;
			Origin = LocalToWorld.GetLocation() + Extent;
		}
		//FBoxSphereBounds NewBounds(Origin, Extent, CurShadowWidth);
		//NewBounds.TransformBy(LocalToWorld);
		CurBounds.BoxExtent = FVector(10000.f);
		CurBounds.SphereRadius = CurShadowWidth;
		//UE_LOG(LogTemp, Log, TEXT("Bound Size: %4.4f"), CurBounds.BoxExtent.X);

		return CurBounds;
	}
	else
	{
		return FBoxSphereBounds(LocalToWorld.GetLocation(), FVector::ZeroVector, 0.f);
	}
}

void UShadowFakeryStaticMeshComponent::UpdateShadowState(const FVector& NewLightDir, float ShadowLength, float ShadowWidth)
{
	LightDir = NewLightDir;
	CurShadowLength = ShadowLength;
	CurShadowWidth = ShadowWidth;
	UpdateBounds();
}

void UShadowFakeryStaticMeshComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UMaterialInstanceDynamic* MaterialInst = CreateDynamicMaterialInstance(0);
	if (MaterialInst)
	{
		MaterialInst->SetScalarParameterValue(GSunYawName, GSunYaw);
		MaterialInst->SetVectorParameterValue(GSunDirectionName, FLinearColor(GLightDirWithSize));
	}
}
