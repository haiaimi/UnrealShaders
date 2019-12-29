#include "UniformBufferShader.h"
#include "RHIResources.h"
#include "RenderResource.h"
#include "GlobalShader.h"
#include "ShaderParameterUtils.h"
#include "RHIStaticStates.h"
#include "TextureResource.h"
#include "RenderUtils.h"
#include "PipelineStateCache.h"
#include "Engine/Texture.h"
#include "Engine/TextureRenderTarget.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "SceneInterface.h"
#include "Button.h"
#include "Common.h"

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FUniformBufferShaderData, )
SHADER_PARAMETER(FVector4, ColorOne)
SHADER_PARAMETER(FVector4, ColorTwo)
SHADER_PARAMETER(FVector4, ColorThree)
SHADER_PARAMETER(FVector4, ColorFour)
SHADER_PARAMETER(uint32, ColorIndex)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

 //ΪShader����UniformBuffer������Ҫ��Shader�������������������Щ����д�뵽Common.ush��shader�ļ���
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FUniformBufferShaderData, "FUniformData");    

class FUniformBufferShader : public FGlobalShader
{
public:
	FUniformBufferShader() {}

	FUniformBufferShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		SimpleColorVal.Bind(Initializer.ParameterMap, TEXT("SimpleColor"));
		TestTextureVal.Bind(Initializer.ParameterMap, TEXT("UniformBufferTexture"));
		TestTextureSampler.Bind(Initializer.ParameterMap, TEXT("UniformBufferTextureSampler"));
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
		OutEnvironment.SetDefine(TEXT("UNIFORM_BUFFER_MICRO"), 1);
	}

	void SetParameters(FRHICommandListImmediate& RHICmdList, const FLinearColor& MyColor, FTextureRHIParamRef& MyTexture, FUniformBufferData& MyData)
	{
		//����һ����ͨ����
		SetShaderValue(RHICmdList, GetPixelShader(), SimpleColorVal, MyColor);
		 
		//������Ϊ��ɫ�����������Լ��������
		SetTextureParameter(RHICmdList, GetPixelShader(), TestTextureVal, TestTextureSampler, 
				TStaticSamplerState<SF_AnisotropicLinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(),     //����������ã������Թ��ˣ�U��V,W����ά�ȶ���Clamp
				MyTexture);
		//����UniformBuffer
		FUniformBufferShaderData UBShaderData;
		UBShaderData.ColorOne = MyData.ColorOne;
		UBShaderData.ColorTwo = MyData.ColorTwo;
		UBShaderData.ColorThree = MyData.ColorThree;
		UBShaderData.ColorFour = MyData.ColorFour;
		UBShaderData.ColorIndex = MyData.ColorIndex;

		SetUniformBufferParameter(RHICmdList, GetPixelShader(), GetUniformBufferParameter<FUniformBufferShaderData>(), FUniformBufferShaderData::CreateUniformBuffer(UBShaderData, EUniformBufferUsage::UniformBuffer_SingleDraw));
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << SimpleColorVal << TestTextureVal;
		return bShaderHasOutdatedParameters;
	}

private:
	FShaderParameter SimpleColorVal;

	FShaderResourceParameter TestTextureVal;

	FShaderResourceParameter TestTextureSampler;
};

class FUniformBufferShaderVS :public FUniformBufferShader
{
	DECLARE_SHADER_TYPE(FUniformBufferShaderVS, Global);

public:
	FUniformBufferShaderVS() {}

	FUniformBufferShaderVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FUniformBufferShader(Initializer)
	{
	}
};

class FUniformBufferShaderPS :public FUniformBufferShader
{
	DECLARE_SHADER_TYPE(FUniformBufferShaderPS, Global);

public:
	FUniformBufferShaderPS() {}

	FUniformBufferShaderPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FUniformBufferShader(Initializer)
	{
	}
};

IMPLEMENT_SHADER_TYPE(, FUniformBufferShaderVS, TEXT("/Plugins/Shaders/Private/UniformBufferShader.usf"),TEXT("MainVS"), SF_Vertex)
IMPLEMENT_SHADER_TYPE(, FUniformBufferShaderPS, TEXT("/Plugins/Shaders/Private/UniformBufferShader.usf"),TEXT("MainPS"), SF_Pixel)


