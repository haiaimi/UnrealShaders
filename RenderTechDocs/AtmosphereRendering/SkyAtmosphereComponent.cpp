// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/SkyAtmosphereComponent.h"

#include "Atmosphere/AtmosphericFogComponent.h"
#include "Components/ArrowComponent.h"
#include "Components/BillboardComponent.h"
#include "Engine/MapBuildDataRegistry.h"
#include "Internationalization/Text.h"
#include "Logging/MessageLog.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/MapErrors.h"
#include "Misc/UObjectToken.h"
#include "Rendering/SkyAtmosphereCommonData.h"
#include "UObject/UObjectIterator.h"
#include "UObject/ConstructorHelpers.h"

#if WITH_EDITOR
#include "ObjectEditorUtils.h"
#include "ObjectTools.h"
#include "FileHelpers.h"
#include "AssetRegistryModule.h"
#endif
//@StarLight code - START Precomputed Multi Scattering on mobile, edit by wanghai
#include "../Private/SkyAtmosphereRendering.h"
#include "Engine/VolumeTexture.h"

//@StarLight code - END Precomputed Multi Scattering on mobile, edit by wanghai

#define LOCTEXT_NAMESPACE "SkyAtmosphereComponent"



/*=============================================================================
	USkyAtmosphereComponent implementation.
=============================================================================*/

USkyAtmosphereComponent::USkyAtmosphereComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, SkyAtmosphereSceneProxy(nullptr)
{
	// All distance here are in kilometer and scattering/absorptions coefficient in 1/kilometers.
	const float EarthBottomRadius = 6360.0f;
	const float EarthTopRadius = 6420.0f;
	const float EarthRayleighScaleHeight = 8.0f;
	const float EarthMieScaleHeight = 1.2f;
	
	// Default: Earth like atmosphere
	TransformMode = ESkyAtmosphereTransformMode::PlanetTopAtAbsoluteWorldOrigin;
	BottomRadius = EarthBottomRadius;
	AtmosphereHeight = EarthTopRadius - EarthBottomRadius;
	GroundAlbedo = FColor(170, 170, 170); // 170 => 0.4f linear

	// Float to a u8 rgb + float length can lose some precision but it is better UI wise.
	const FLinearColor RayleightScatteringRaw = FLinearColor(0.005802f, 0.013558f, 0.033100f);
	RayleighScattering = RayleightScatteringRaw * (1.0f / RayleightScatteringRaw.B);
	RayleighScatteringScale = RayleightScatteringRaw.B;
	RayleighExponentialDistribution = EarthRayleighScaleHeight;

	MieScattering = FColor(FColor::White);
	MieScatteringScale = 0.003996f;
	MieAbsorption = FColor(FColor::White);
	MieAbsorptionScale = 0.000444f;
	MieAnisotropy = 0.8f;
	MieExponentialDistribution = EarthMieScaleHeight;

	// Absorption tent distribution representing ozone distribution in Earth atmosphere.
	const FLinearColor OtherAbsorptionRaw = FLinearColor(0.000650f, 0.001881f, 0.000085f);
	OtherAbsorptionScale = OtherAbsorptionRaw.G;
	OtherAbsorption = OtherAbsorptionRaw * (1.0f / OtherAbsorptionRaw.G);
	OtherTentDistribution.TipAltitude = 25.0f;
	OtherTentDistribution.TipValue    =  1.0f;
	OtherTentDistribution.Width       = 15.0f;

	SkyLuminanceFactor = FLinearColor(FLinearColor::White);
	MultiScatteringFactor = 1.0f;
	AerialPespectiveViewDistanceScale = 1.0f;
	HeightFogContribution = 1.0f;

	TransmittanceMinLightElevationAngle = -90.0f;

	memset(OverrideAtmosphericLight, 0, sizeof(OverrideAtmosphericLight));

	ValidateStaticLightingGUIDs();
}

USkyAtmosphereComponent::~USkyAtmosphereComponent()
{
}

