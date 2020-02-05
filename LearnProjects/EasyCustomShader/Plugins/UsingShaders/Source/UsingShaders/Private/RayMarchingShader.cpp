#include "RayMarchingShader.h"
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

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FRayMarchingShaderData, )
SHADER_PARAMETER(FVector2D, ViewResolution)
SHADER_PARAMETER(float, TimeSeconds)
SHADER_PARAMETER(FVector2D, MousePos)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

struct FSVertexInput
{
	FVector4 Position;
	FVector2D UV;
};

class FRayMarchingVertexDeclaration : public FRenderResource
{
public:
	FVertexDeclarationRHIRef VertexDeclarationRHI;

	virtual void InitRHI()override
	{
		//设置顶点输入布局
		FVertexDeclarationElementList Elements;
		uint32 Stride = sizeof(FSVertexInput);   //步长
		Elements.Add(FVertexElement(0, STRUCT_OFFSET(FSVertexInput, Position), EVertexElementType::VET_Float4, 0, Stride));  //第一个参数Stream应该是配合Instance来使用
		Elements.Add(FVertexElement(0, STRUCT_OFFSET(FSVertexInput, UV), EVertexElementType::VET_Float2, 1, Stride));
		VertexDeclarationRHI = RHICreateVertexDeclaration(Elements);
	}

	virtual void ReleaseRHI()override
	{
		VertexDeclarationRHI.SafeRelease();
	}
};

class FRayMarchingShader : public FGlobalShader
{
public:
	FRayMarchingShader() {}

	FRayMarchingShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
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
		OutEnvironment.SetDefine(TEXT("RayMarching_MICRO"), 1);
	}

	void SetParameters(FRHICommandListImmediate& RHICmdList, FRayMarchingBufferData& MyData)
	{
		FRayMarchingShaderData SShaderData;
		SShaderData.ViewResolution = MyData.ViewResolution;
		SShaderData.TimeSeconds = MyData.TimeSeconds;
		SShaderData.MousePos = MyData.MousePos;

		SetUniformBufferParameter(RHICmdList, GetPixelShader(), GetUniformBufferParameter<FRayMarchingShaderData>(), FRayMarchingShaderData::CreateUniformBuffer(SShaderData, EUniformBufferUsage::UniformBuffer_SingleDraw));
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		return bShaderHasOutdatedParameters;
	}

private:
};

template<int32 Index>
class FRayMarchingShaderVS :public FRayMarchingShader
{
	DECLARE_SHADER_TYPE(FRayMarchingShaderVS, Global);

public:
	FRayMarchingShaderVS() {}

	FRayMarchingShaderVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FRayMarchingShader(Initializer)
	{
	}
};

template<int32 Index>
class FRayMarchingShaderPS :public FRayMarchingShader
{
	DECLARE_SHADER_TYPE(FRayMarchingShaderPS, Global);

public:
	FRayMarchingShaderPS() {}

	FRayMarchingShaderPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FRayMarchingShader(Initializer)
	{
	}
};

 //为Shader声明UniformBuffer，不需要在Shader中声明，该声明会把这些定义写入到Common.ush的shader文件中，所以Shader文件中要包含Common文件
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FRayMarchingShaderData, "FRayMarchingData");    


IMPLEMENT_SHADER_TYPE(, FRayMarchingShaderVS<1>, TEXT("/Plugins/Shaders/Private/SeascapeShader.usf"),TEXT("MainVS"), SF_Vertex)
IMPLEMENT_SHADER_TYPE(, FRayMarchingShaderPS<1>, TEXT("/Plugins/Shaders/Private/SeascapeShader.usf"),TEXT("MainPS"), SF_Pixel)

IMPLEMENT_SHADER_TYPE(, FRayMarchingShaderVS<2>, TEXT("/Plugins/Shaders/Private/ProteanCloud.usf"), TEXT("MainVS"), SF_Vertex)
IMPLEMENT_SHADER_TYPE(, FRayMarchingShaderPS<2>, TEXT("/Plugins/Shaders/Private/ProteanCloud.usf"), TEXT("MainPS"), SF_Pixel)


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
	FVertexBufferRHIRef VertexBufferRHI = RHICreateVertexBuffer(VertexDataStride * NumVertices, BUF_Volatile, CreateInfo);        //Buf类型是改动类型，没帧写入，所以声明为BUF_Volatile
	void* VoidPtr = RHILockVertexBuffer(VertexBufferRHI, 0, VertexDataStride * NumVertices, RLM_WriteOnly);        //lock顶点缓冲buffer，并且只能写入
	FPlatformMemory::Memcpy(VoidPtr, VertexData, VertexDataStride * NumVertices);       //传入顶点数据
	RHIUnlockVertexBuffer(VertexBufferRHI);    //unlock顶点缓冲

	//同上创建一个索引缓冲
	FIndexBufferRHIRef IndexBufferRHI = RHICreateIndexBuffer(IndexDataStride, IndexDataStride * NumIndices, BUF_Volatile, CreateInfo);
	void* VoidPtr2 = RHILockIndexBuffer(IndexBufferRHI, 0, IndexDataStride * NumIndices, RLM_WriteOnly);
	FPlatformMemory::Memcpy(VoidPtr2, IndexData, IndexDataStride * NumIndices);
	RHIUnlockIndexBuffer(IndexBufferRHI);

	RHICmdList.SetStreamSource(0, VertexBufferRHI, 0);     //针对不同的图形接口设置顶点缓冲
	RHICmdList.DrawIndexedPrimitive(IndexBufferRHI, MinVertexIndex, 0, NumVertices, 0, NumPrimitives, 1);        //绘制图元

	IndexBufferRHI.SafeRelease();
	VertexBufferRHI.SafeRelease();
}

