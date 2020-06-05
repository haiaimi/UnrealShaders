// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShadowFakeryInst.generated.h"

UCLASS()
class SHADOWFAKERY_API AShadowFakeryInst : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AShadowFakeryInst();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)override;
#endif

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	virtual bool ShouldTickIfViewportsOnly()const override
	{
		return true;
	}

	UFUNCTION(BlueprintCallable)
	void GenerateShadowDistanceField();
public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	class UStaticMeshComponent* ObjectMeshCompent;
	
	UPROPERTY(EditAnywhere)
	TSubclassOf<class UShadowFakeryStaticMeshComponent> ShadowMeshCompentType;

	UPROPERTY(EditAnywhere)
	class UFoliageType* CurFoliageType;

	UPROPERTY(EditAnywhere, Category = "GenerateShadowDistanceField")
	int32 ShadowDistanceFieldSize;

	UPROPERTY(EditAnywhere)
	float ShadowMaskCutOffset;
	 
	UPROPERTY(EditAnywhere)
	FName SunYawParam;

	UPROPERTY(EditAnywhere)
	FName SunDirectionParam;

private:
	class ADirectionalLight* SceneLight;

	FVector MaskCutDir;

	TArray<UShadowFakeryStaticMeshComponent*> AllShadowStaticMesh;

	bool bHasInit;

	bool bSwitchShadowFakery;
};