static bool SkyAtmosphereComponentStaticLightingBuilt(const USkyAtmosphereComponent* Component)
{
	AActor* Owner = Component->GetOwner();
	UMapBuildDataRegistry* Registry = nullptr;
	if (Owner)
	{
		ULevel* OwnerLevel = Owner->GetLevel();
		if (OwnerLevel && OwnerLevel->OwningWorld)
		{
			ULevel* ActiveLightingScenario = OwnerLevel->OwningWorld->GetActiveLightingScenario();
			if (ActiveLightingScenario && ActiveLightingScenario->MapBuildData)
			{
				Registry = ActiveLightingScenario->MapBuildData;
			}
			else if (OwnerLevel->MapBuildData)
			{
				Registry = OwnerLevel->MapBuildData;
			}
		}
	}

	const FSkyAtmosphereMapBuildData* SkyAtmosphereFogBuildData = Registry ? Registry->GetSkyAtmosphereBuildData(Component->GetStaticLightingBuiltGuid()) : nullptr;
	UWorld* World = Component->GetWorld();
	if (World)
	{
		class FSceneInterface* Scene = Component->GetWorld()->Scene;

		// Only require building if there is a Sky or Sun light requiring lighting builds, i.e. non movable.
		const bool StaticLightingDependsOnAtmosphere = Scene->HasSkyLightRequiringLightingBuild() || Scene->HasAtmosphereLightRequiringLightingBuild();
		// Built data is available or static lighting does not depend any sun/sky components.
		return (SkyAtmosphereFogBuildData != nullptr && StaticLightingDependsOnAtmosphere) || !StaticLightingDependsOnAtmosphere;
	}

	return true;	// The component has not been spawned in any world yet so let's mark it as built for now.
}

void USkyAtmosphereComponent::SendRenderTransformCommand()
{
	if (SkyAtmosphereSceneProxy)
	{
		FTransform ComponentTransform = GetComponentTransform();
		uint8 TrsfMode = uint8(TransformMode);
		FSkyAtmosphereSceneProxy* SceneProxy = SkyAtmosphereSceneProxy;
		ENQUEUE_RENDER_COMMAND(FUpdateSkyAtmosphereSceneProxyTransformCommand)(
			[SceneProxy, ComponentTransform, TrsfMode](FRHICommandList& RHICmdList)
		{
			SceneProxy->UpdateTransform(ComponentTransform, TrsfMode);
		});
	}
}

void USkyAtmosphereComponent::CreateRenderState_Concurrent(FRegisterComponentContext* Context)
{
	Super::CreateRenderState_Concurrent(Context);
	//@StarLight code - START Precomputed Multi Scattering on mobile, edit by wanghai
	FString MapName = GWorld->GetMapName();
	if (MapName.Contains(TEXT("UEDPIE_")))
	{
		FString Tmp;
		MapName.Split(TEXT("_"), &Tmp, &MapName);
		MapName.Split(TEXT("_"), &Tmp, &MapName);
	}

	PrecomputedScatteringLut = LoadObject<UVolumeTexture>(nullptr, *(TEXT("/Game/SkyAtmosphereLuts/Tex_ScatteringTexture_") + MapName));
	PrecomputedTranmisttanceLut = LoadObject<UTexture2D>(nullptr, *(TEXT("/Game/SkyAtmosphereLuts/Tex_Tranmittance_") + MapName));
	PrecomputedIrradianceLut = LoadObject<UTexture2D>(nullptr, *(TEXT("/Game/SkyAtmosphereLuts/Tex_Irradiance_") + MapName));
	
	// Some platform may create texture async, so we need to flush to ensure the texture is usable
	FlushRenderingCommands();
	//@StarLight code - END Precomputed Multi Scattering on mobile, edit by wanghai

	// If one day we need to look up lightmass built data, lookup it up here using the guid from the correct MapBuildData.

	bool bHidden = false;
#if WITH_EDITORONLY_DATA
	bHidden = GetOwner() ? GetOwner()->bHiddenEdLevel : false;
#endif // WITH_EDITORONLY_DATA
	if (!ShouldComponentAddToScene())
	{
		bHidden = true;
	}

	if (GetVisibleFlag() && !bHidden &&
		ShouldComponentAddToScene() && ShouldRender() && IsRegistered() && (GetOuter() == NULL || !GetOuter()->HasAnyFlags(RF_ClassDefaultObject)))
	{
		// Create the scene proxy.
		SkyAtmosphereSceneProxy = new FSkyAtmosphereSceneProxy(this);
		GetWorld()->Scene->AddSkyAtmosphere(SkyAtmosphereSceneProxy, SkyAtmosphereComponentStaticLightingBuilt(this));
	}

	//@StarLight code - START Precomputed Multi Scattering on mobile, edit by wanghai
#if WITH_EDITOR
	if (bShouldUpdatePrecomputedAtmpsphereLuts)
	{
		FScene* Scene = GetWorld()->Scene->GetRenderScene();

		bShouldUpdatePrecomputedAtmpsphereLuts = false;
	}
#endif
	//@StarLight code - END Precomputed Multi Scattering on mobile, edit by wanghai
}

