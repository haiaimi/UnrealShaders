// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RainRenderingCone.generated.h"

/**
* The double cone used for rain rendering
*/
UCLASS()
class ORION_API ARainRenderingCone : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ARainRenderingCone();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;
	
	void MakeConeProceduralMesh();

	void SaveRainDepthTexture();

#if WITH_EDITOR
	virtual bool ShouldTickIfViewportsOnly()const override
	{
		return true;
	}
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif

	void GenerateNoiseForRainDrop();

	void GenerateRainSplashMesh();

	void ComputeRainSplashOffset();

public:
	UPROPERTY(EditAnywhere, Category = "Cone")
	int32 ConeEdgeVertexCount = 10;

	UPROPERTY(EditAnywhere, Category = "Cone")
	float ViewOffset = 3.f;

	UPROPERTY(EditAnywhere, Category = "Cone")
	bool bGenerateRainDrop = false;

	UPROPERTY(EditAnywhere, Category = "Cone")
	float RainFadeOutAlpha = 0.1f;

	UPROPERTY(EditAnywhere, Category = "RainDrop")
	int32 RainDropNum = 100;

	UPROPERTY(EditAnywhere, Category = "RainDrop")
	int32 MaxRainDropPixelCount = 5;

	UPROPERTY(EditAnywhere, Category = "RainDrop")
	int32 RainDropMotionBlurRadius = 5;

	UPROPERTY(EditAnywhere, Category = "RainSplash")
	int32 RainSplashCount = 1000;

	UPROPERTY(EditAnywhere, Category = "RainDrop")
	float RainSplashRadius = 4000.f;

	UPROPERTY(EditAnywhere)
	class UMaterial* NoiseMaterial;
	
	UPROPERTY(EditAnywhere)
	class UTextureRenderTarget2D* NoiseRenderTarget;

	UPROPERTY(EditAnywhere, Category = "Cone")
	bool bDebugInEditorViewport = true;

	UPROPERTY(EditAnywhere, Category = "Cone")
	FString RainDepthStoragePath;

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta=(AllowPrivateAccess = "true"))
	class UProceduralMeshComponent* ConeMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	class UProceduralMeshComponent* RainSplashMeshComponent;

	UPROPERTY(Transient)
	UTexture2D* RainDepthTexture2D;

	TWeakObjectPtr<class ARainDepthCapture> CurrentRainCapture;

	FVector2D CurrentSplashOffset;

	FVector PreViewLocation;

	float PreViewYaw;

	FMatrix CaptureViewProjectionMatrix;
};
