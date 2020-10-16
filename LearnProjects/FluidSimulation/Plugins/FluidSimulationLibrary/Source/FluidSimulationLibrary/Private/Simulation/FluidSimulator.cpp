// Fill out your copyright notice in the Description page of Project Settings.
#include "Simulation/FluidSimulator.h"
#include "Components/BoxComponent.h"
#include "ProceduralMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/LocalPlayer.h"
#include "SceneView.h"
#include "Engine/Engine.h"
#include "Simulation/FluidSimulation3D.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/TextureRenderTarget2D.h"
#include "DrawDebugHelpers.h"
#include "Materials/MaterialInstanceDynamic.h"

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

	//
	//FluidRenderingQuadMesh->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
	//FluidRenderingQuadMesh->SetRelativeLocation(FVector::ZeroVector);
	FluidRenderingQuadMesh->SetComponentToWorld(GetActorTransform());

	APlayerController* Player = UGameplayStatics::GetPlayerController(this, 0);
	ULocalPlayer* const LP = Player ? Player->GetLocalPlayer() : nullptr;
	LP->ViewportClient->Viewport->ViewportResizedEvent.AddUObject(this, &AFluidSimulator::UpdateFluidRenderTarget);

	const FIntPoint CurRTSize = LP->ViewportClient->Viewport->GetRenderTargetTextureSizeXY();

	FluidRenderResult = NewObject<UTexture2D>();
	FluidRenderTarget = NewObject<UTextureRenderTarget2D>();
	FluidRenderTarget->SizeX = CurRTSize.X * 0.5f;
	FluidRenderTarget->SizeY = CurRTSize.Y * 0.5f;
	FluidRenderTarget->AddressX = TextureAddress::TA_Clamp;
	FluidRenderTarget->AddressY = TextureAddress::TA_Clamp;
	FluidRenderTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA32f;
	FluidRenderTarget->ClearColor = FLinearColor::Transparent;
	FluidRenderTarget->UpdateResource();

	if (FluidRenderToViewMaterial)
	{
		FluidRenderingQuadMesh->SetMaterial(0, FluidRenderToViewMaterial);
		UMaterialInstanceDynamic* MTInst = FluidRenderingQuadMesh->CreateAndSetMaterialInstanceDynamic(0);
		MTInst->SetTextureParameterValue(TEXT("RayMarchFluid"), FluidRenderTarget);
	}

	CreateFluidProxy();
	
	if (VolumeFluidProxy.IsValid())
	{
		VolumeFluidProxy->RayMarchRTSize = CurRTSize;
	}
}

void AFluidSimulator::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	VolumeFluidProxy.Reset();
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
		FBoxSphereBounds MeshBounds = FluidRenderingQuadMesh->CalcLocalBounds();
		
		if (LP->GetProjectionData(LP->ViewportClient->Viewport, eSSP_FULL, ProjectionData))
		{
			const FVector VolumePos = GetActorLocation() + (ProjectionData.ViewOrigin - GetActorLocation()).GetSafeNormal() * MeshBounds.SphereRadius;
			const FMatrix ViewMatrix = FTranslationMatrix(-ProjectionData.ViewOrigin) * ProjectionData.ViewRotationMatrix;
			FVector ViewSpaceVolumePos = ViewMatrix.TransformPosition(VolumePos);

			static const FVector NDCCornerPos[4] = { FVector(-1.f, 1.f, 1.f),
													FVector(1.f, 1.f, 1.f),
													FVector(-1.f, -1.f, 1.f),
													FVector(1.f, -1.f, 1.f)};
			static const TArray<FVector2D> UVs = { FVector2D(0.f, 0.f),
												  FVector2D(1.f, 0.f), 
												  FVector2D(0.f, 1.f),
												  FVector2D(1.f, 1.f)};
			static const TArray<int32> Indices = {0, 2, 1,
												  1, 2, 3};
			
			//const FBox CurrentBox = MeshBounds.GetBox().TransformBy(GetActorTransform());
			if((ProjectionData.ViewOrigin - GetActorLocation()).Size() <= MeshBounds.SphereRadius + GEngine->NearClipPlane)
				ViewSpaceVolumePos.Z = GEngine->NearClipPlane + 0.5f;

			//UKismetSystemLibrary::PrintString(this, FString::SanitizeFloat((ProjectionData.ViewOrigin - GetActorLocation()).Size()));
			TArray<FVector> WorldSpaceCorners;
			TArray<FVector> WorldSpaceCorners_Test;
			WorldSpaceCorners.Reserve(4);
			WorldSpaceCorners_Test.Reserve(4);
			for (auto& Pos : NDCCornerPos)
			{
				//UGameplayStatics::DeprojectScreenToWorld()
				FVector Result = (GetActorTransform().ToMatrixNoScale() * ProjectionData.ComputeViewProjectionMatrix()).InverseFast().TransformFVector4(FVector4(Pos * FVector(ViewSpaceVolumePos.Z, ViewSpaceVolumePos.Z, GEngine->NearClipPlane), ViewSpaceVolumePos.Z));
				WorldSpaceCorners_Test.Add(Result);
				//Result = GetActorTransform().InverseTransformPosition(Result);
				//FVector Result_Test = (ProjectionData.ComputeViewProjectionMatrix()).InverseTransformPosition(Pos * ViewSpaceVolumePos.Z);
				WorldSpaceCorners.Add(Result);
				
			}
			//DrawDebugMesh(GetWorld(), WorldSpaceCorners, Indices, FColor::Cyan);

			TArray<FVector> EmptyVec;
			TArray<FVector2D> EmptyUV;
			TArray<FColor> EmptyColor;
			TArray<FProcMeshTangent> EmptyTangent;
			FluidRenderingQuadMesh->CreateMeshSection(0, WorldSpaceCorners, Indices, EmptyVec, UVs, EmptyColor, EmptyTangent, false);
			FProcMeshSection* MeshSection = FluidRenderingQuadMesh->GetProcMeshSection(0);
			MeshSection->SectionLocalBox = FluidProxyBox->CalcBounds(FTransform::Identity).GetBox();
			FluidRenderingQuadMesh->SetProcMeshSection(0, *MeshSection);
		}
	}
}