static void DrawIndexedPrimitiveUP_cpy3(
	FRHICommandList& RHICmdList,
	uint32 PrimitiveType,
	uint32 MinVertexIndex,
	uint32 NumVertices,
	uint32 NumPrimitives,
	const void* IndexData,
	uint32 IndexDataStride,
	const void* VertexData,
	uint32 VertexDataStride)
{
	const uint32 NumIndices = GetVertexCountForPrimitiveCount(NumPrimitives, PrimitiveType);

	FRHIResourceCreateInfo CreateInfo;
	FVertexBufferRHIRef VertexBufferRHI = RHICreateVertexBuffer(VertexDataStride * NumVertices, BUF_Volatile, CreateInfo);        //Buf�����ǸĶ����ͣ�û֡д�룬��������ΪBUF_Volatile
	void* VoidPtr = RHILockVertexBuffer(VertexBufferRHI, 0, VertexDataStride * NumVertices, RLM_WriteOnly);        //lock���㻺��buffer������ֻ��д��
	FPlatformMemory::Memcpy(VoidPtr, VertexData, VertexDataStride * NumVertices);       //���붥������
	RHIUnlockVertexBuffer(VertexBufferRHI);    //unlock���㻺��

	//ͬ�ϴ���һ����������
	FIndexBufferRHIRef IndexBufferRHI = RHICreateIndexBuffer(IndexDataStride, IndexDataStride * NumIndices, BUF_Volatile, CreateInfo);
	void* VoidPtr2 = RHILockIndexBuffer(IndexBufferRHI, 0, IndexDataStride * NumIndices, RLM_WriteOnly);
	FPlatformMemory::Memcpy(VoidPtr2, IndexData, IndexDataStride * NumIndices);
	RHIUnlockIndexBuffer(IndexBufferRHI);

	RHICmdList.SetStreamSource(0, VertexBufferRHI, 0);     //��Բ�ͬ��ͼ�νӿ����ö��㻺��
	RHICmdList.DrawIndexedPrimitive(IndexBufferRHI, MinVertexIndex, 0, NumVertices, 0, NumPrimitives, 1);        //����ͼԪ

	IndexBufferRHI.SafeRelease();
	VertexBufferRHI.SafeRelease();
}