void USkyAtmosphereComponent::SendRenderTransform_Concurrent()
{
	Super::SendRenderTransform_Concurrent();
	SendRenderTransformCommand();
}

void USkyAtmosphereComponent::DestroyRenderState_Concurrent()
{
	Super::DestroyRenderState_Concurrent();

	if (SkyAtmosphereSceneProxy)
	{
		GetWorld()->Scene->RemoveSkyAtmosphere(SkyAtmosphereSceneProxy);

		FSkyAtmosphereSceneProxy* SceneProxy = SkyAtmosphereSceneProxy;
		ENQUEUE_RENDER_COMMAND(FDestroySkyAtmosphereSceneProxyCommand)(
			[SceneProxy](FRHICommandList& RHICmdList)
		{
			delete SceneProxy;
		});

		SkyAtmosphereSceneProxy = nullptr;
	}
}

void USkyAtmosphereComponent::ValidateStaticLightingGUIDs()
{
	// Validate light guids.
	if (!bStaticLightingBuiltGUID.IsValid())
	{
		UpdateStaticLightingGUIDs();
	}
}

void USkyAtmosphereComponent::UpdateStaticLightingGUIDs()
{
	bStaticLightingBuiltGUID = FGuid::NewGuid();
}

#if WITH_EDITOR

void USkyAtmosphereComponent::CheckForErrors()
{
	AActor* Owner = GetOwner();
	if (Owner && GetVisibleFlag())
	{
		UWorld* ThisWorld = Owner->GetWorld();
		bool bMultipleFound = false;
		bool bLegacyAtmosphericFogFound = false;

		if (ThisWorld)
		{
			for (TObjectIterator<USkyAtmosphereComponent> ComponentIt; ComponentIt; ++ComponentIt)
			{
				USkyAtmosphereComponent* Component = *ComponentIt;

				if (Component != this
					&& !Component->IsPendingKill()
					&& Component->GetVisibleFlag()
					&& Component->GetOwner()
					&& ThisWorld->ContainsActor(Component->GetOwner())
					&& !Component->GetOwner()->IsPendingKill())
				{
					bMultipleFound = true;
					break;
				}
			}
			for (TObjectIterator<UAtmosphericFogComponent> ComponentIt; ComponentIt; ++ComponentIt)
			{
				UAtmosphericFogComponent* Component = *ComponentIt;

				if (!Component->IsPendingKill()
					&& Component->GetVisibleFlag()
					&& Component->GetOwner()
					&& ThisWorld->ContainsActor(Component->GetOwner())
					&& !Component->GetOwner()->IsPendingKill())
				{
					bLegacyAtmosphericFogFound = true;
					break;
				}
			}
		}

		if (bMultipleFound)
		{
			FMessageLog("MapCheck").Error()
				->AddToken(FUObjectToken::Create(Owner))
				->AddToken(FTextToken::Create(LOCTEXT("MapCheck_Message_MultipleSkyAtmosphere", "Multiple sky atmosphere are active, only one can be enabled per world.")))
				->AddToken(FMapErrorToken::Create(FMapErrors::MultipleSkyAtmospheres));
		}
		if (bLegacyAtmosphericFogFound)
		{
			FMessageLog("MapCheck").Error()
				->AddToken(FUObjectToken::Create(Owner))
				->AddToken(FTextToken::Create(LOCTEXT("MapCheck_Message_MultipleSkyAtmosphereType", "A SkyAtmosphere and a legacy AtmosphericFog components are both active, we recommend to have only one enabled per world.")))
				->AddToken(FMapErrorToken::Create(FMapErrors::MultipleSkyAtmosphereTypes));
		}
	}
}

void USkyAtmosphereComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// If any properties have been changed in the atmosphere category, it means the sky look will change and lighting needs to be rebuild.
	const FName CategoryName = FObjectEditorUtils::GetCategoryFName(PropertyChangedEvent.Property);
	if (CategoryName == FName(TEXT("Planet")) ||
		CategoryName == FName(TEXT("Atmosphere")) ||
		CategoryName == FName(TEXT("Atmosphere - Rayleigh")) ||
		CategoryName == FName(TEXT("Atmosphere - Mie")) ||
		CategoryName == FName(TEXT("Atmosphere - Absorption")) ||
		CategoryName == FName(TEXT("Art direction")))
	{
		if (SkyAtmosphereComponentStaticLightingBuilt(this))
		{
			// If we have changed an atmosphere property and the lighyting has already been built, we need to ask for a rebuild by updating the static lighting GUIDs.
			UpdateStaticLightingGUIDs();
		}

		if (PropertyChangedEvent.MemberProperty && PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(USkyAtmosphereComponent, TransformMode))
		{
			SendRenderTransformCommand();
		}
		//@StarLight code - START Precomputed Multi Scattering on mobile, edit by wanghai
		if (bUsePrecomputedAtmpsphereLuts)
		{
			bShouldUpdatePrecomputedAtmpsphereLuts = true;
		}
		//@StarLight code - END Precomputed Multi Scattering on mobile, edit by wanghai
	}
}

#endif // WITH_EDITOR

void USkyAtmosphereComponent::PostInterpChange(FProperty* PropertyThatChanged)
{
	Super::PostInterpChange(PropertyThatChanged);
	MarkRenderStateDirty();
}

void USkyAtmosphereComponent::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar << bStaticLightingBuiltGUID;
}

void USkyAtmosphereComponent::OverrideAtmosphereLightDirection(int32 AtmosphereLightIndex, const FVector& LightDirection)
{
	check(AtmosphereLightIndex >= 0 && AtmosphereLightIndex < NUM_ATMOSPHERE_LIGHTS);
	if (AreDynamicDataChangesAllowed() && SkyAtmosphereSceneProxy &&
		(!OverrideAtmosphericLight[AtmosphereLightIndex] || OverrideAtmosphericLightDirection[AtmosphereLightIndex]!=LightDirection))
	{
		FSceneInterface* Scene = GetWorld()->Scene;
		OverrideAtmosphericLight[AtmosphereLightIndex] = true;
		OverrideAtmosphericLightDirection[AtmosphereLightIndex] = LightDirection;
		MarkRenderStateDirty();
	}
}

#if WITH_EDITOR
void USkyAtmosphereComponent::SetPrecomputedLut(FTextureRHIRef ScatteringTexture, FTextureRHIRef TranmisttanceTexture, FTextureRHIRef IrradianceTexture)
{
	if (SkyAtmosphereSceneProxy)
	{
		SkyAtmosphereSceneProxy->PrecomputedScatteringLut = ScatteringTexture;
		SkyAtmosphereSceneProxy->PrecomputedTranmisttanceLut = TranmisttanceTexture;
		SkyAtmosphereSceneProxy->PrecomputedIrradianceLut = IrradianceTexture;

		SkyAtmosphereSceneProxy->RenderSceneInfo->UpdatePrecomputedLuts();
	}
}
#endif

void USkyAtmosphereComponent::GetOverrideLightStatus(bool* OutOverrideAtmosphericLight, FVector* OutOverrideAtmosphericLightDirection) const
{
	memcpy(OutOverrideAtmosphericLight, OverrideAtmosphericLight, sizeof(OverrideAtmosphericLight));
	memcpy(OutOverrideAtmosphericLightDirection, OverrideAtmosphericLightDirection, sizeof(OverrideAtmosphericLightDirection));
}

#define SKY_DECLARE_BLUEPRINT_SETFUNCTION(MemberType, MemberName) void USkyAtmosphereComponent::Set##MemberName(MemberType NewValue)\
{\
	if (AreDynamicDataChangesAllowed() && MemberName != NewValue)\
	{\
		MemberName = NewValue;\
		MarkRenderStateDirty();\
	}\
}\

#define SKY_DECLARE_BLUEPRINT_SETFUNCTION_LINEARCOEFFICIENT(MemberName) void USkyAtmosphereComponent::Set##MemberName(FLinearColor NewValue)\
{\
	if (AreDynamicDataChangesAllowed() && MemberName != NewValue)\
	{\
		MemberName = NewValue.GetClamped(0.0f, 1e38f); \
		MarkRenderStateDirty();\
	}\
}\

