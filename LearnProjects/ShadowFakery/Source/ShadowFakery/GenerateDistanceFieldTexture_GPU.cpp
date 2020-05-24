// Fill out your copyright notice in the Description page of Project Settings.


#include "GenerateDistanceFieldTexture_GPU.h"
#include "Engine/StaticMesh.h"
#include "Rendering/PositionVertexBuffer.h"
#include "RenderTargetPool.h"
#include "RendererInterface.h"
#include "Templates/RefCounting.h"
#include "Math/OrthoMatrix.h"

FGenerateDistanceFieldTexture_GPU::FGenerateDistanceFieldTexture_GPU()
{
}

FGenerateDistanceFieldTexture_GPU::~FGenerateDistanceFieldTexture_GPU()
{
}

struct FDrawMaskInstance
{
	FDrawMaskInstance(FMatrix&& InWorld, FVector4&& InColor):
		World(InWorld),Color(InColor)
	{}

	FMatrix World;
	FVector4 Color;
};

class FOnlyPosVertexDeclaration : public FRenderResource
{
public:
	FVertexDeclarationRHIRef VertexDeclarationRHI;

	virtual void InitRHI()override
	{
		FVertexDeclarationElementList Elements;
		uint32 Stride = sizeof(FVector);
		
		Elements.Add(FVertexElement(0, 0, EVertexElementType::VET_Float3, 0, Stride));
		uint32 Offset = 0;
		Stride = sizeof(FDrawMaskInstance);
		//For instance world matrix
		Elements.Add(FVertexElement(1, Offset, EVertexElementType::VET_Float4, 1, Stride, true));
		Offset += sizeof(FVector4);
		Elements.Add(FVertexElement(1, Offset, EVertexElementType::VET_Float4, 2, Stride, true));
		Offset += sizeof(FVector4);
		Elements.Add(FVertexElement(1, Offset, EVertexElementType::VET_Float4, 3, Stride, true));
		Offset += sizeof(FVector4);
		Elements.Add(FVertexElement(1, Offset, EVertexElementType::VET_Float4, 4, Stride, true));
		Offset += sizeof(FVector4);

		Elements.Add(FVertexElement(1, Offset, EVertexElementType::VET_Float4, 5, Stride, true));    //For instance color
		VertexDeclarationRHI = RHICreateVertexDeclaration(Elements);
	}

	virtual void ReleaseRHI()override
	{
		VertexDeclarationRHI.SafeRelease();
	}
};

class FGenerateMeshMaskShaderVS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FGenerateMeshMaskShaderVS, Global)

public:
	FGenerateMeshMaskShaderVS() {};

	FGenerateMeshMaskShaderVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		ViewProjMatrix.Bind(Initializer.ParameterMap, TEXT("ViewProjMatrix"));
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
	}

	void SetParameters(
		FRHICommandListImmediate& RHICmdList,
		FMatrix& ViewProjMat
	)
	{
		SetShaderValue(RHICmdList, GetVertexShader(), ViewProjMatrix, ViewProjMat);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);

		Ar << ViewProjMatrix;

		return bShaderHasOutdatedParameters;
	}

private:
	FShaderParameter ViewProjMatrix;
};

class FGenerateMeshMaskShaderPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FGenerateMeshMaskShaderVS, Global)

public:
	FGenerateMeshMaskShaderPS() {};

	FGenerateMeshMaskShaderPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		ViewProjMatrix.Bind(Initializer.ParameterMap, TEXT("ViewProjMatrix"));
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
	}

	void SetParameters(
		FRHICommandListImmediate& RHICmdList,
		FMatrix& ViewProjMat
	)
	{
		SetShaderValue(RHICmdList, GetVertexShader(), ViewProjMatrix, ViewProjMat);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);

		Ar << ViewProjMatrix;

		return bShaderHasOutdatedParameters;
	}

private:
	FShaderParameter ViewProjMatrix;
};

IMPLEMENT_SHADER_TYPE(, FGenerateMeshMaskShaderVS, TEXT("/Shaders/Private/ShadowFakery.usf"), TEXT("GenerateMeshMaskShaderVS"), SF_Vertex)
IMPLEMENT_SHADER_TYPE(, FGenerateMeshMaskShaderPS, TEXT("/Shaders/Private/ShadowFakery.usf"), TEXT("GenerateMeshMaskShaderPS"), SF_Pixel)