static void DrawUniformBufferShaderRenderTarget_RenderThread(
	FRHICommandListImmediate& RHICmdList,
	ERayMarchingShader ShaderType,
	FTextureRenderTargetResource* OutputRenderTargetResource,
	ERHIFeatureLevel::Type FeatureLevel,
	FName TextureRenderTargetName,
	FRayMarchingBufferData MyData
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

	TShaderMap<FGlobalShaderType>* GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);
	//TShaderMapRef<FRayMarchingShaderVS<1>> VertexShader(GlobalShaderMap);
	//TShaderMapRef<FRayMarchingShaderPS<1>> PixelShader(GlobalShaderMap);        //获取自定义的Shader

	FRayMarchingShader* VertexShader = nullptr;
	FRayMarchingShader* PixelShader = nullptr;

	switch (ShaderType)
	{
	case ERayMarchingShader::None:
		//Do nothing, sometime we don't need run it
		return;
		break;
	case ERayMarchingShader::Seascape:
		VertexShader = *TShaderMapRef<FRayMarchingShaderVS<1>>(GlobalShaderMap);
		PixelShader = *TShaderMapRef<FRayMarchingShaderPS<1>>(GlobalShaderMap);
		break;
	case ERayMarchingShader::ProteanCloud:
		VertexShader = *TShaderMapRef<FRayMarchingShaderVS<2>>(GlobalShaderMap);
		PixelShader = *TShaderMapRef<FRayMarchingShaderPS<2>>(GlobalShaderMap);
		break;
	}

	FIntPoint DrawTargetResolution(OutputRenderTargetResource->GetSizeX(), OutputRenderTargetResource->GetSizeY());  
    RHICmdList.SetViewport(0, 0, 0.0f, DrawTargetResolution.X, DrawTargetResolution.Y, 1.0f);    //设置视口大小

	FRHIRenderPassInfo RPInfo(OutputRenderTargetResource->GetRenderTargetTexture(), ERenderTargetActions::Load_Store, OutputRenderTargetResource->TextureRHI);
	RHICmdList.BeginRenderPass(RPInfo, TEXT("RayMarchingShader"));

	FRayMarchingVertexDeclaration VertexDeclaration;   
	VertexDeclaration.InitRHI(); //创建定点输入布局

	FGraphicsPipelineStateInitializer GraphicPSPoint;
	RHICmdList.ApplyCachedRenderTargets(GraphicPSPoint);
	GraphicPSPoint.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	GraphicPSPoint.BlendState = TStaticBlendState<>::GetRHI();
	GraphicPSPoint.RasterizerState = TStaticRasterizerState<>::GetRHI();
	GraphicPSPoint.PrimitiveType = PT_TriangleList;        //绘制的图元类型
	GraphicPSPoint.BoundShaderState.VertexDeclarationRHI = VertexDeclaration.VertexDeclarationRHI;
	GraphicPSPoint.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(VertexShader);
	GraphicPSPoint.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(PixelShader);
	SetGraphicsPipelineState(RHICmdList, GraphicPSPoint);

	PixelShader->SetParameters(RHICmdList, MyData);    //设置Shader参数，这里一个颜色参数和一个纹理参数

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

URayMarchingBlueprintLibrary::URayMarchingBlueprintLibrary(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void URayMarchingBlueprintLibrary::DrawRayMarchingRenderTarget(ERayMarchingShader ShaderType, class UTextureRenderTarget* OutputRenderTarget, AActor* MyActor, FRayMarchingBufferData ShaderStructData)
{
	check(IsInGameThread());

	if (!OutputRenderTarget)return;

	FTextureRenderTargetResource* TextureRenderTargetResource = OutputRenderTarget->GameThread_GetRenderTargetResource();
	UWorld* World = MyActor->GetWorld();
	ERHIFeatureLevel::Type FeatureLevel = World->Scene->GetFeatureLevel();
	FName TextureRenderTargetName = OutputRenderTarget->GetFName();
	//把渲染加入到渲染线程
	ENQUEUE_RENDER_COMMAND(CaptureCommand)([ShaderType, TextureRenderTargetResource, FeatureLevel, TextureRenderTargetName, ShaderStructData](FRHICommandListImmediate& RHICmdList)
	{
		DrawUniformBufferShaderRenderTarget_RenderThread(RHICmdList, ShaderType, TextureRenderTargetResource, FeatureLevel, TextureRenderTargetName, ShaderStructData);
	});
}
