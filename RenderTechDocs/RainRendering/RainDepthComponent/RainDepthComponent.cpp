#include "Components/RainDepthComponent.h"
#include "Components/ArrowComponent.h"
#include "AssetRegistryModule.h"
#if WITH_EDITOR
#include "FileHelpers.h"
#endif

URainDepthComponent::URainDepthComponent()
{
	RainDepthStoragePath = TEXT("/Game/Arts/Effects/Texture/");
}

void URainDepthComponent::SaveDepthTexture(FTextureRHIRef InTexture)
{
#if WITH_EDITOR
	if (!InTexture.IsValid())
	{
		return;
	}
	FlushRenderingCommands();
	FString MapName = TEXT("Null");
	if (GetWorld())
	{
		MapName = GetWorld()->GetMapName();
	}
	FRHITexture2D* InTexture2D = InTexture->GetTexture2D();
	FString TextureName = TEXT("TX_RainDepth_") + MapName;
	FString PackageName = RainDepthStoragePath + TextureName;

	UTexture2D* CurTexture = LoadObject<UTexture2D>(nullptr, *PackageName);

	UPackage* Package = CreatePackage(NULL, *PackageName);
	Package->FullyLoad();
	UTexture2D* RainDepthTexture = NewObject<UTexture2D>(Package, *TextureName, RF_Public | RF_Standalone | RF_MarkAsRootSet);
	RainDepthTexture->AddToRoot();
	RainDepthTexture->PlatformData = new FTexturePlatformData();
	RainDepthTexture->PlatformData->SizeX = DepthResolutionX;
	RainDepthTexture->PlatformData->SizeY = DepthResolutionY;
	RainDepthTexture->PlatformData->SetNumSlices(1);
	RainDepthTexture->PlatformData->PixelFormat = EPixelFormat::PF_R16F;
	RainDepthTexture->CompressionQuality = ETextureCompressionQuality::TCQ_Medium;
	RainDepthTexture->MipGenSettings = TextureMipGenSettings::TMGS_NoMipmaps;
	RainDepthTexture->Filter = TextureFilter::TF_Nearest;
	RainDepthTexture->SRGB = false;
	RainDepthTexture->CompressionSettings = TC_EditorIcon;

	int32 Index = 0;
	Index = RainDepthTexture->PlatformData->Mips.Add(new FTexture2DMipMap());

	FTexture2DMipMap* Mip = &RainDepthTexture->PlatformData->Mips[Index];
	Mip->SizeX = DepthResolutionX;
	Mip->SizeY = DepthResolutionY;
	Mip->SizeZ = 1;
	
	TArray<uint16> PixelData;
	uint32 DataNum = DepthResolutionX * DepthResolutionY * 1;
	PixelData.SetNumZeroed(DataNum);

	//for(int32 i = 0; i < DepthResolutionX; ++i)
	//	for (int32 j = 0; j < DepthResolutionY; ++j)
	//	{
	//		PixelData.Add(65535);
	//	}
	if (InTexture2D)
	{
        FIntPoint DepthResolution = FIntPoint(DepthResolutionX, DepthResolutionY);
        ENQUEUE_RENDER_COMMAND(FReadBackRainDepthBuffer)([InTexture2D, &PixelData, DepthResolution](FRHICommandListImmediate& RHICmdList){
            
            uint32 DestStride = 0;
            uint8* Data = (uint8*)RHILockTexture2D(InTexture2D, 0, EResourceLockMode::RLM_ReadOnly, DestStride, false);
            if (DestStride == DepthResolution.X * sizeof(uint16))
            {
                FMemory::Memmove(PixelData.GetData(), Data, DepthResolution.X * DepthResolution.Y * sizeof(uint16));
            }
            else
            {
                uint32 Stride = DepthResolution.X * sizeof(uint16);
                for (int32 i = 0; i < DepthResolution.Y; ++i)
                {
                    FMemory::Memmove((uint8*)(PixelData.GetData() + i * DepthResolution.X), Data, Stride);
                    Data += DestStride;
                }
            }
            RHIUnlockTexture2D(InTexture2D, 0, false);
        });
	}

	FlushRenderingCommands();

	// Lock the texture so it can be modified
	Mip->BulkData.Lock(LOCK_READ_WRITE);
	uint8* TextureData = (uint8*)Mip->BulkData.Realloc(DepthResolutionX * DepthResolutionY * 1 * sizeof(uint16));
	FMemory::Memcpy(TextureData, PixelData.GetData(), PixelData.Num() * sizeof(uint16));
	Mip->BulkData.Unlock();

	RainDepthTexture->Source.Init(DepthResolutionX, DepthResolutionY, 1, 1, ETextureSourceFormat::TSF_G16, (uint8*)PixelData.GetData());
	RainDepthTexture->UpdateResource();
	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(RainDepthTexture);

	FlushRenderingCommands();
	FString PackageFileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	TArray<UPackage*> Packages = { Package };
	FEditorFileUtils::PromptForCheckoutAndSave(Packages, true, /*bPromptToSave=*/ false);
	//bool bSaved = UPackage::SavePackage(Package, MultiScatteringLut, EObjectFlags::RF_Public | EObjectFlags::RF_Standalone | EObjectFlags::RF_Transactional, *PackageFileName, GError, nullptr, true, true, SAVE_NoError);
#endif
}

