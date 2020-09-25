// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FluidSimulator.generated.h"

/**
 * This class is used to simulate fluid, and can be placed in the scene
 */

UCLASS(BlueprintType, Blueprintable)
class FLUIDSIMULATION_API AFluidSimulator : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AFluidSimulator();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

private:
	void SubmitDrawToRenderThread(float DeltaTime);

protected:
	UPROPERTY(EditDefaultsOnly)
	int32 IterationCount;

	UPROPERTY(EditDefaultsOnly)
	FIntVector FluidVolumeSize;

	UPROPERTY(EditDefaultsOnly)
	float VorticityScale;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, meta=(AllowPrivateAccess = "true"))
	class UBoxComponent* FluidProxyBox;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, meta=(AllowPrivateAccess = "true"))
	class UProceduralMeshComponent* FluidRenderingQuadMesh;

	UPROPERTY(BlueprintReadOnly)
	class UTexture2D* FluidRenderResult;

	// The Material that render fluid to the viewport, it is always a transluency material.
	UPROPERTY(EditDefaultsOnly)
	class UMaterialInterface* FluidRenderToViewMaterial;
};
