// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/InteractiveWaterComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Simulation/InteractiveWater.h"
#include "Kismet/KismetSystemLibrary.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "SubSystem/InteractiveWaterSubsystem.h"
#include "Engine/Engine.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

#define SURFACE_WATER SurfaceType10

// Sets default values for this component's properties
UInteractiveWaterComponent::UInteractiveWaterComponent() : 
	InterationTimesPerSecond(30.f),
	FieldSize(512),
	InteractiveAreaSize(5000.f),
	StopSimuationThreshold(10.f),
	HeightFieldRT0(nullptr),
	HeightFieldRT1(nullptr),
	CurrentWaterMesh(nullptr),
	StopTimeAccumlator(0.f)
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;
}


// Called when the game starts
void UInteractiveWaterComponent::BeginPlay()
{
	Super::BeginPlay();

	if(UKismetSystemLibrary::IsDedicatedServer(this)) return;

	PreLocation = GetOwner()->GetActorLocation();

	InteractiveWaterProxy = TSharedPtr<FInteractiveWater>(new FInteractiveWater);

	HeightFieldRT0 = NewObject<UTextureRenderTarget2D>();
	HeightFieldRT1 = NewObject<UTextureRenderTarget2D>();

	UTextureRenderTarget2D* RTs[2] = {HeightFieldRT0, HeightFieldRT1};

	for (auto RT : RTs)
	{
		RT->SizeX = FieldSize.X;
		RT->SizeY = FieldSize.Y;
		RT->AddressX = TextureAddress::TA_Clamp;
		RT->AddressY = TextureAddress::TA_Clamp;
		RT->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RG16f;
		RT->ClearColor = FLinearColor::Transparent;
		RT->UpdateResource();
	}

	NormalMap = NewObject<UTextureRenderTarget2D>();
	NormalMap->SizeX = FieldSize.X;
	NormalMap->SizeY = FieldSize.Y;
	NormalMap->AddressX = TextureAddress::TA_Clamp;
	NormalMap->AddressY = TextureAddress::TA_Clamp;
	NormalMap->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA16f;
	NormalMap->ClearColor = FLinearColor::Transparent;
	NormalMap->UpdateResource();
	
	InteractiveWaterProxy->SetResource(HeightFieldRT0, HeightFieldRT1, NormalMap, 1.f / InterationTimesPerSecond, GetWorld()->Scene->GetFeatureLevel());

	GetOwner()->OnActorBeginOverlap.AddDynamic(this, &UInteractiveWaterComponent::OnBeginOverlap);
	GetOwner()->OnActorEndOverlap.AddDynamic(this, &UInteractiveWaterComponent::OnEndOverlap);

	if (!InteractiveWaterSubsystem)
	{
		if (UGameInstance* GI = GetWorld()->GetGameInstance<UGameInstance>())
		{
			InteractiveWaterSubsystem = GI->GetSubsystem<UInteractiveWaterSubsystem>();
			InteractiveWaterSubsystem->InteractiveWaterComponent = this;
		}
	}
}