SKY_DECLARE_BLUEPRINT_SETFUNCTION(float, RayleighScatteringScale);
SKY_DECLARE_BLUEPRINT_SETFUNCTION_LINEARCOEFFICIENT(RayleighScattering);
SKY_DECLARE_BLUEPRINT_SETFUNCTION(float, RayleighExponentialDistribution);

SKY_DECLARE_BLUEPRINT_SETFUNCTION(float, MieScatteringScale);
SKY_DECLARE_BLUEPRINT_SETFUNCTION_LINEARCOEFFICIENT(MieScattering);
SKY_DECLARE_BLUEPRINT_SETFUNCTION(float, MieAbsorptionScale);
SKY_DECLARE_BLUEPRINT_SETFUNCTION_LINEARCOEFFICIENT(MieAbsorption);
SKY_DECLARE_BLUEPRINT_SETFUNCTION(float, MieAnisotropy);
SKY_DECLARE_BLUEPRINT_SETFUNCTION(float, MieExponentialDistribution);

SKY_DECLARE_BLUEPRINT_SETFUNCTION(float, OtherAbsorptionScale);
SKY_DECLARE_BLUEPRINT_SETFUNCTION_LINEARCOEFFICIENT(OtherAbsorption);

SKY_DECLARE_BLUEPRINT_SETFUNCTION_LINEARCOEFFICIENT(SkyLuminanceFactor);
SKY_DECLARE_BLUEPRINT_SETFUNCTION(float, AerialPespectiveViewDistanceScale);
SKY_DECLARE_BLUEPRINT_SETFUNCTION(float, HeightFogContribution);

/*=============================================================================
	ASkyAtmosphere implementation.
=============================================================================*/

#if WITH_EDITOR
#include "ObjectEditorUtils.h"
#endif

ASkyAtmosphere::ASkyAtmosphere(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SkyAtmosphereComponent = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("SkyAtmosphereComponent"));
	RootComponent = SkyAtmosphereComponent;

#if WITH_EDITORONLY_DATA
	ArrowComponent = CreateEditorOnlyDefaultSubobject<UArrowComponent>(TEXT("ArrowComponent"));

	if (!IsRunningCommandlet())
	{
		// Structure to hold one-time initialization
		struct FConstructorStatics
		{
			ConstructorHelpers::FObjectFinderOptional<UTexture2D> SkyAtmosphereTextureObject;
			FName ID_SkyAtmosphere;
			FText NAME_SkyAtmosphere;
			FConstructorStatics()
				: SkyAtmosphereTextureObject(TEXT("/Engine/EditorResources/S_SkyAtmosphere"))
				, ID_SkyAtmosphere(TEXT("Fog"))
				, NAME_SkyAtmosphere(NSLOCTEXT("SpriteCategory", "Fog", "Fog"))
			{
			}
		};
		static FConstructorStatics ConstructorStatics;

		if (GetSpriteComponent())
		{
			GetSpriteComponent()->Sprite = ConstructorStatics.SkyAtmosphereTextureObject.Get();
			GetSpriteComponent()->SetRelativeScale3D(FVector(0.5f, 0.5f, 0.5f));
			GetSpriteComponent()->SpriteInfo.Category = ConstructorStatics.ID_SkyAtmosphere;
			GetSpriteComponent()->SpriteInfo.DisplayName = ConstructorStatics.NAME_SkyAtmosphere;
			GetSpriteComponent()->SetupAttachment(SkyAtmosphereComponent);
		}

		if (ArrowComponent)
		{
			ArrowComponent->ArrowColor = FColor(150, 200, 255);

			ArrowComponent->bTreatAsASprite = true;
			ArrowComponent->SpriteInfo.Category = ConstructorStatics.ID_SkyAtmosphere;
			ArrowComponent->SpriteInfo.DisplayName = ConstructorStatics.NAME_SkyAtmosphere;
			ArrowComponent->SetupAttachment(SkyAtmosphereComponent);
			ArrowComponent->bLightAttachment = true;
			ArrowComponent->bIsScreenSizeScaled = true;
		}
	}
#endif // WITH_EDITORONLY_DATA

	PrimaryActorTick.bCanEverTick = true;
	SetHidden(false);
}