extern TGlobalResource<FFluidSmiulationManager> GFluidSmiulationManager;

void AFluidSimulator::CreateFluidProxy()
{
	if(VolumeFluidProxy.IsValid())
		return;

	VolumeFluidProxy = MakeShared<FVolumeFluidProxy, ESPMode::ThreadSafe>();

	ENQUEUE_RENDER_COMMAND(FEnqueResource)([this](FRHICommandListImmediate& RHICmdList)
	{
		if (VolumeFluidProxy.IsValid())
			GFluidSmiulationManager.AddFluidProxy(VolumeFluidProxy);
	});
}

void AFluidSimulator::SubmitDrawToRenderThread(float DeltaTime)
{
	if(!VolumeFluidProxy.IsValid())return;
	FTextureResource* TextureResource = FluidRenderResult ? FluidRenderResult->Resource : nullptr;
	FTextureRenderTargetResource* RTResource = FluidRenderTarget ? FluidRenderTarget->GameThread_GetRenderTargetResource() : nullptr;
	UWorld* World = GetWorld();
	ERHIFeatureLevel::Type FeatureLevel = World->Scene->GetFeatureLevel();

	const FQuat BoxQuat = GetActorRotation().Quaternion();
	const FVector BoxExtent = FluidProxyBox->GetScaledBoxExtent();
	const FVector BoxOrigin = GetActorLocation() - BoxQuat.GetRightVector() * BoxExtent.Y - BoxQuat.GetUpVector() * BoxExtent.Z - BoxQuat.GetForwardVector() * BoxExtent.X;
	// Update the fluid render resource
	VolumeFluidProxy->FluidVolumeSize = FluidVolumeSize;
	VolumeFluidProxy->FluidVolumeTransform = FTransform(GetActorRotation(), BoxOrigin, BoxExtent * 2.f);
	VolumeFluidProxy->IterationCount = IterationCount;
	VolumeFluidProxy->VorticityScale = VorticityScale;
	VolumeFluidProxy->TimeStep = DeltaTime;
	VolumeFluidProxy->TextureRenderTargetResource = RTResource;
	VolumeFluidProxy->TextureResource = TextureResource;
	VolumeFluidProxy->FeatureLevel = FeatureLevel;
	//UWorld* World = GetWorld();
	//ERHIFeatureLevel::Type FeatureLevel = World->Scene->GetFeatureLevel();
	//FScene* Scene = World->Scene->GetRenderScene();

	//int32 IterCount = IterationCount;
	//FIntVector FluidVolSize = FluidVolumeSize;
	//float VortiScale = VorticityScale;
	//GEngine->PreRenderDelegate.RemoveAll(this);
	//GEngine->PreRenderDelegate.AddWeakLambda(this, [ResourceParams, FeatureLevel, IterCount, DeltaTime, FluidVolSize, VortiScale, Scene]() {
	//	FRHICommandListImmediate& RHICmdList = GetImmediateCommandList_ForRenderCommand();
	//	UpdateFluid3D(RHICmdList, ResourceParams, IterCount, DeltaTime, VortiScale, FluidVolSize, Scene, FeatureLevel);
	//});
}

void AFluidSimulator::UpdateFluidRenderTarget(FViewport* Viewport, uint32 Index)
{
	if (FluidRenderTarget && Viewport)
	{
		FIntPoint NewRTSize = Viewport->GetRenderTargetTextureSizeXY();
		FluidRenderTarget->ResizeTarget(NewRTSize.X / 2, NewRTSize.Y / 2);
		if (VolumeFluidProxy.IsValid())
		{
			VolumeFluidProxy->RayMarchRTSize = NewRTSize;
		}
	}
}

