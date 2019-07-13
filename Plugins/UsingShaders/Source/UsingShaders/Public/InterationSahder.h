#pragma once
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Classes/Kismet/BlueprintFunctionLibrary.h"
#include "InterationShader.generated.h"

/**
 * 
 */
UCLASS(MinimalAPI, meta = (ScriptName = "HaiaimiShaderLibrary"))
class UInterationShaderBlueprintLibrary :public UBlueprintFunctionLibrary
{
	GENERATED_UCLASS_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "UsingShaderPlugin", meta = (WorldContext = "WorldContext"))
	static void DrawInterationShaderRenderTarget(class UTextureRenderTarget* OutputRenderTarget, AActor* Ac, FLinearColor MyColor, class UTexture* MyTexture);
};