/*=============================================================================
	FSkyAtmosphereSceneProxy implementation.
=============================================================================*/



FSkyAtmosphereSceneProxy::FSkyAtmosphereSceneProxy(const USkyAtmosphereComponent* InComponent)
	: bStaticLightingBuilt(false)
	, AtmosphereSetup(*InComponent)
{
	SkyLuminanceFactor = InComponent->SkyLuminanceFactor;
	AerialPespectiveViewDistanceScale = InComponent->AerialPespectiveViewDistanceScale;
	HeightFogContribution = InComponent->HeightFogContribution;

	InComponent->GetOverrideLightStatus(OverrideAtmosphericLight, OverrideAtmosphericLightDirection);

	TransmittanceAtZenith = AtmosphereSetup.GetTransmittanceAtGroundLevel(FVector(0.0f, 0.0f, 1.0f));

	//@StarLight code - START Precomputed Multi Scattering on mobile, edit by wanghai
	if (InComponent->PrecomputedScatteringLut && InComponent->PrecomputedScatteringLut->Resource && InComponent->PrecomputedScatteringLut->Resource->TextureRHI.IsValid())
	{
		PrecomputedScatteringLut = InComponent->PrecomputedScatteringLut->Resource->TextureRHI;
	}
	if (InComponent->PrecomputedTranmisttanceLut && InComponent->PrecomputedTranmisttanceLut->Resource && InComponent->PrecomputedTranmisttanceLut->Resource->TextureRHI.IsValid())
	{
		PrecomputedTranmisttanceLut = InComponent->PrecomputedTranmisttanceLut->Resource->TextureRHI;
	}
	if (InComponent->PrecomputedIrradianceLut && InComponent->PrecomputedIrradianceLut->Resource && InComponent->PrecomputedIrradianceLut->Resource->TextureRHI.IsValid())
	{
		PrecomputedIrradianceLut = InComponent->PrecomputedIrradianceLut->Resource->TextureRHI;
	}
#if WITH_EDITOR
	TempComponent = const_cast<USkyAtmosphereComponent*>(InComponent);
#endif
	//@StarLight code - END Precomputed Multi Scattering on mobile, edit by wanghai
}

FSkyAtmosphereSceneProxy::~FSkyAtmosphereSceneProxy()
{
}

FVector FSkyAtmosphereSceneProxy::GetAtmosphereLightDirection(int32 AtmosphereLightIndex, const FVector& DefaultDirection) const
{
	if (OverrideAtmosphericLight[AtmosphereLightIndex])
	{
		return OverrideAtmosphericLightDirection[AtmosphereLightIndex];
	}
	return DefaultDirection;
}

