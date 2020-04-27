// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RHIResources.h"
#include "RHIUtilities.h"
#include "FFTWaveSimulator.generated.h"

///** Encapsulates a GPU read/write texture 2D with its UAV and SRV. */
struct FTextureRWBuffer2D
{
	FTexture2DRHIRef Buffer;
	FUnorderedAccessViewRHIRef UAV;
	FShaderResourceViewRHIRef SRV;
	uint32 NumBytes;

	FTextureRWBuffer2D()
		: NumBytes(0)
	{}

	~FTextureRWBuffer2D()
	{
		Release();
	}

	// @param AdditionalUsage passed down to RHICreateVertexBuffer(), get combined with "BUF_UnorderedAccess | BUF_ShaderResource" e.g. BUF_Static
	const static uint32 DefaultTextureInitFlag = TexCreate_ShaderResource | TexCreate_UAV;
	void Initialize(const uint32 BytesPerElement, const uint32 SizeX, const uint32 SizeY, const EPixelFormat Format, uint32 Flags = DefaultTextureInitFlag)
	{
		check(GMaxRHIFeatureLevel == ERHIFeatureLevel::SM5
			|| IsVulkanPlatform(GMaxRHIShaderPlatform)
			|| IsMetalPlatform(GMaxRHIShaderPlatform)
			|| (GMaxRHIFeatureLevel == ERHIFeatureLevel::ES3_1 && GSupportsResourceView)
		);

		NumBytes = SizeX * SizeY * BytesPerElement;

		FRHIResourceCreateInfo CreateInfo;
		Buffer = RHICreateTexture2D(
			SizeX, SizeY, Format, //PF_R32_FLOAT,
			/*NumMips=*/ 1,
			1,
			Flags,
			/*BulkData=*/ CreateInfo);


		UAV = RHICreateUnorderedAccessView(Buffer, 0);
		SRV = RHICreateShaderResourceView(Buffer, 0);
	}

	void AcquireTransientResource()
	{
		RHIAcquireTransientResource(Buffer);
	}
	void DiscardTransientResource()
	{
		RHIDiscardTransientResource(Buffer);
	}

	void Release()
	{
		int32 BufferRefCount = Buffer ? Buffer->GetRefCount() : -1;

		if (BufferRefCount == 1)
		{
			DiscardTransientResource();
		}

		NumBytes = 0;
		Buffer.SafeRelease();
		UAV.SafeRelease();
		SRV.SafeRelease();
	}
};

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
	class UMeshComponent* WaveMesh;

	UPROPERTY(EditAnywhere, Category = WaveRenderResource)
	bool bUseStaticMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = WaveRenderResource, meta=(editcondition = "bUseStaticMesh"))
	class UStaticMesh* WaveStaticMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = WaveRenderResource, meta = (editcondition = "bUseStaticMesh"))
	float WaveStaticMeshGridSize;

	UPROPERTY(EditAnywhere, Category = WaveProperty)
	int32 VerticalTileCount;

	UPROPERTY(EditAnywhere, Category = WaveProperty)
	int32 HorizontalTileCount;

	UPROPERTY(EditAnywhere, Category = WaveProperty)
	float MeshGridLength;

	UPROPERTY(EditAnywhere, Category = WaveProperty)
	int32 TimeRate;

	UPROPERTY(EditAnywhere, Category = WaveProperty)
	int32 WaveSize;

	/**not mean the wave mesh grid length, only use in shader*/
	UPROPERTY(EditAnywhere, Category = SpectrumProperty)
	float PatchLength;

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

	UPROPERTY(EditAnywhere, Category = WaveMaterialParams)
	FName WaveDisplacementScale;

	UPROPERTY(EditAnywhere, Category = WaveMaterialParams)
	FName WaveTexelOffset;

	UPROPERTY(EditAnywhere, Category = WaveMaterialParams)
	FName WaveGradientZ;

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
	FRWBuffer Spectrum;
	FRWBuffer SpectrumConj;

	FTextureRWBuffer2D HeightBuffer;
	FTextureRWBuffer2D SlopeBuffer;
	FTextureRWBuffer2D DisplacementBuffer;

	bool bHasInit;
};
