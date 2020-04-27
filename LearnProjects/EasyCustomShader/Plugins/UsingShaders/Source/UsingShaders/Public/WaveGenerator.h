// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WaveGenerator.generated.h"

UCLASS()
class AWaveGenerator : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AWaveGenerator();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	virtual void Destroyed()override;
	
	void CreateWaveSimulators();

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)override;

	virtual bool ShouldTickIfViewportsOnly()const override
	{
		return true;
	}
#endif

public:
	UPROPERTY(EditAnywhere, Category = GeneratorConfig)
	TSubclassOf<class AFFTWaveSimulator> WaveClassType;

	UPROPERTY(EditAnywhere, Category = GeneratorConfig)
	int32 HorizontalTileCount;

	UPROPERTY(EditAnywhere, Category = GeneratorConfig)
	int32 VerticalTileCount;

private:
	UPROPERTY()
	TMap<AFFTWaveSimulator*, FVector> WaveSimulatorCache;

	UPROPERTY()
	USceneComponent* BaseScene;
};