#if WITH_EDITOR
//@StarLight code - START Precomputed Multi Scattering on mobile, edit by wanghai
template<typename TextureType>
TextureType* SavePrecomputedLut(FTextureRHIRef InTexture, FIntVector TextureSize, const FString& InTextureName)
{
	if (!InTexture.IsValid())
	{
		return nullptr;
	}

	FRHITexture2D* InTexture2D = InTexture->GetTexture2D();
	FRHITexture3D* InTexture3D = InTexture->GetTexture3D();
	FString TextureName = InTextureName;
	FString PackageName = TEXT("/Game/SkyAtmosphereLuts/");
	PackageName += TextureName;

	TextureType* CurTexture = LoadObject<TextureType>(nullptr, *PackageName);

	if (CurTexture)
	{
		CurTexture->RemoveFromRoot();
		TArray<UObject*> DeleteAsset = { CurTexture };
		ObjectTools::ForceDeleteObjects(DeleteAsset, false);
	}

	UPackage* Package = CreatePackage(NULL, *PackageName);
	Package->FullyLoad();
	TextureType* MultiScatteringLut = NewObject<TextureType>(Package, *TextureName, RF_Public | RF_Standalone | RF_MarkAsRootSet);
	MultiScatteringLut->AddToRoot();
	MultiScatteringLut->PlatformData = new FTexturePlatformData();
	MultiScatteringLut->PlatformData->SizeX = TextureSize.X;
	MultiScatteringLut->PlatformData->SizeY = TextureSize.Y;
	MultiScatteringLut->PlatformData->SetNumSlices(TextureSize.Z);
	MultiScatteringLut->PlatformData->PixelFormat = EPixelFormat::PF_FloatRGBA;
	MultiScatteringLut->CompressionQuality = ETextureCompressionQuality::TCQ_Medium;
	MultiScatteringLut->MipGenSettings = TextureMipGenSettings::TMGS_NoMipmaps;
	MultiScatteringLut->SRGB = false;
	MultiScatteringLut->CompressionSettings = TC_HDR;

	int32 Index = 0;
	//if (!CurTexture)
	{
		Index = MultiScatteringLut->PlatformData->Mips.Add(new FTexture2DMipMap());
	}
	FTexture2DMipMap* Mip = &MultiScatteringLut->PlatformData->Mips[Index];
	Mip->SizeX = TextureSize.X;
	Mip->SizeY = TextureSize.Y;
	Mip->SizeZ = TextureSize.Z;
	
	TArray<FFloat16Color> PixelData;
	uint32 DataNum = TextureSize.X * TextureSize.Y * TextureSize.Z;
	PixelData.SetNumZeroed(DataNum);

	if (InTexture3D)
	{
		ENQUEUE_RENDER_COMMAND(FReadBackScattering)([InTexture3D, TextureSize, &PixelData](FRHICommandListImmediate& RHICmdList) {
			RHICmdList.Read3DSurfaceFloatData(InTexture3D, FIntRect(0, 0, TextureSize.X, TextureSize.Y), FIntPoint(0, TextureSize.Z), PixelData);
		});
	}
	else if (InTexture2D)
	{
		ENQUEUE_RENDER_COMMAND(FReadBackTextures)([InTexture2D, TextureSize, &PixelData](FRHICommandListImmediate& RHICmdList) {
			uint32 DestStride = 0;
			if (InTexture2D->GetFormat() == EPixelFormat::PF_FloatRGBA)
			{
				FIntPoint Texture2DSize = InTexture2D->GetSizeXY();
				int32 Scale = Texture2DSize.X * Texture2DSize.Y / (TextureSize.X * TextureSize.Y * TextureSize.Z);
				RHICmdList.ReadSurfaceFloatData(InTexture2D, FIntRect(0, 0, Texture2DSize.X, Texture2DSize.Y / Scale), PixelData, CubeFace_PosX, 0 , 0);
			}
			else if (InTexture2D->GetFormat() == EPixelFormat::PF_A32B32G32R32F)
			{
				FIntPoint Texture2DSize = InTexture2D->GetSizeXY();
				TArray<FLinearColor> LinearColors;
				RHICmdList.ReadSurfaceData(InTexture2D, FIntRect(0, 0, Texture2DSize.X, Texture2DSize.Y), LinearColors, FReadSurfaceDataFlags());

				for (int32 i = 0; i < PixelData.Num(); ++i)
				{
					PixelData[i] = FFloat16Color(LinearColors[i]);
				}

				/*FLinearColor* Texture2DData = (FLinearColor*)RHILockTexture2D(InTexture2D, 0, RLM_ReadOnly, DestStride, true);
				RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
				int32 RowDataSize = InTexture2D->GetSizeX() * sizeof(FLinearColor);
				for (uint32 i = 0; i < InTexture2D->GetSizeY(); ++i)
				{
					for (uint32 j = 0; j < InTexture2D->GetSizeX(); ++j)
					{
						PixelData[i * InTexture2D->GetSizeX() + j] = FFloat16Color(Texture2DData[j]);
					}
					Texture2DData += DestStride / sizeof(FLinearColor);
				}
				RHIUnlockTexture2D(InTexture2D, 0, false);*/
			}
		});
	}

	FlushRenderingCommands();

	// Lock the texture so it can be modified
	Mip->BulkData.Lock(LOCK_READ_WRITE);
	uint8* TextureData = (uint8*)Mip->BulkData.Realloc(TextureSize.X * TextureSize.Y * TextureSize.Z * sizeof(FFloat16Color));
	FMemory::Memcpy(TextureData, PixelData.GetData(), PixelData.Num() * sizeof(FFloat16Color));

	Mip->BulkData.Unlock();
	MultiScatteringLut->Source.Init(TextureSize.X, TextureSize.Y, TextureSize.Z, 1, ETextureSourceFormat::TSF_RGBA16F, (uint8*)PixelData.GetData());
	MultiScatteringLut->UpdateResource();
	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(MultiScatteringLut);

	FlushRenderingCommands();
	FString PackageFileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	TArray<UPackage*> Packages = { Package };
	FEditorFileUtils::PromptForCheckoutAndSave(Packages, true, /*bPromptToSave=*/ false);
	//bool bSaved = UPackage::SavePackage(Package, MultiScatteringLut, EObjectFlags::RF_Public | EObjectFlags::RF_Standalone | EObjectFlags::RF_Transactional, *PackageFileName, GError, nullptr, true, true, SAVE_NoError);

	return MultiScatteringLut;
}