// Called every frame
void UInteractiveWaterComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	
	if(UKismetSystemLibrary::IsDedicatedServer(this)) return;

	//UE_LOG(LogTemp, Log, TEXT("---------TickComponent---------"));

	if(InteractiveWaterSubsystem->ForcePos.Num() > 0)
		StopTimeAccumlator = 0.f;
	else
		StopTimeAccumlator += DeltaTime;
	if ((StopTimeAccumlator > StopSimuationThreshold || !InteractiveWaterProxy->ShouldSimulate(DeltaTime)) && DeltaTime <= 1.f / InterationTimesPerSecond)
		return;

	const FVector CurLocation = GetOwner()->GetActorLocation();
	const FVector DeltaLocation = CurLocation - PreLocation;
	FVector2D DeltaUV = FVector2D(DeltaLocation) / InteractiveAreaSize;
	if (FMath::IsNearlyZero(DeltaUV.Size()) && DeltaLocation.Size() > 0.f)
	{
		DeltaUV = FVector2D(0.0001f, 0.0001f);
	}
	FVector2D UVToHeightField = FVector2D(DeltaUV.Y, -DeltaUV.X);

	InteractiveWaterProxy->MoveDir = UVToHeightField;
	InteractiveWaterProxy->DeltaTime = DeltaTime;

	InteractiveWaterProxy->UpdateForceParams(DeltaTime, UVToHeightField, CurLocation, InteractiveAreaSize, InteractiveWaterSubsystem->ForcePos);

	PreLocation = CurLocation;

	ENQUEUE_RENDER_COMMAND(FComputeInteractiveWater)([this](FRHICommandListImmediate& RHICmdList)
	{
		InteractiveWaterProxy->UpdateWater();
	});
	
	/*if (GEngine->PreRenderDelegate.IsBoundToObject(this))
	{
		GEngine->PreRenderDelegate.RemoveAll(this);
	}*/

	//GEngine->PreRenderDelegate.AddWeakLambda(this, [this](){InteractiveWaterProxy->ComputeNormal_RenderThread();});

	InteractiveWaterSubsystem->ForcePos.Reset();

	UTextureRenderTarget* CurHeightMap = InteractiveWaterProxy->GetCurrentTarget_GameThread(UVToHeightField);

	if (!CurrentWaterMesh) CurrentWaterMesh = InteractiveWaterSubsystem->CurWaterMesh;
	if (CurrentWaterMesh)
	{
		MTInst = CurrentWaterMesh->CreateDynamicMaterialInstance(0, CurrentWaterMesh->GetMaterial(0));
		//MTInst = CurrentWaterMesh->CreateAndSetMaterialInstanceDynamic(0);
		MTInst->SetScalarParameterValue(TEXT("WaveSize"), InteractiveAreaSize);
		MTInst->SetVectorParameterValue(TEXT("RoleLocation"), FLinearColor(CurLocation));
		MTInst->SetVectorParameterValue(TEXT("RoleUV"), FLinearColor(FVector(InteractiveWaterProxy->ForcePos, 0.f)));
		MTInst->SetTextureParameterValue(TEXT("NormalMap"), NormalMap);
	}
}

void UInteractiveWaterComponent::OnBeginOverlap(AActor* OverlappedActor, AActor* OtherActor)
{
	if (OtherActor)
	{
		UStaticMeshComponent* StaticMesh = OtherActor->FindComponentByClass<UStaticMeshComponent>();
		if (StaticMesh)
		{
			UPhysicalMaterial* PhysMaterial = StaticMesh->GetBodyInstance()->GetSimplePhysicalMaterial();

			if (!WaterPhysicMaterial)
			{
				UE_LOG(LogTemp, Warning, TEXT("------YOU NEED TO SET WATER SURFACE------"));
				return;
			}

			if (PhysMaterial->SurfaceType == WaterPhysicMaterial->SurfaceType)
			{
				CurrentWaterMesh = StaticMesh;
				if(InteractiveWaterSubsystem)
					InteractiveWaterSubsystem->CurWaterMesh = CurrentWaterMesh;
			}
		}
		UKismetSystemLibrary::PrintString(this, OtherActor->GetName());
	}
}

void UInteractiveWaterComponent::OnEndOverlap(AActor* OverlappedActor, AActor* OtherActor)
{
	if (OtherActor)
	{
		UStaticMeshComponent* StaticMesh = OtherActor->FindComponentByClass<UStaticMeshComponent>();
		if (StaticMesh && StaticMesh == CurrentWaterMesh)
		{
			//CurrentWaterMesh = nullptr;
		}
	}
}

void UInteractiveWaterComponent::TouchWaterSurface(FVector2D UV)
{
	TArray<FVector> Pos = { GetOwner()->GetActorLocation() + FVector::OneVector * 1000.f};
	InteractiveWaterProxy->UpdateForceParams(GetWorld()->DeltaTimeSeconds, FVector2D::ZeroVector, GetOwner()->GetActorLocation(), InteractiveAreaSize, Pos);
}

