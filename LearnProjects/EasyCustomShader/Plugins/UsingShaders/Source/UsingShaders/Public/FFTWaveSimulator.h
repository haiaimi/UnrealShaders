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

	virtual void PostInitializeComponents()override;

	virtual void PostActorCreated()override;

	virtual void EndPlay(EEndPlayReason::Type EndPlayReason)override;

	virtual void Destroyed()override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	void InitWaveResource();

	// Run on cpu, don't use it
	void PrepareForFFT(float TimeSeconds);

	void EvaluateWavesFFT(float TimeSeconds);

	// Run on cpu, don't use it
	FVector2D InitSpectrum(float TimeSeconds, int32 n, int32 m);

	// Compute w(k),uesd in h(k,t), (Phillips spectrum) 
	float Dispersion(int32 n, int32 m);

	void ComputeSpectrum();

	void CreateWaveGrid();

	void CreateResources();

	// Run on cpu, don't use it
	void ComputePositionAndNormal();

	FVector2D GetWaveDimension()const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)override;

	virtual bool ShouldTickIfViewportsOnly()const override
	{
		return true;
	}
#endif

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	class UProceduralMeshComponent* WaveMesh;

	UPROPERTY(EditAnywhere, Category = WaveProperty)
	int32 HorizontalTileCount;

	UPROPERTY(EditAnywhere, Category = WaveProperty)
	int32 VerticalTileCount;

	UPROPERTY(EditAnywhere, Category = WaveProperty)
	float MeshGridLength;

	UPROPERTY(EditAnywhere, Category = WaveProperty)
	int32 TimeRate;

	UPROPERTY(EditAnywhere, Category = WaveProperty)
	int32 WaveSize;

	/**not mean the wave mesh grid length, only use in shader*/
	UPROPERTY(EditAnywhere, Category = SpectrumProperty)
	float GridLength;

	UPROPERTY(EditAnywhere, Category = SpectrumProperty)
	FVector WindSpeed;

	UPROPERTY(EditAnywhere, Category = SpectrumProperty)
	float WaveAmplitude;
	
	UPROPERTY(EditAnywhere, Category = WaveRenderResource)
	class UMaterialInterface* GridMaterial;

	UPROPERTY(EditAnywhere, Category = WaveRenderResource)
	class UTextureRenderTarget* WaveHeightMapRenderTarget;

	UPROPERTY(EditAnywhere, Category = WaveRenderResource)
	class UTextureRenderTarget* WaveNormalRenderTarget;

	UPROPERTY(EditAnywhere, Category = WaveRenderResource)
	FName WaveDisplacementScale;

	UPROPERTY(EditAnywhere, Category = Debug)
	bool DrawNormal;

	TArray<FVector2D> RandomTable;

	TArray<float> ButterflyLookupTable;

	FVertexBufferRHIRef RandomTableVB;
	FShaderResourceViewRHIRef RandomTableSRV;

	FVertexBufferRHIRef ButterflyLookupTableVB;
	FShaderResourceViewRHIRef ButterflyLookupTableSRV;

	FVertexBufferRHIRef DispersionTableVB;
	FShaderResourceViewRHIRef DispersionTableSRV;
public:
	TArray<FVector> WavePosition;
	TArray<FVector> WaveVertices;
	TArray<FVector> WaveNormals;
	TArray<FVector2D> UVs;

	TArray<float> DispersionTable;

	// Render resources
	FRWBufferStructured Spectrum;
	FRWBufferStructured SpectrumConj;

	FRWBufferStructured HeightBuffer;
	FRWBufferStructured SlopeBuffer;
	FRWBufferStructured DisplacementBuffer;

	bool bHasInit;
};
