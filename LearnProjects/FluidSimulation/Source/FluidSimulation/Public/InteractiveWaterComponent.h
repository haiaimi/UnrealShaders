// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InteractiveWaterComponent.generated.h"


UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class FLUIDSIMULATION_API UInteractiveWaterComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UInteractiveWaterComponent();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION()
	void OnBeginOverlap(AActor* OverlappedActor, AActor* OtherActor);

	UFUNCTION()
	void OnEndOverlap(AActor* OverlappedActor, AActor* OtherActor);

	UFUNCTION(BlueprintCallable)
	void TouchWaterSurface(FVector2D UV);

public:
	UPROPERTY(EditDefaultsOnly)
	FIntPoint FieldSize;

	UPROPERTY(EditDefaultsOnly)
	float InteractiveAreaSize;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	class UTextureRenderTarget2D* HeightFieldRT0;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	class UTextureRenderTarget2D* HeightFieldRT1;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	class UMaterialInstanceDynamic* MTInst;

private:
	FVector PreLocation;

	TSharedPtr<class FInteractiveWater> InteractiveWaterProxy;

	class UStaticMeshComponent* CurrentWaterMesh;

	class UInteractiveWaterSubsystem* InteractiveWaterSubsystem;

	TArray<FVector> InteractivePosition;
	TArray<FVector4> IntearctivePoint;
};
