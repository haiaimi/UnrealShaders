#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RayMarchingShader.generated.h"

USTRUCT(BlueprintType)
struct FRayMarchingBufferData
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadWrite, VisibleAnywhere, Category = SeascapeBufferData)
	FVector2D ViewResolution;

	UPROPERTY(BlueprintReadWrite, VisibleAnywhere, Category = SeascapeBufferData)
	float TimeSeconds;

	UPROPERTY(BlueprintReadWrite, VisibleAnywhere, Category = SeascapeBufferData)
	FVector2D MousePos;
};

UCLASS(MinimalAPI, meta = (ScriptName = "UniformBufferShaderLibrary"))
class URayMarchingBlueprintLibrary :public UBlueprintFunctionLibrary
{
	GENERATED_UCLASS_BODY()

	UFUNCTION(BlueprintCallable, Category = "UsingShaderPlugin", meta = (WorldContext = "WorldContext"))
	static void DrawRayMarchingRenderTarget(class UTextureRenderTarget* OutputRenderTarget, AActor* MyActor, FRayMarchingBufferData ShaderStructData);
};