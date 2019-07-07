// Fill out your copyright notice in the Description page of Project Settings.


#include "TestShader.h"
#include "GlobalShader.h"
#include "ShaderParameterUtils.h"
#include "TextureResource.h"
#include "RHIStaticStates.h"
#include "RenderUtils.h"
#include "PipelineStateCache.h"


UTestShaderBlueprintLibrary::UTestShaderBlueprintLibrary()
{

}

void UTestShaderBlueprintLibrary::DrawTestShaderRenderTarget(class UTextureRenderTarget* OutputRenderTarget, AActor* Ac, FLinearColor AcColor)
{

}

class FHelloShader : public FGlobalShader
{

public:
	FHelloShader() {}

	FHelloShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	{
		SimpleColorVal.Bind(Initializer.ParameterMap, TEXT("SimpleColor"));
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

	void SetParameters(FRHICommandListImmediate& RHICmdList, const FLinearColor& MyColor)
	{
		SetShaderValue(RHICmdList, GetPixelShader(), SimpleColorVal, MyColor);
	}

	virtual bool Serialize(FArchive& Ar)override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << SimpleColorVal;
		return bShaderHasOutdatedParameters;
	}

private:
	FShaderParameter SimpleColorVal;
};

class FHelloShaderVS : public FHelloShader
{
	DECLARE_SHADER_TYPE(FHelloShaderVS, Global);

public:
	FHelloShaderVS() {}

	FHelloShaderVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	{
	}
};

class FHelloShaderPS : public FHelloShader
{
	DECLARE_SHADER_TYPE(FHelloShaderPS, Global);

public:
	FHelloShaderPS() {}

	FHelloShaderPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	{
	}
};

IMPLEMENT_SHADER_TYPE(, FHelloShaderVS, TEXT("/Plugin/UsingShaders/Private/CustomShader.usf"), TEXT("MainVS"), SF_Vertex)
IMPLEMENT_SHADER_TYPE(, FHelloShaderPS, TEXT("/Plugin/UsingShaders/Private/CustomShader.usf"), TEXT("MainPS"), SF_Pixel)


static void DrawHelloShaderRenderTarget_RenderThread(
	FRHICommandListImmediate& RHICmdList,
	FTextureRenderTargetResource* OutputRenderTargetResource,
	ERHIFeatureLevel::Type FeatureLevel,
	FName TextureRenderTargetName,
	FLinearColor MyColor
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

	SetRenderTarget(RHICmdList,
		OutputRenderTargetResource->GetRenderTargetTexture(),
		FTextureRHIRef(),
		ESimpleRenderTargetMode::EUninitializedColorAndDepth,
		FExclusiveDepthStencil::DepthNop_StencilNop);

	TShaderMap<FGlobalShaderType>* GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);
	TShaderMapRef<FHelloShaderVS> VertexShader(GlobalShaderMap);
	TShaderMapRef<FHelloShaderPS> PixelShader(GlobalShaderMap);

	FGraphicsPipelineStateInitializer GraphicPSPoint;
	RHICmdList.ApplyCachedRenderTargets(GraphicPSPoint);
	GraphicPSPoint.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	GraphicPSPoint.BlendState = TStaticBlendState<>::GetRHI();
	GraphicPSPoint.RasterizerState = TStaticRasterizerState<>::GetRHI();
	GraphicPSPoint.PrimitiveType = PT_TriangleList;
	GraphicPSPoint.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector4();
	GraphicPSPoint.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
	GraphicPSPoint.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
	SetGraphicsPipelineState(RHICmdList, GraphicPSPoint);

	PixelShader->SetParameters(RHICmdList, MyColor);

	FVector4 Vertices[4];  
    Vertices[0].Set(-1.0f, 1.0f, 0, 1.0f);  
    Vertices[1].Set(1.0f, 1.0f, 0, 1.0f);  
    Vertices[2].Set(-1.0f, -1.0f, 0, 1.0f);  
    Vertices[3].Set(1.0f, -1.0f, 0, 1.0f);  
    static const uint16 Indices[6] =  
    {  
        0, 1, 2,  
        2, 1, 3  
    };  

	DrawIndexedPrimitiveUP(RHICmdList, PT_TriangleStrip, 0, ARRAY_COUNT(Vertices), 2, Indices, sizeof(Indices[0]), Vertices, sizeof(Vertices[0]));
	RHICmdList.CopyToResolveTarget(OutputRenderTargetResource->GetRenderTargetTexture(), OutputRenderTargetResource->TextureRHI, FResolveParams());
}