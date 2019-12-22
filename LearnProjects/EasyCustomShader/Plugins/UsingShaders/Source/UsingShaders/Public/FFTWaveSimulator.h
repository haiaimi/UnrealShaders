// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RHIResources.h"
#include "FFTWaveSimulator.generated.h"

UCLASS()
class AFFTWaveSimulator : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AFFTWaveSimulator();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	void InitWaveResource();

	void PrepareForFFT(float TimeSeconds);

	void EvaluateWavesFFT(float TimeSeconds);

	FVector2D InitSpectrum(float TimeSeconds, int32 n, int32 m);

	float Dispersion(int32 n, int32 m);

	void ComputeSpectrum();

	void CreateWaveGrid();

	void CreateResources();

	void ComputePositionAndNormal();

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)override;
#endif

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	class UProceduralMeshComponent* WaveMesh;

	UPROPERTY(EditAnywhere)
	int32 WaveSize;

	UPROPERTY(EditAnywhere)
	float GridLength;

	UPROPERTY(EditAnywhere)
	FVector WindSpeed;

	UPROPERTY(EditAnywhere)
	float WaveAmplitude;

	UPROPERTY(EditAnywhere)
	class UMaterialInterface* GridMaterial;

	TArray<float> ButterflyLookupTable;

	FVertexBufferRHIRef ButterflyLookupTableVB;
	FShaderResourceViewRHIRef ButterflyLookupTableSRV;

	FVertexBufferRHIRef DispersionTableVB;
	FShaderResourceViewRHIRef DispersionTableSRV;
private:
	TArray<FVector> WavePosition;
	TArray<FVector> WaveVertices;
	TArray<FVector> WaveNormals;
	TArray<FVector2D> UVs;

	TArray<float> DispersionTable;

	// Render resources
	FTexture2DRHIRef Spectrum;
	FTexture2DRHIRef SpectrumConj;

	FTexture2DRHIRef HeightBuffer;
	FTexture2DRHIRef SlopeBuffer;
	FTexture2DRHIRef DisplacementBuffer;
};