static void DrawUniformBufferShaderRenderTarget_RenderThread(
	FRHICommandListImmediate& RHICmdList,
	FTextureRenderTargetResource* OutputRenderTargetResource,
	ERHIFeatureLevel::Type FeatureLevel,
	FName TextureRenderTargetName,
	FLinearColor MyColor,
	FTextureRHIParamRef MyTexture,
	FUniformBufferData MyData
)
{
	check(IsInRenderingThread())

#if WANTS_DRAW_MESH_EVENTS
	FString EventName;
	TextureRenderTargetName.ToString(EventName);
	SCOPED_DRAW_EVENTF(RHICmdList, SceneCapture, TEXT("UniformBufferShader%s"), *EventName);
#else
	SCOPED_DRAW_EVENTF(RHICmdList, DrawUVDisplacementRenderTarget_RenderThread);
#endif

	/*SetRenderTarget(RHICmdList,
		OutputRenderTargetResource->GetRenderTargetTexture(),
		FTextureRHIRef(),
		ESimpleRenderTargetMode::EUninitializedColorAndDepth,
		FExclusiveDepthStencil::DepthNop_StencilNop);*/
	//RHICmdList.BeginRenderPass(FRHIRenderPassInfo(OutputRenderTargetResource->GetRenderTargetTexture(), EDepthStencilTargetActions::ClearDepthStencil_DontStoreDepthStencil, nullptr, FExclusiveDepthStencil::DepthNop_StencilNop), TEXT("hello"));
	FIntPoint DrawTargetResolution(OutputRenderTargetResource->GetSizeX(), OutputRenderTargetResource->GetSizeY());  
    RHICmdList.SetViewport(0, 0, 0.0f, 500, 500, 1.0f);    //�����ӿڴ�С

	FRHIRenderPassInfo RPInfo(OutputRenderTargetResource->GetRenderTargetTexture(), ERenderTargetActions::DontLoad_Store, OutputRenderTargetResource->TextureRHI);
	RHICmdList.BeginRenderPass(RPInfo, TEXT("UniformBufferShader"));

	TShaderMap<FGlobalShaderType>* GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);
	TShaderMapRef<FUniformBufferShaderVS> VertexShader(GlobalShaderMap);
	TShaderMapRef<FUniformBufferShaderPS> PixelShader(GlobalShaderMap);        //��ȡ�Զ����Shader

	FCommonVertexDeclaration VertexDeclaration;   
	VertexDeclaration.InitRHI(); //�����������벼��

	FGraphicsPipelineStateInitializer GraphicPSPoint;
	RHICmdList.ApplyCachedRenderTargets(GraphicPSPoint);
	GraphicPSPoint.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	GraphicPSPoint.BlendState = TStaticBlendState<>::GetRHI();
	GraphicPSPoint.RasterizerState = TStaticRasterizerState<>::GetRHI();
	GraphicPSPoint.PrimitiveType = PT_TriangleList;        //���Ƶ�ͼԪ����
	GraphicPSPoint.BoundShaderState.VertexDeclarationRHI = VertexDeclaration.VertexDeclarationRHI;
	GraphicPSPoint.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
	GraphicPSPoint.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
	SetGraphicsPipelineState(RHICmdList, GraphicPSPoint);

	PixelShader->SetParameters(RHICmdList, MyColor, MyTexture, MyData);    //����Shader����������һ����ɫ������һ���������

	FUVertexInput Vertices[4];
	Vertices[0].Position.Set(-1.0f, 1.0f, 0, 1.0f);
	Vertices[1].Position.Set(1.0f, 1.0f, 0, 1.0f);
	Vertices[2].Position.Set(-1.0f, -1.0f, 0, 1.0f);
	Vertices[3].Position.Set(1.0f, -1.0f, 0, 1.0f);
	Vertices[0].UV = FVector2D(0.0f, 1.0f);
	Vertices[1].UV = FVector2D(1.0f, 1.0f);
	Vertices[2].UV = FVector2D(0.0f, 0.0f);
	Vertices[3].UV = FVector2D(1.0f, 0.0f);
    static const uint16 Indices[6] =  
    {  
        0, 1, 2,  
        2, 1, 3  
    };  

	DrawIndexedPrimitiveUP_cpy3(RHICmdList, PT_TriangleList, 0, ARRAY_COUNT(Vertices), 2, Indices, sizeof(Indices[0]), Vertices, sizeof(Vertices[0]));
	//RHICmdList.CopyToResolveTarget(OutputRenderTargetResource->GetRenderTargetTexture(), OutputRenderTargetResource->TextureRHI, FResolveParams());

	RHICmdList.EndRenderPass();
}


UUniformShaderBlueprintLibrary::UUniformShaderBlueprintLibrary(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UUniformShaderBlueprintLibrary::DrawUniformBufferShaderRenderTarget(class UTextureRenderTarget* OutputRenderTarget, AActor* Ac, FLinearColor MyColor, class UTexture* MyTexture, FUniformBufferData MyData)
{
	check(IsInGameThread());

	if (!OutputRenderTarget)return;

	FTextureRenderTargetResource* TextureRenderTargetResource = OutputRenderTarget->GameThread_GetRenderTargetResource();
	FTextureRHIParamRef MyTextureRHI = MyTexture->TextureReference.TextureReferenceRHI;
	UWorld* World = Ac->GetWorld();
	ERHIFeatureLevel::Type FeatureLevel = World->Scene->GetFeatureLevel();
	FName TextureRenderTargetName = OutputRenderTarget->GetFName();
	//����Ⱦ���뵽��Ⱦ�߳�
	ENQUEUE_RENDER_COMMAND(CaptureCommand)([TextureRenderTargetResource, FeatureLevel, MyColor, TextureRenderTargetName, MyTextureRHI, MyData](FRHICommandListImmediate& RHICmdList)
	{
		DrawUniformBufferShaderRenderTarget_RenderThread(RHICmdList, TextureRenderTargetResource, FeatureLevel, TextureRenderTargetName, MyColor, MyTextureRHI, MyData);
	});
}
