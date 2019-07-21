#include "InterationShader.h"
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
#include "BlurComputeShader.h"
#include "RHICommandList.h"


struct FVertexInput
{
	FVector4 Position;
	FVector2D UV;
};

class FInterationDeclaration : public FRenderResource
{
public:
	FVertexDeclarationRHIRef VertexDeclarationRHI;

	virtual void InitRHI()override
	{
		//���ö������벼��
		FVertexDeclarationElementList Elements;
		uint32 Stride = sizeof(FVertexInput);   //����
		Elements.Add(FVertexElement(0, STRUCT_OFFSET(FVertexInput, Position), EVertexElementType::VET_Float4, 0, Stride));  //��һ������StreamӦ�������Instance��ʹ��
		Elements.Add(FVertexElement(0, STRUCT_OFFSET(FVertexInput, UV), EVertexElementType::VET_Float2, 1, Stride));
		VertexDeclarationRHI = RHICreateVertexDeclaration(Elements);
	}

	virtual void ReleaseRHI()override
	{
		VertexDeclarationRHI.SafeRelease();
	}
};

class FInterationShader : public FGlobalShader
{
public:
	FInterationShader() {}

	FInterationShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		SimpleColorVal.Bind(Initializer.ParameterMap, TEXT("SimpleColor"));
		TestTextureVal.Bind(Initializer.ParameterMap, TEXT("InterationTexture"));
		TestTextureSampler.Bind(Initializer.ParameterMap, TEXT("InterationTextureSampler"));
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
		OutEnvironment.SetDefine(TEXT("TEST_MICRO"), 1);
	}

	void SetParameters(FRHICommandListImmediate& RHICmdList, const FLinearColor& MyColor, FTextureRHIParamRef& MyTexture)
	{
		SetShaderValue(RHICmdList, GetPixelShader(), SimpleColorVal, MyColor);
		 
		//������Ϊ��ɫ�������������
		SetTextureParameter(RHICmdList, GetPixelShader(), TestTextureVal, TestTextureSampler, 
				TStaticSamplerState<SF_AnisotropicLinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(),     //����������ã������Թ��ˣ�U��V,W����ά�ȶ���Clamp
				MyTexture);
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

class FInterationShaderVS :public FInterationShader
{
	DECLARE_SHADER_TYPE(FInterationShaderVS, Global);

public:
	FInterationShaderVS() {}

	FInterationShaderVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FInterationShader(Initializer)
	{
	}
};

class FInterationShaderPS :public FInterationShader
{
	DECLARE_SHADER_TYPE(FInterationShaderPS, Global);

public:
	FInterationShaderPS() {}

	FInterationShaderPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FInterationShader(Initializer)
	{
	}
};

IMPLEMENT_SHADER_TYPE(, FInterationShaderVS, TEXT("/Plugins/Shaders/Private/InterationShader.usf"),TEXT("MainVS"), SF_Vertex)
IMPLEMENT_SHADER_TYPE(, FInterationShaderPS, TEXT("/Plugins/Shaders/Private/InterationShader.usf"),TEXT("MainPS"), SF_Pixel)


static void DrawIndexedPrimitiveUP_cpy2(
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

static void DrawInterationShaderRenderTarget_RenderThread(
	FRHICommandListImmediate& RHICmdList,
	FTextureRenderTargetResource* OutputRenderTargetResource,
	ERHIFeatureLevel::Type FeatureLevel,
	FName TextureRenderTargetName,
	FLinearColor MyColor,
	FTextureRHIParamRef MyTexture  
)
{
	check(IsInRenderingThread())

#if WANTS_DRAW_MESH_EVENTS
	FString EventName;
	TextureRenderTargetName.ToString(EventName);
	SCOPED_DRAW_EVENTF(RHICmdList, SceneCapture, TEXT("HelloShader%s"), *EventName);
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
	RHICmdList.BeginRenderPass(RPInfo, TEXT("InterationShader"));

	TShaderMap<FGlobalShaderType>* GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);
	TShaderMapRef<FInterationShaderVS> VertexShader(GlobalShaderMap);
	TShaderMapRef<FInterationShaderPS> PixelShader(GlobalShaderMap);        //��ȡ�Զ����Shader

	FInterationDeclaration VertexDeclaration;   
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

	PixelShader->SetParameters(RHICmdList, MyColor, MyTexture);    //����Shader����������һ����ɫ������һ���������

	FVertexInput Vertices[4];
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

	DrawIndexedPrimitiveUP_cpy2(RHICmdList, PT_TriangleList, 0, ARRAY_COUNT(Vertices), 2, Indices, sizeof(Indices[0]), Vertices, sizeof(Vertices[0]));
	//RHICmdList.CopyToResolveTarget(OutputRenderTargetResource->GetRenderTargetTexture(), OutputRenderTargetResource->TextureRHI, FResolveParams());

	RHICmdList.EndRenderPass();
}


UInterationShaderBlueprintLibrary::UInterationShaderBlueprintLibrary(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UInterationShaderBlueprintLibrary::DrawInterationShaderRenderTarget(class UTextureRenderTarget* OutputRenderTarget, AActor* Ac, FLinearColor MyColor, class UTexture* MyTexture)
{
	check(IsInGameThread());

	if (!OutputRenderTarget)return;

	FTextureRenderTargetResource* TextureRenderTargetResource = OutputRenderTarget->GameThread_GetRenderTargetResource();
	FTextureRHIParamRef MyTextureRHI = MyTexture->TextureReference.TextureReferenceRHI;
	UWorld* World = Ac->GetWorld();
	ERHIFeatureLevel::Type FeatureLevel = World->Scene->GetFeatureLevel();
	FName TextureRenderTargetName = OutputRenderTarget->GetFName();
	//����Ⱦ���뵽��Ⱦ�߳�
	ENQUEUE_RENDER_COMMAND(CaptureCommand)([TextureRenderTargetResource, FeatureLevel, MyColor, TextureRenderTargetName, MyTextureRHI](FRHICommandListImmediate& RHICmdList)
	{
		DrawInterationShaderRenderTarget_RenderThread(RHICmdList, TextureRenderTargetResource, FeatureLevel, TextureRenderTargetName, MyColor, MyTextureRHI);
	});
}

void UInterationShaderBlueprintLibrary::DrawInterationShaderRenderTarget_Blur(class UTextureRenderTarget* OutputRenderTarget, AActor* Ac, FLinearColor MyColor, class UTexture* MyTexture)
{
	check(IsInGameThread());

	if (!OutputRenderTarget)return;

	FTextureRenderTargetResource* TextureRenderTargetResource = OutputRenderTarget->GameThread_GetRenderTargetResource();
	FTextureRHIParamRef MyTextureRHI = MyTexture->TextureReference.TextureReferenceRHI;
	UWorld* World = Ac->GetWorld();
	ERHIFeatureLevel::Type FeatureLevel = World->Scene->GetFeatureLevel();
	FName TextureRenderTargetName = OutputRenderTarget->GetFName();
	//����Ⱦ���뵽��Ⱦ�߳�
	ENQUEUE_RENDER_COMMAND(CaptureCommand)([TextureRenderTargetResource, FeatureLevel, MyColor, TextureRenderTargetName, MyTextureRHI](FRHICommandListImmediate& RHICmdList)
	{
		DrawInterationShaderRenderTarget_RenderThread(RHICmdList, TextureRenderTargetResource, FeatureLevel, TextureRenderTargetName, MyColor, MyTextureRHI);
	});

		//����UAVͼ
	FRHIResourceCreateInfo RHIResourceCreateInfo;
	FTextureRHIParamRef Texture = RHICreateTexture2D(MyTextureRHI->GetTexture2D()->GetSizeX(), MyTextureRHI->GetTexture2D()->GetSizeY(), PF_A32B32G32R32F, 1, 1, TexCreate_ShaderResource | TexCreate_UAV, RHIResourceCreateInfo);
	FUnorderedAccessViewRHIParamRef TextureUAV = RHICreateUnorderedAccessView(Texture);

	DrawBlurComputeShaderRenderTarget(Ac, MyTextureRHI, TextureUAV);
}