FMatrix URainDepthComponent::GetCaptureProjectMatrix()
{
	FVector ViewOrigin = GetComponentLocation();
	FOrthoMatrix OrthoProjMatrix(CaptureViewWidth, CaptureViewHeight, 1.f, 0.f);
	FMatrix ViewRotationMatrix = FInverseRotationMatrix(GetComponentRotation());
	
	const FMatrix TmpViewMatrix = FTranslationMatrix(-ViewOrigin) * ViewRotationMatrix * FMatrix(
		FPlane(0, 0, 1, 0),
		FPlane(1, 0, 0, 0),
		FPlane(0, 1, 0, 0),
		FPlane(0, 0, 0, 1));
	FMatrix ViewProjMatrix = TmpViewMatrix * OrthoProjMatrix;

	return ViewProjMatrix;
}

void URainDepthComponent::CreateRenderState_Concurrent(FRegisterComponentContext* Context)
{
	Super::CreateRenderState_Concurrent(Context);

	if (GetVisibleFlag() &&
		ShouldComponentAddToScene() && ShouldRender() && IsRegistered() && (GetOuter() == NULL || !GetOuter()->HasAnyFlags(RF_ClassDefaultObject)))
	{
		// Create the scene proxy.
		RainDepthSceneProxy = new FRainDepthSceneProxy(this);
		GetWorld()->Scene->AddRainDepthCapture(RainDepthSceneProxy);
	}
}

void URainDepthComponent::SendRenderTransform_Concurrent()
{
	Super::SendRenderTransform_Concurrent();
}

void URainDepthComponent::DestroyRenderState_Concurrent()
{
	Super::DestroyRenderState_Concurrent();
	
	if (RainDepthSceneProxy)
	{
		GetWorld()->Scene->RemoveRainDepthCapture(RainDepthSceneProxy);

		FRainDepthSceneProxy* SceneProxy = RainDepthSceneProxy;
		ENQUEUE_RENDER_COMMAND(FDestroyRainDepthSceneProxyCommand)(
			[SceneProxy](FRHICommandList& RHICmdList)
		{
			delete SceneProxy;
		});

		RainDepthSceneProxy = nullptr;
	}
}

#if WITH_EDITOR
void URainDepthComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	//MarkRenderStateDirty();
}
#endif

ARainDepthCapture::ARainDepthCapture()
{
	RainDepthComponent = CreateDefaultSubobject<URainDepthComponent>(TEXT("RainDepthCapture"));
	RootComponent = RainDepthComponent;

#if WITH_EDITORONLY_DATA
	ArrowComponent = CreateEditorOnlyDefaultSubobject<UArrowComponent>(TEXT("ArrowComponent"));

	if (!IsRunningCommandlet())
	{
		if (ArrowComponent)
		{
			ArrowComponent->ArrowColor = FColor(150, 200, 255);

			ArrowComponent->bTreatAsASprite = true;
			ArrowComponent->SetupAttachment(RainDepthComponent);
			//ArrowComponent->bLightAttachment = true;
			ArrowComponent->bIsScreenSizeScaled = true;
		}
	}
#endif // WITH_EDITORONLY_DATA
}


FRainDepthSceneProxy::FRainDepthSceneProxy(URainDepthComponent* InRainDepthComponent)
{
	if(!InRainDepthComponent) return;
	
	RainDepthComponent = InRainDepthComponent;
	DepthResolution = FIntPoint(InRainDepthComponent->DepthResolutionX, InRainDepthComponent->DepthResolutionY);
	DepthCapturePosition = InRainDepthComponent->GetComponentLocation();
	DepthCaptureRotation = InRainDepthComponent->GetComponentRotation();

	CaptureViewWidth = InRainDepthComponent->CaptureViewWidth;
	CaptureViewHeight = InRainDepthComponent->CaptureViewHeight;

	MaxDepth = InRainDepthComponent->MaxDepth;
	RainDepthInfo = nullptr;
	bShouldSaveTexture = InRainDepthComponent->bSaveDepthTexture;
}

FRainDepthSceneProxy::~FRainDepthSceneProxy()
{

}

void FRainDepthSceneProxy::SaveDepthTextureAsAsset(FTextureRHIRef InTexture)
{
	if (RainDepthComponent)
	{
		URainDepthComponent* DepthComponent = RainDepthComponent;
		auto SaveTask = FFunctionGraphTask::CreateAndDispatchWhenReady([DepthComponent, InTexture]()
		{
			FlushRenderingCommands();
			DepthComponent->SaveDepthTexture(InTexture);
			DepthComponent->bSaveDepthTexture = false;
		}, TStatId(), nullptr, ENamedThreads::GameThread);
	}

	bShouldSaveTexture = false;
}
