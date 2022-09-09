#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "EngineDefines.h"
#include "GameFramework/Info.h"
#include "Misc/Guid.h"
#include "RenderResource.h"

#include "RainDepthComponent.generated.h"

class FRainDepthSceneProxy;
class FRainDepthProjectedInfo;
/**
 * 
 */
UCLASS(ClassGroup = Rendering/*, collapsecategories, hidecategories = (Object, Mobility, Activation, "Components|Activation"), editinlinenew*/, meta = (BlueprintSpawnableComponent))
class ENGINE_API URainDepthComponent : public USceneComponent
{
	GENERATED_BODY()

	URainDepthComponent();

public:
	UPROPERTY(EditAnywhere, Category = RainCapture)
	int32 DepthResolutionX = 512;

	UPROPERTY(EditAnywhere, Category = RainCapture)
	int32 DepthResolutionY = 512;

	UPROPERTY(EditAnywhere, Category = RainCapture)
	float CaptureViewWidth = 4000.f;

	UPROPERTY(EditAnywhere, Category = RainCapture)
	float CaptureViewHeight = 4000.f;

	UPROPERTY(EditAnywhere, Category = RainCapture)
	float MaxDepth = 20000.f;

	UPROPERTY(EditAnywhere, Category = RainCapture)
	bool bSaveDepthTexture = false;

	UPROPERTY(EditAnywhere, Category = RainCapture)
	FString RainDepthStoragePath;

	FRainDepthSceneProxy* RainDepthSceneProxy;

public:
	void SaveDepthTexture(FTextureRHIRef InTexture);

	FMatrix GetCaptureProjectMatrix();

protected:
	//~ Begin UActorComponent Interface.
	virtual void CreateRenderState_Concurrent(FRegisterComponentContext* Context) override;
	virtual void SendRenderTransform_Concurrent() override;
	virtual void DestroyRenderState_Concurrent() override;
	//~ End UActorComponent Interface.

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
};

UCLASS(showcategories = (Movement, Rendering, "Input|MouseInput", "Input|TouchInput"), hidecategories = (Info, Object, Input), MinimalAPI)
class ARainDepthCapture : public AInfo
{
	GENERATED_BODY()

public:
	ARainDepthCapture();

private:
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = Atmosphere, meta = (AllowPrivateAccess = "true"))
	class URainDepthComponent* RainDepthComponent;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	class UArrowComponent* ArrowComponent;
#endif

public:
	/** Returns SkyAtmosphereComponent subobject */
	ENGINE_API URainDepthComponent* GetComponent() const { return RainDepthComponent; }

};


class ENGINE_API FRainDepthSceneProxy
{
public:
	FRainDepthSceneProxy(URainDepthComponent* InRainDepthComponent);

	~FRainDepthSceneProxy();

	URainDepthComponent* RainDepthComponent;

	FIntPoint DepthResolution;

	float CaptureViewWidth;
	float CaptureViewHeight;

	float MaxDepth;

	FVector DepthCapturePosition;

	FRotator DepthCaptureRotation;

	FRainDepthProjectedInfo* RainDepthInfo;

	bool bShouldSaveTexture;

public:
	void SaveDepthTextureAsAsset(FTextureRHIRef InTexture);
};