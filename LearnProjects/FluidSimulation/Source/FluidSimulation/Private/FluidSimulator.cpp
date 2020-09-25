// Fill out your copyright notice in the Description page of Project Settings.
#include "FluidSimulator.h"
#include "Components/BoxComponent.h"
#include "ProceduralMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/LocalPlayer.h"
#include "SceneView.h"
#include "Engine/Engine.h"
#include "FluidSimulation3D.h"
#include "Kismet/GameplayStatics.h"

// Sets default values
AFluidSimulator::AFluidSimulator():
	IterationCount(20),
	FluidVolumeSize(128),
	VorticityScale(0.2f)
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	FluidProxyBox = CreateDefaultSubobject<UBoxComponent>(TEXT("FluidProxyBox"));
	FluidRenderingQuadMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("FluidRenderingQuadMesh"));
	RootComponent = FluidProxyBox;
	FluidRenderingQuadMesh->SetupAttachment(RootComponent);

	FluidRenderResult = nullptr;
}

// Called when the game starts or when spawned
void AFluidSimulator::BeginPlay()
{
	Super::BeginPlay();
	
	FluidRenderResult = NewObject<UTexture2D>();
	if (FluidRenderToViewMaterial)
	{
		FluidRenderingQuadMesh->SetMaterial(0, FluidRenderToViewMaterial);
	}
}

// Called every frame
void AFluidSimulator::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	SubmitDrawToRenderThread(DeltaTime);

	FluidRenderingQuadMesh->Bounds = FluidProxyBox->Bounds;

	APlayerController* Player = UGameplayStatics::GetPlayerController(this, 0);

	ULocalPlayer* const LP = Player ? Player->GetLocalPlayer() : nullptr;
	if (LP && LP->ViewportClient)
	{
		FSceneViewProjectionData ProjectionData;
		if (LP->GetProjectionData(LP->ViewportClient->Viewport, eSSP_FULL, ProjectionData))
		{
			const FMatrix InvViewProj = ProjectionData.ComputeViewProjectionMatrix().InverseFast();
			const FMatrix ViewMatrix = FTranslationMatrix(-ProjectionData.ViewOrigin) * ProjectionData.ViewRotationMatrix;
			const FVector ViewSpaceVolumePos = ViewMatrix.TransformPosition(GetActorLocation());

			static const FVector NDCCornerPos[4] = { FVector(-1.f, 1.f, 0.f),
													FVector(1.f, 1.f, 0.f),
													FVector(-1.f, -1.f, 0.f),
													FVector(1.f, -1.f, 0.f)};
			static const TArray<FVector2D> UVs = { FVector2D(0.f, 0.f),
												  FVector2D(1.f, 0.f), 
												  FVector2D(0.f, 1.f),
												  FVector2D(1.f, 1.f)};
			static const TArray<int32> Indices = {0, 2, 1,
												  1, 2, 3};

			TArray<FVector> WorldSpaceCorners;
			WorldSpaceCorners.Reserve(4);
			for (auto& Pos : NDCCornerPos)
			{
				FVector Result = (InvViewProj * GetActorTransform().ToMatrixNoScale()).TransformPosition(Pos * ViewSpaceVolumePos.Z);
				WorldSpaceCorners.Add(Result - GetActorLocation());
			}
			TArray<FVector> EmptyVec;
			TArray<FVector2D> EmptyUV;
			TArray<FColor> EmptyColor;
			TArray<FProcMeshTangent> EmptyTangent;
			FluidRenderingQuadMesh->CreateMeshSection(0, WorldSpaceCorners, Indices, EmptyVec, UVs, EmptyColor, EmptyTangent, false);
			FProcMeshSection* MeshSection = FluidRenderingQuadMesh->GetProcMeshSection(0);
			MeshSection->SectionLocalBox = FluidProxyBox->CalcBounds(FTransform::Identity).GetBox();
		}
	}
}

void AFluidSimulator::SubmitDrawToRenderThread(float DeltaTime)
{
	FTextureResource* TextureResource = FluidRenderResult ? FluidRenderResult->Resource : nullptr;
	FFluidResourceParams ResourceParams;
	ResourceParams.TextureResource = TextureResource;
	UWorld* World = GetWorld();
	ERHIFeatureLevel::Type FeatureLevel = World->Scene->GetFeatureLevel();
	FScene* Scene = World->Scene->GetRenderScene();

	int32 IterCount = IterationCount;
	FIntVector FluidVolSize = FluidVolumeSize;
	float VortiScale = VorticityScale;
	GEngine->PreRenderDelegate.RemoveAll(this);
	GEngine->PreRenderDelegate.AddWeakLambda(this, [ResourceParams, FeatureLevel, IterCount, DeltaTime, FluidVolSize, VortiScale, Scene]() {
		FRHICommandListImmediate& RHICmdList = GetImmediateCommandList_ForRenderCommand();
		UpdateFluid3D(RHICmdList, ResourceParams, IterCount, DeltaTime, VortiScale, FluidVolSize, Scene, FeatureLevel);
	});
}

