#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UniformBufferShader.generated.h"

USTRUCT(BlueprintType)
struct FUniformBufferData
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadWrite, VisibleAnywhere, Category = UniformBufferData)
	FLinearColor ColorOne;

	UPROPERTY(BlueprintReadWrite, VisibleAnywhere, Category = UniformBufferData)
	FLinearColor ColorTwo;

	UPROPERTY(BlueprintReadWrite, VisibleAnywhere, Category = UniformBufferData)
	FLinearColor ColorThree;

	UPROPERTY(BlueprintReadWrite, VisibleAnywhere, Category = UniformBufferData)
	FLinearColor ColorFour;

	UPROPERTY(BlueprintReadWrite, VisibleAnywhere, Category = UniformBufferData)
	int32 ColorIndex;
};

UCLASS(MinimalAPI, meta = (ScriptName = "UniformBufferShaderLibrary"))
class UUniformShaderBlueprintLibrary :public UBlueprintFunctionLibrary
{
	GENERATED_UCLASS_BODY()

	UFUNCTION(BlueprintCallable, Category = "UsingShaderPlugin", meta = (WorldContext = "WorldContext"))
	static void DrawUniformBufferShaderRenderTarget(class UTextureRenderTarget* OutputRenderTarget, AActor* Ac, FLinearColor AcColor, class UTexture* MyTexture, FUniformBufferData ShaderStructData);
};