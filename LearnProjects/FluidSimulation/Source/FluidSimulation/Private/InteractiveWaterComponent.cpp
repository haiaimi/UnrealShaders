// Fill out your copyright notice in the Description page of Project Settings.


#include "InteractiveWaterComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "InteractiveWater.h"
#include "Kismet/KismetSystemLibrary.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "InteractiveWaterSubsystem.h"

#define SURFACE_WATER SurfaceType10

// Sets default values for this component's properties
UInteractiveWaterComponent::UInteractiveWaterComponent() : 
	FieldSize(512),
	InteractiveAreaSize(5000.f),
	HeightFieldRT0(nullptr),
	HeightFieldRT1(nullptr),
	CurrentWaterMesh(nullptr)
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;
}


// Called when the game starts
void UInteractiveWaterComponent::BeginPlay()
{
	Super::BeginPlay();

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

	InteractiveWaterProxy->SetResource(HeightFieldRT0, HeightFieldRT1);

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
	
	const FVector CurLocation = GetOwner()->GetActorLocation();
	const FVector DeltaLocation = CurLocation - PreLocation;
	FVector2D DeltaUV = FVector2D(DeltaLocation) / InteractiveAreaSize;
	if (FMath::IsNearlyZero(DeltaUV.Size()) && DeltaLocation.Size() > 0.f)
	{
		//DeltaUV = FVector2D(0.0001f, 0.0001f);
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

	InteractiveWaterSubsystem->ForcePos.Reset();

	UTextureRenderTarget* CurHeightMap = InteractiveWaterProxy->GetCurrentTarget_GameThread(UVToHeightField);

	if (CurrentWaterMesh)
	{
		MTInst = CurrentWaterMesh->CreateDynamicMaterialInstance(0, CurrentWaterMesh->GetMaterial(0));
		//MTInst = CurrentWaterMesh->CreateAndSetMaterialInstanceDynamic(0);
		MTInst->SetVectorParameterValue(TEXT("RoleLocation"), FLinearColor(CurLocation));
		MTInst->SetVectorParameterValue(TEXT("RoleUV"), FLinearColor(FVector(InteractiveWaterProxy->ForcePos, 0.f)));
		MTInst->SetTextureParameterValue(TEXT("WaterHeightMap"), CurHeightMap);
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

			if (PhysMaterial->SurfaceType == SURFACE_WATER)
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

