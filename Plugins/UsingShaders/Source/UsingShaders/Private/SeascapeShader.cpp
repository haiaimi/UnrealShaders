#include "SeascapeShader.h"
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

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FSeascapeShaderData, )
SHADER_PARAMETER(FVector2D, ViewResolution)
SHADER_PARAMETER(float, TimeSeconds)
SHADER_PARAMETER(FVector2D, MousePos)
END_GLOBAL_SHADER_PARAMETER_STRUCT()


struct FSVertexInput
{
	FVector4 Position;
	FVector2D UV;
};

class FSeascapeVertexDeclaration : public FRenderResource
{
public:
	FVertexDeclarationRHIRef VertexDeclarationRHI;

	virtual void InitRHI()override
	{
		//���ö������벼��
		FVertexDeclarationElementList Elements;
		uint32 Stride = sizeof(FSVertexInput);   //����
		Elements.Add(FVertexElement(0, STRUCT_OFFSET(FSVertexInput, Position), EVertexElementType::VET_Float4, 0, Stride));  //��һ������StreamӦ�������Instance��ʹ��
		Elements.Add(FVertexElement(0, STRUCT_OFFSET(FSVertexInput, UV), EVertexElementType::VET_Float2, 1, Stride));
		VertexDeclarationRHI = RHICreateVertexDeclaration(Elements);
	}

	virtual void ReleaseRHI()override
	{
		VertexDeclarationRHI.SafeRelease();
	}
};

class FSeascapeShader : public FGlobalShader
{
public:
	FSeascapeShader() {}

	FSeascapeShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{

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
		OutEnvironment.SetDefine(TEXT("SEASCAPE_MICRO"), 1);
	}

	void SetParameters(FRHICommandListImmediate& RHICmdList, FSeascapeBufferData& MyData)
	{
		FSeascapeShaderData SShaderData;
		SShaderData.ViewResolution = MyData.ViewResolution;
		SShaderData.TimeSeconds = MyData.TimeSeconds;
		SShaderData.MousePos = MyData.MousePos;

		SetUniformBufferParameter(RHICmdList, GetPixelShader(), GetUniformBufferParameter<FSeascapeShaderData>(), FSeascapeShaderData::CreateUniformBuffer(SShaderData, EUniformBufferUsage::UniformBuffer_SingleDraw));
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		return bShaderHasOutdatedParameters;
	}

private:
};

template<int32 Index>
class FSeascapeShaderVS :public FSeascapeShader
{
	DECLARE_SHADER_TYPE(FSeascapeShaderVS, Global);

public:
	FSeascapeShaderVS() {}

	FSeascapeShaderVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FSeascapeShader(Initializer)
	{
	}
};

template<int32 Index>
class FSeascapeShaderPS :public FSeascapeShader
{
	DECLARE_SHADER_TYPE(FSeascapeShaderPS, Global);

public:
	FSeascapeShaderPS() {}

	FSeascapeShaderPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FSeascapeShader(Initializer)
	{
	}
};

 //ΪShader����UniformBuffer������Ҫ��Shader�������������������Щ����д�뵽Common.ush��shader�ļ���
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FSeascapeShaderData, "FSeascapeData");    


IMPLEMENT_SHADER_TYPE(, FSeascapeShaderVS<1>, TEXT("/Plugins/Shaders/Private/SeascapeShader.usf"),TEXT("MainVS"), SF_Vertex)
IMPLEMENT_SHADER_TYPE(, FSeascapeShaderPS<1>, TEXT("/Plugins/Shaders/Private/SeascapeShader.usf"),TEXT("MainPS"), SF_Pixel)

IMPLEMENT_SHADER_TYPE(, FSeascapeShaderVS<2>, TEXT("/Plugins/Shaders/Private/ProteanCloud.usf"), TEXT("MainVS"), SF_Vertex)
IMPLEMENT_SHADER_TYPE(, FSeascapeShaderPS<2>, TEXT("/Plugins/Shaders/Private/ProteanCloud.usf"), TEXT("MainPS"), SF_Pixel)


static void DrawIndexedPrimitiveUP_cpy4(
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
	FSeascapeBufferData MyData
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

	FIntPoint DrawTargetResolution(OutputRenderTargetResource->GetSizeX(), OutputRenderTargetResource->GetSizeY());  
    RHICmdList.SetViewport(0, 0, 0.0f, DrawTargetResolution.X, DrawTargetResolution.Y, 1.0f);    //�����ӿڴ�С

	FRHIRenderPassInfo RPInfo(OutputRenderTargetResource->GetRenderTargetTexture(), ERenderTargetActions::Load_Store, OutputRenderTargetResource->TextureRHI);
	RHICmdList.BeginRenderPass(RPInfo, TEXT("SeascapeShader"));

	TShaderMap<FGlobalShaderType>* GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);
	TShaderMapRef<FSeascapeShaderVS<2>> VertexShader(GlobalShaderMap);
	TShaderMapRef<FSeascapeShaderPS<2>> PixelShader(GlobalShaderMap);        //��ȡ�Զ����Shader

	FSeascapeVertexDeclaration VertexDeclaration;   
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

	PixelShader->SetParameters(RHICmdList, MyData);    //����Shader����������һ����ɫ������һ���������

	FSVertexInput Vertices[4];
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

	DrawIndexedPrimitiveUP_cpy4(RHICmdList, PT_TriangleList, 0, ARRAY_COUNT(Vertices), 2, Indices, sizeof(Indices[0]), Vertices, sizeof(Vertices[0]));
	RHICmdList.CopyToResolveTarget(OutputRenderTargetResource->GetRenderTargetTexture(), OutputRenderTargetResource->TextureRHI, FResolveParams());

	RHICmdList.EndRenderPass();
}

USeascapeShaderBlueprintLibrary::USeascapeShaderBlueprintLibrary(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void USeascapeShaderBlueprintLibrary::DrawSeascapeShaderRenderTarget(class UTextureRenderTarget* OutputRenderTarget, AActor* MyActor, FSeascapeBufferData ShaderStructData)
{
	check(IsInGameThread());

	if (!OutputRenderTarget)return;

	FTextureRenderTargetResource* TextureRenderTargetResource = OutputRenderTarget->GameThread_GetRenderTargetResource();
	UWorld* World = MyActor->GetWorld();
	ERHIFeatureLevel::Type FeatureLevel = World->Scene->GetFeatureLevel();
	FName TextureRenderTargetName = OutputRenderTarget->GetFName();
	//����Ⱦ���뵽��Ⱦ�߳�
	ENQUEUE_RENDER_COMMAND(CaptureCommand)([TextureRenderTargetResource, FeatureLevel, TextureRenderTargetName, ShaderStructData](FRHICommandListImmediate& RHICmdList)
	{
		DrawUniformBufferShaderRenderTarget_RenderThread(RHICmdList, TextureRenderTargetResource, FeatureLevel, TextureRenderTargetName, ShaderStructData);
	});
}