void GenerateMeshMaskTexture(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, class UStaticMesh* StaticMesh, float StartDegree, uint32 TextureSize)
{
	check(IsInRenderingThread());
	
	if (!StaticMesh)return;

	FStaticMeshLODResources& LODModel = StaticMesh->RenderData->LODResources[0];
	FBoxSphereBounds& Bounds = StaticMesh->RenderData->Bounds;
	FPositionVertexBuffer& PositionVertexBuffer = LODModel.VertexBuffers.PositionVertexBuffer;
	FRawStaticIndexBuffer& Indices = LODModel.IndexBuffer;

	FVertexBufferRHIRef VertexBuffer = PositionVertexBuffer.CreateRHIBuffer_RenderThread();
	FIndexBufferRHIRef IndexBuffer = Indices.CreateRHIBuffer_RenderThread();

	TArray<FDrawMaskInstance> ModelInstance;
	//ModelInstance.SetNum(4);
	ModelInstance.Emplace(FMatrix::Identity, FVector4(0.f, 0.f, 0.f, 1.f));
	float BoundSize;
	FOrthoMatrix OrthoProjMatrix(BoundSize, BoundSize, 1.f, 0.f);
	FMatrix ViewRotationMatrix = FInverseRotationMatrix(FRotator(0.f, 180.f, 0.f));
	ViewRotationMatrix = ViewRotationMatrix * FMatrix(
		FPlane(0, 0, 1, 0),
		FPlane(1, 0, 0, 0),
		FPlane(0, 1, 0, 0),
		FPlane(0, 0, 0, 1));

	FVector ViewOrigin(10000.f, 0.f, 0.f);
	const FMatrix ViewMatrix = FTranslationMatrix(-ViewOrigin) * ViewRotationMatrix;
	const FMatrix ViewProjMatrix = ViewMatrix * OrthoProjMatrix;

	FRHIResourceCreateInfo CreateInfo;
	FVertexBufferRHIRef MeshModelInstancedVB = RHICreateVertexBuffer(ModelInstance.Num()* sizeof(FDrawMaskInstance), BUF_Static | BUF_ShaderResource, CreateInfo);
	void* MeshModelInstancedData = RHILockVertexBuffer(MeshModelInstancedVB, 0, ModelInstance.Num() * sizeof(FDrawMaskInstance), RLM_WriteOnly);
	FPlatformMemory::Memcpy(MeshModelInstancedData, ModelInstance.GetData(), ModelInstance.Num() * sizeof(FDrawMaskInstance));
	RHIUnlockVertexBuffer(MeshModelInstancedVB);
	
	TShaderMapRef<FGenerateMeshMaskShaderVS> VertexShader(GetGlobalShaderMap(FeatureLevel));
	TShaderMapRef<FGenerateMeshMaskShaderPS> PixelShader(GetGlobalShaderMap(FeatureLevel));

	RHICmdList.SetViewport(0.f, 0.f, 0.f, TextureSize, TextureSize, 1.f);
	
	TRefCountPtr<IPooledRenderTarget> MaskRT;
	FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(FIntPoint(TextureSize, TextureSize), PF_A32B32G32R32F, FClearValueBinding::Transparent, TexCreate_ShaderResource, TexCreate_RenderTargetable, false));
	GRenderTargetPool.FindFreeElement(RHICmdList, Desc, MaskRT, TEXT("MaskRT"));
	
	FRHITexture* ColorRTs[1] = { MaskRT->GetRenderTargetItem().TargetableTexture };
	FRHIRenderPassInfo PassInfo(1, ColorRTs, ERenderTargetActions::Load_Store);
	RHICmdList.BeginRenderPass(PassInfo, TEXT("GenerateMeshMask"));

	FOnlyPosVertexDeclaration VertexDeclaration;
	VertexDeclaration.InitRHI();

	FGraphicsPipelineStateInitializer GraphicPSPoint;
	RHICmdList.ApplyCachedRenderTargets(GraphicPSPoint);
	GraphicPSPoint.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	GraphicPSPoint.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One>::GetRHI();
	GraphicPSPoint.RasterizerState = TStaticRasterizerState<>::GetRHI();
	GraphicPSPoint.PrimitiveType = PT_TriangleList;
	GraphicPSPoint.BoundShaderState.VertexDeclarationRHI = VertexDeclaration.VertexDeclarationRHI;
	GraphicPSPoint.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
	GraphicPSPoint.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
	SetGraphicsPipelineState(RHICmdList, GraphicPSPoint);

	RHICmdList.SetStreamSource(0, VertexBuffer, 0);
	RHICmdList.SetStreamSource(1, MeshModelInstancedVB, 0);
	RHICmdList.DrawIndexedPrimitive(IndexBuffer, 0, 0, Indices.GetNumIndices(), 0, Indices.GetNumIndices() / 3, 4);

	VertexBuffer.SafeRelease();
	IndexBuffer.SafeRelease();
	MeshModelInstancedVB.SafeRelease();

	RHICmdList.EndRenderPass();
}
