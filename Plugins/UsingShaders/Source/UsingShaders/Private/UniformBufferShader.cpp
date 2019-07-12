#include "UniformBufferSahder.h"

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FUniformBufferData, )
	SHADER_PARAMETER(FVector4, ColorOne)
	SHADER_PARAMETER(FVector4, ColorTwo)
	SHADER_PARAMETER(FVector4, ColorThree)
	SHADER_PARAMETER(FVector4, ColorFour)
	SHADER_PARAMETER(uint32, ColorIndex) 
END_GLOBAL_SHADER_PARAMETER_STRUCT()

IMPLEMENT_UNIFORM_BUFFER_STRUCT(FUniformBufferData)

UUniformShaderBlueprintLibrary::UUniformShaderBlueprintLibrary(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UUniformShaderBlueprintLibrary::DrawUniformBufferShaderRenderTarget(class UTextureRenderTarget* OutputRenderTarget, AActor* Ac, FLinearColor AcColor, class UTexture* MyTexture, FUniformBufferData ShaderStructData)
{

}

