// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Classes/Kismet/BlueprintFunctionLibrary.h"
#include "TestShader.generated.h"

/**
 * 
 */
UCLASS(MinimalAPI, meta = (ScriptName = "HaiaimiShaderLibrary"))
class USINGSHADERS_API UTestShaderBlueprintLibrary : UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UTestShaderBlueprintLibrary();

	UFUNCTION(BlueprintCallable, Category = "UsingShaderPlugin", meta = (WorldContext = "WorldContext"))
	static void DrawTestShaderRenderTarget(class UTextureRenderTarget* OutputRenderTarget, AActor* Ac, FLinearColor AcColor);
};