void FSkyAtmosphereSceneProxy::SavePrecomputedLuts()
{
	FTextureRHIRef StaticLightScatteringLutTexture = RenderSceneInfo->GetStaticLightScatteringLutTexture()->GetRenderTargetItem().TargetableTexture;
	FTextureRHIRef MultiScatteringLutTextureSwapA = RenderSceneInfo->GetMultiScatteringLutTextureSwapA()->GetRenderTargetItem().TargetableTexture;
	FTextureRHIRef ScatteringAltasTexture = RenderSceneInfo->GetScatteringAltasTexture()->GetRenderTargetItem().TargetableTexture;
	FTextureRHIRef TransmittanceLutTexture = RenderSceneInfo->GetTransmittanceLutTexture()->GetRenderTargetItem().TargetableTexture;
	FTextureRHIRef IrradianceLutTextureSwapA = RenderSceneInfo->GetIrradianceLutTextureSwapA()->GetRenderTargetItem().TargetableTexture;

	bool bUseStaticLight = AtmosphereSetup.bUseStaticLight;

	TWeakObjectPtr<USkyAtmosphereComponent> ComponentRef = TempComponent;
	auto SaveTask = FFunctionGraphTask::CreateAndDispatchWhenReady([ComponentRef, bUseStaticLight, StaticLightScatteringLutTexture, MultiScatteringLutTextureSwapA, ScatteringAltasTexture, TransmittanceLutTexture, IrradianceLutTextureSwapA]()
	{
		FlushRenderingCommands();
		
		if (ComponentRef.IsValid())
		{
			if(ComponentRef->PrecomputedScatteringLut)ComponentRef->PrecomputedScatteringLut->RemoveFromRoot();
			if(ComponentRef->PrecomputedTranmisttanceLut)ComponentRef->PrecomputedTranmisttanceLut->RemoveFromRoot();
			if(ComponentRef->PrecomputedIrradianceLut)ComponentRef->PrecomputedIrradianceLut->RemoveFromRoot();
		}
		FString MapName = GWorld->GetMapName();
		FTextureRHIRef InTexture = bUseStaticLight ? StaticLightScatteringLutTexture : ScatteringAltasTexture;
		FIntVector TextureSize = bUseStaticLight ? StaticLightScatteringLutTexture->GetSizeXYZ() : MultiScatteringLutTextureSwapA->GetSizeXYZ();
		TextureSize.Z /= 4;
		UVolumeTexture* ScatteringTexture = SavePrecomputedLut<UVolumeTexture>(InTexture, TextureSize, TEXT("Tex_ScatteringTexture_") + MapName);
		InTexture = TransmittanceLutTexture;
		UTexture2D* TranmittanceTexture = SavePrecomputedLut<UTexture2D>(InTexture, InTexture->GetSizeXYZ(), TEXT("Tex_Tranmittance_") + MapName);
		InTexture = IrradianceLutTextureSwapA;
		UTexture2D* IrradianceTexture = SavePrecomputedLut<UTexture2D>(InTexture, InTexture->GetSizeXYZ(), TEXT("Tex_Irradiance_") + MapName);

		// Update current texture
		if (ComponentRef.IsValid())
		{
			ComponentRef->SetPrecomputedLut(ScatteringTexture->Resource->TextureRHI, TranmittanceTexture->Resource->TextureRHI, IrradianceTexture->Resource->TextureRHI);
		}
		
	},  TStatId(), nullptr, ENamedThreads::GameThread);

	//FTaskGraphInterface::Get().WaitUntilTaskCompletes(SaveTask);
}
#endif 
//@StarLight code - END Precomputed Multi Scattering on mobile, edit by wanghai
#undef LOCTEXT_NAMESPACE


