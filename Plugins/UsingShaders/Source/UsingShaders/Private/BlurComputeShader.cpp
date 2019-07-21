#include "BlurComputeShader.h"
#include "GlobalShader.h"
#include "ShaderParameterUtils.h"
#include "RHIStaticStates.h"
#include "Engine/Texture.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "SceneInterface.h"
#include "RHIResources.h"
#include "RenderResource.h"

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FBlurComputeShaderData, )
//SHADER_PARAMETER(TArray<float>, )
END_GLOBAL_SHADER_PARAMETER_STRUCT()


IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FBlurComputeShaderData, "FBlurData");   

#define GROUP_THREAD_COUNTS 32

class FBlurComputeShader : public FGlobalShader
{
public:
	FBlurComputeShader() {}

	FBlurComputeShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		InputSurface.Bind(Initializer.ParameterMap, TEXT("InputTexture"));
		InputSampler.Bind(Initializer.ParameterMap, TEXT("TextureSampler"));
		OutputSurface.Bind(Initializer.ParameterMap, TEXT("OutputTexture"));
	}

	static bool ShouldCache(EShaderPlatform Platform)
	{
		return true;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Paramers)
	{
		return true;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("BLUR_MICRO"), 1);
	}

	void SetParameters(FRHICommandListImmediate& RHICmdList, FBlurComputeShaderData& BlurData, FTextureRHIParamRef& InTexture,FUnorderedAccessViewRHIRef& OutputUAV)
	{
		if (OutputSurface.IsBound())
			RHICmdList.SetUAVParameter(GetComputeShader(), OutputSurface.GetBaseIndex(), OutputUAV);

		SetTextureParameter(RHICmdList, GetComputeShader(), InputSurface, InputSampler, TStaticSamplerState<SF_AnisotropicLinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), InTexture);

		SetUniformBufferParameter(RHICmdList, GetPixelShader(), GetUniformBufferParameter<FBlurComputeShaderData>(), FBlurComputeShaderData::CreateUniformBuffer(BlurData, EUniformBufferUsage::UniformBuffer_SingleDraw));
	}

	//UAV需要解绑，以便其他地方使用
	void UnbindUAV(FRHICommandList& RHICmdList)
	{
		if (OutputSurface.IsBound())
			RHICmdList.SetUAVParameter(GetComputeShader(), OutputSurface.GetBaseIndex(), FUnorderedAccessViewRHIParamRef());
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << InputSurface << OutputSurface;
		return bShaderHasOutdatedParameters;
	}

private:
	FShaderResourceParameter InputSurface;

	FShaderResourceParameter InputSampler;

	FShaderResourceParameter OutputSurface;
};

IMPLEMENT_SHADER_TYPE(, FBlurComputeShader, TEXT("/Plugins/Shaders/Private/SeascapeShader.usf"), TEXT("MainCS"), SF_Compute)

static void ExcuteBlurComputeShader_RenderThread(
		FRHICommandListImmediate& RHICmdList,
		ERHIFeatureLevel::Type FeatureLevel,
		FTextureRHIParamRef InTexture,
		FUnorderedAccessViewRHIRef OutputUAV)

{
	check(IsInRenderingThread())

	TShaderMapRef<FBlurComputeShader> ComputeShader(GetGlobalShaderMap(FeatureLevel));
	RHICmdList.SetComputeShader(ComputeShader->GetComputeShader());

	ComputeShader->SetParameters(RHICmdList, FBlurComputeShaderData(), InTexture, OutputUAV);
	DispatchComputeShader(RHICmdList, *ComputeShader, InTexture->GetTexture2D()->GetSizeX() / GROUP_THREAD_COUNTS, InTexture->GetTexture2D()->GetSizeY() / GROUP_THREAD_COUNTS, 1);        //调度计算着色器
	ComputeShader->UnbindUAV(RHICmdList);
}

UBlurComputeShaderBlueprintLibrary::UBlurComputeShaderBlueprintLibrary(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{}


void UBlurComputeShaderBlueprintLibrary::DrawBlurComputeShaderRenderTarget(AActor* Ac, FTextureRHIParamRef MyTextureRHI)
{
	//创建UAV图
	FTextureRHIRef Texture = RHICreateTexture2D(MyTextureRHI->GetTexture2D()->GetSizeX(), MyTextureRHI->GetTexture2D()->GetSizeY(), PF_A32B32G32R32F, 1, 1, TexCreate_ShaderResource | TexCreate_UAV, FRHIResourceCreateInfo());
	FUnorderedAccessViewRHIParamRef TextureUAV = RHICreateUnorderedAccessView(Texture);

	UWorld* World = Ac->GetWorld();
	ERHIFeatureLevel::Type FeatureLevel = World->Scene->GetFeatureLevel();

	ENQUEUE_RENDER_COMMAND(CaptureCommand)([FeatureLevel, MyTextureRHI, TextureUAV](FRHICommandListImmediate& RHICmdList)
	{
		ExcuteBlurComputeShader_RenderThread(RHICmdList, FeatureLevel, MyTextureRHI, TextureUAV);
	});
}
