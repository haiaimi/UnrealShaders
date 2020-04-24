#include "BlurComputeShader.h"
#include "GlobalShader.h"
#include "ShaderParameterUtils.h"
#include "RHIStaticStates.h"
#include "Engine/Texture.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "SceneInterface.h"
#include "RHICommandList.h"
#include "Engine/Engine.h"

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FBlurComputeShaderData, )
//SHADER_PARAMETER(TArray<float>, )
END_GLOBAL_SHADER_PARAMETER_STRUCT()


IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FBlurComputeShaderData, "FBlurData");   

//线程组中线程数，对应shader中的定义
#define GROUP_THREAD_COUNTS 64        

template<int Index>
class FBlurComputeShader : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FBlurComputeShader, Global)
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

	void SetParameters(FRHICommandListImmediate& RHICmdList, FRHITexture* InTexture, FRHIUnorderedAccessView* OutputUAV)
	{
		if (OutputSurface.IsBound())
			RHICmdList.SetUAVParameter(GetComputeShader(), OutputSurface.GetBaseIndex(), OutputUAV);
		
		//RHICmdList.SetShaderTexture(GetComputeShader(), InputSurface.GetBaseIndex(), InTexture);
		SetTextureParameter(RHICmdList, GetComputeShader(), InputSurface, InputSampler, TStaticSamplerState<SF_AnisotropicLinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), InTexture);

		//SetUniformBufferParameter(RHICmdList, GetPixelShader(), GetUniformBufferParameter<FBlurComputeShaderData>(), FBlurComputeShaderData::CreateUniformBuffer(BlurData, EUniformBufferUsage::UniformBuffer_SingleDraw));
	}

	//UAV需要解绑，以便其他地方使用
	void UnbindUAV(FRHICommandList& RHICmdList)
	{
		if (OutputSurface.IsBound())
			RHICmdList.SetUAVParameter(GetComputeShader(), OutputSurface.GetBaseIndex(), nullptr);
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

IMPLEMENT_SHADER_TYPE(template<>, FBlurComputeShader<1>, TEXT("/Plugins/Shaders/Private/BlurComputeShader.usf"), TEXT("HorzBlurCS"), SF_Compute)
IMPLEMENT_SHADER_TYPE(template<>, FBlurComputeShader<2>, TEXT("/Plugins/Shaders/Private/BlurComputeShader.usf"), TEXT("VertBlurCS"), SF_Compute)

static void ExcuteBlurComputeShader_RenderThread(
		FRHICommandListImmediate& RHICmdList,
		ERHIFeatureLevel::Type FeatureLevel,
		int32 BlurCounts,
		FRHITexture* InTexture,
		FRHITexture* UAVSource,
		FRHIUnorderedAccessView* OutputUAV)

{
	check(IsInRenderingThread())

	TShaderMapRef<FBlurComputeShader<1>> ComputeShaderHorz(GetGlobalShaderMap(FeatureLevel));
	TShaderMapRef<FBlurComputeShader<2>> ComputeShaderVert(GetGlobalShaderMap(FeatureLevel));

	//创建UAV图
	FRHIResourceCreateInfo RHIResourceCreateInfo;
	FTexture2DRHIRef TempTexture = RHICreateTexture2D(InTexture->GetSizeXYZ().X, InTexture->GetSizeXYZ().Y, PF_A32B32G32R32F, 1, 1, TexCreate_ShaderResource | TexCreate_UAV, RHIResourceCreateInfo);
	FUnorderedAccessViewRHIRef TempTextureUAV = RHICreateUnorderedAccessView(TempTexture);

	//GEngine->AddOnScreenDebugMessage(-1, 10, FColor::Blue, InTexture->GetSizeXYZ().ToString());
	FRHITexture* IterTexture = InTexture;

	//多次模糊处理
	for (int32 i = 0; i < BlurCounts; ++i)
	{
		RHICmdList.SetComputeShader(ComputeShaderHorz->GetComputeShader());
		ComputeShaderHorz->SetParameters(RHICmdList, IterTexture, TempTextureUAV);
		DispatchComputeShader(RHICmdList, *ComputeShaderHorz, InTexture->GetSizeXYZ().X / GROUP_THREAD_COUNTS, InTexture->GetSizeXYZ().Y, 1);        //横向计算着色器
		ComputeShaderHorz->UnbindUAV(RHICmdList);

		RHICmdList.SetComputeShader(ComputeShaderVert->GetComputeShader());
		ComputeShaderVert->SetParameters(RHICmdList, TempTexture, OutputUAV);
		DispatchComputeShader(RHICmdList, *ComputeShaderVert, InTexture->GetSizeXYZ().X, InTexture->GetSizeXYZ().Y / GROUP_THREAD_COUNTS, 1);        //纵向计算着色器
		ComputeShaderVert->UnbindUAV(RHICmdList);
		IterTexture = UAVSource;
	}
	
	TempTextureUAV.SafeRelease();
}

void DrawBlurComputeShaderRenderTarget(AActor* Ac, int32 BlurCounts, FRHITexture* MyTexture, FRHITexture* UAVSource, FRHIUnorderedAccessView* TextureUAV)
{
	UWorld* World = Ac->GetWorld();
	ERHIFeatureLevel::Type FeatureLevel = World->Scene->GetFeatureLevel();

	ENQUEUE_RENDER_COMMAND(CaptureCommand)([FeatureLevel, BlurCounts, MyTexture, UAVSource, TextureUAV](FRHICommandListImmediate& RHICmdList)
	{
		ExcuteBlurComputeShader_RenderThread(RHICmdList, FeatureLevel, BlurCounts, MyTexture, UAVSource, TextureUAV);
	});
}
