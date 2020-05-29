// Fill out your copyright notice in the Description page of Project Settings.


#include "GenerateDistanceFieldTexture_GPU.h"
#include "Engine/StaticMesh.h"
#include "Rendering/PositionVertexBuffer.h"
#include "RenderTargetPool.h"
#include "RendererInterface.h"
#include "Templates/RefCounting.h"
#include "Math/OrthoMatrix.h"
#include "RawIndexBuffer.h"
#include "GenerateMips.h"

static TAutoConsoleVariable<float> CVarMaskVolumeScale(
	TEXT("r.MaskVolumeScale"),
	1.05f,
	TEXT(""),
	ECVF_Default);

FGenerateDistanceFieldTexture_GPU::FGenerateDistanceFieldTexture_GPU()
{
}

FGenerateDistanceFieldTexture_GPU::~FGenerateDistanceFieldTexture_GPU()
{
}

void GenerateDistanceFieldTexture(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, uint32 TextureSize, FTextureRHIRef ResultRT, FTextureRHIRef MaskTexture, const FVector2D& DFDimension, uint32 CurLevel);

struct FDrawMaskInstance
{
	FDrawMaskInstance(const FMatrix& InWorld, const FVector4& InColor):
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

struct FVertexInput
{
	FVector4 Position;
	FVector2D UV;
};

class FCommonVertexDeclaration : public FRenderResource
{
public:
	FVertexDeclarationRHIRef VertexDeclarationRHI;

	virtual void InitRHI()override
	{
		FVertexDeclarationElementList Elements;
		uint32 Stride = sizeof(FVertexInput);
		Elements.Add(FVertexElement(0, STRUCT_OFFSET(FVertexInput, Position), EVertexElementType::VET_Float4, 0, Stride));
		Elements.Add(FVertexElement(0, STRUCT_OFFSET(FVertexInput, UV), EVertexElementType::VET_Float2, 1, Stride));
		VertexDeclarationRHI = RHICreateVertexDeclaration(Elements);
	}

	virtual void ReleaseRHI()override
	{
		VertexDeclarationRHI.SafeRelease();
	}
};

static void DrawIndexedPrimitiveUP_Custom(
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
	FVertexBufferRHIRef VertexBufferRHI = RHICreateVertexBuffer(VertexDataStride * NumVertices, BUF_Volatile, CreateInfo);
	void* VoidPtr = RHILockVertexBuffer(VertexBufferRHI, 0, VertexDataStride * NumVertices, RLM_WriteOnly);
	FPlatformMemory::Memcpy(VoidPtr, VertexData, VertexDataStride * NumVertices);
	RHIUnlockVertexBuffer(VertexBufferRHI);

	FIndexBufferRHIRef IndexBufferRHI = RHICreateIndexBuffer(IndexDataStride, IndexDataStride * NumIndices, BUF_Volatile, CreateInfo);
	void* VoidPtr2 = RHILockIndexBuffer(IndexBufferRHI, 0, IndexDataStride * NumIndices, RLM_WriteOnly);
	FPlatformMemory::Memcpy(VoidPtr2, IndexData, IndexDataStride * NumIndices);
	RHIUnlockIndexBuffer(IndexBufferRHI);

	RHICmdList.SetStreamSource(0, VertexBufferRHI, 0);
	RHICmdList.DrawIndexedPrimitive(IndexBufferRHI, MinVertexIndex, 0, NumVertices, 0, NumPrimitives, 1);

	IndexBufferRHI.SafeRelease();
	VertexBufferRHI.SafeRelease();
}

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
		FRHICommandList& RHICmdList,
		const FMatrix& ViewProjMat
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

IMPLEMENT_SHADER_TYPE(, FGenerateMeshMaskShaderVS, TEXT("/Shaders/ShadowFakery.usf"), TEXT("GenerateMeshMaskShaderVS"), SF_Vertex)
IMPLEMENT_SHADER_TYPE(, FGenerateMeshMaskShaderPS, TEXT("/Shaders/ShadowFakery.usf"), TEXT("GenerateMeshMaskShaderPS"), SF_Pixel)

class FGenerateDistanceFieldShaderVS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FGenerateDistanceFieldShaderVS, Global)

public:
	FGenerateDistanceFieldShaderVS() {};

	FGenerateDistanceFieldShaderVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
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
	}

	void SetParameters(
		FRHICommandList& RHICmdList
	)
	{
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		return bShaderHasOutdatedParameters;
	}

private:
};

class FGenerateDistanceFieldShaderPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FGenerateDistanceFieldShaderPS, Global)

public:
	FGenerateDistanceFieldShaderPS() {};

	FGenerateDistanceFieldShaderPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		CurLevel.Bind(Initializer.ParameterMap, TEXT("CurLevel"));
		DistanceFieldDimension.Bind(Initializer.ParameterMap, TEXT("DistanceFieldDimension"));
		MaskTexture.Bind(Initializer.ParameterMap, TEXT("MaskTexture"));
		TextureSampler.Bind(Initializer.ParameterMap, TEXT("TextureSampler"));
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
		FRHICommandList& RHICmdList,
		uint32 InLevel,
		const FVector2D& DFDimension,
		FRHITexture* InTexture 
	)
	{
		SetShaderValue(RHICmdList, GetPixelShader(), CurLevel, InLevel);
		SetShaderValue(RHICmdList, GetPixelShader(), DistanceFieldDimension, DFDimension);
		SetTextureParameter(RHICmdList, GetPixelShader(), MaskTexture, TextureSampler, TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), InTexture);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);

		Ar << DistanceFieldDimension;
		Ar << MaskTexture;
		Ar << TextureSampler;

		return bShaderHasOutdatedParameters;
	}

private:
	FShaderParameter CurLevel;
	FShaderParameter DistanceFieldDimension;
	FShaderResourceParameter MaskTexture;
	FShaderResourceParameter TextureSampler;
};

IMPLEMENT_SHADER_TYPE(, FGenerateDistanceFieldShaderVS, TEXT("/Shaders/ShadowFakery.usf"), TEXT("GenerateDistanceFieldShaderVS"), SF_Vertex)
IMPLEMENT_SHADER_TYPE(, FGenerateDistanceFieldShaderPS, TEXT("/Shaders/ShadowFakery.usf"), TEXT("GenerateDistanceFieldShaderPS"), SF_Pixel)

void GenerateMeshMaskTexture(FRHICommandListImmediate& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, class UStaticMesh* StaticMesh, float StartDegree, uint32 TextureSize)
{
	check(IsInRenderingThread());
	
	if (!StaticMesh)return;

	FStaticMeshLODResources& LODModel = StaticMesh->RenderData->LODResources[0];
	FBoxSphereBounds& Bounds = StaticMesh->RenderData->Bounds;
	FPositionVertexBuffer& PositionVertexBuffer = LODModel.VertexBuffers.PositionVertexBuffer;
	FRawStaticIndexBuffer& Indices = LODModel.IndexBuffer;
	
	FBox MeshBox(Bounds.GetBox());
	const FVector MeshBoxExtent = MeshBox.GetExtent();
	const float DFVoulmeRadius = FMath::Max(MeshBoxExtent.GetMax() * 1.7f, MeshBoxExtent.Size()) * CVarMaskVolumeScale.GetValueOnRenderThread();

	const FBox DistanceFieldVolumeBox = FBox(MeshBox.GetCenter() - FVector(DFVoulmeRadius * 1.7f / 3.f), MeshBox.GetCenter() + FVector(DFVoulmeRadius * 1.7f / 3.f));
	const float DFVoulmeWidth = DistanceFieldVolumeBox.GetSize().X / 2.f;

	TArray<FDrawMaskInstance> ModelInstance;
	const static FVector4 Colors[4] = { FVector4(1.f, 0.f, 0.f, 0.f), FVector4(0.f, 1.f, 0.f, 0.f), FVector4(0.f, 0.f, 1.f, 0.f), FVector4(0.f, 0.f, 0.f, 1.f) };
	for (int32 i = 0; i < UE_ARRAY_COUNT(Colors); ++i)
	{
		const FMatrix ModelWorldMatrix = FTranslationMatrix(-MeshBox.GetCenter()) * FRotationMatrix(FRotator(i * 30.f, -StartDegree, 0.f));
		ModelInstance.Emplace(ModelWorldMatrix, Colors[i]);
	}
	
	FOrthoMatrix OrthoProjMatrix(DFVoulmeWidth, DFVoulmeWidth, 0.f, 0.f);
	FMatrix ViewRotationMatrix = FInverseRotationMatrix(FRotator(0.f, 180.f, 0.f));
	ViewRotationMatrix = ViewRotationMatrix * FMatrix(
		FPlane(0, 0, 1, 0),
		FPlane(1, 0, 0, 0),
		FPlane(0, 1, 0, 0),
		FPlane(0, 0, 0, 1));

	FVector ViewOrigin(DFVoulmeWidth * 2.f, 0.f, 0.f);
	const FMatrix ViewMatrix = FTranslationMatrix(-ViewOrigin) * ViewRotationMatrix;
	const FMatrix ViewProjMatrix = ViewMatrix * OrthoProjMatrix;

	FRHIResourceCreateInfo CreateInfo;
	//Create instanced buffer
	uint32 SizeInBytes = ModelInstance.Num() * sizeof(FDrawMaskInstance);
	FVertexBufferRHIRef MeshModelInstancedVB = RHICreateVertexBuffer(SizeInBytes, BUF_Static | BUF_ShaderResource, CreateInfo);
	void* MeshModelInstancedData = RHILockVertexBuffer(MeshModelInstancedVB, 0, SizeInBytes, RLM_WriteOnly);
	FPlatformMemory::Memcpy(MeshModelInstancedData, ModelInstance.GetData(), SizeInBytes);
	RHIUnlockVertexBuffer(MeshModelInstancedVB);
	//Create Index Buffer
	TArray<uint32> AllIndices;
	LODModel.IndexBuffer.GetCopy(AllIndices);
	SizeInBytes = AllIndices.Num() * sizeof(uint32);
	FIndexBufferRHIRef IndexBuffer = RHICreateIndexBuffer(sizeof(uint32), SizeInBytes, BUF_Static | BUF_ShaderResource, CreateInfo);
	void* MeshModelIndexData = RHILockIndexBuffer(IndexBuffer, 0, SizeInBytes, RLM_WriteOnly);
	FPlatformMemory::Memcpy(MeshModelIndexData, AllIndices.GetData(), SizeInBytes);
	RHIUnlockIndexBuffer(IndexBuffer);
	//Create vertex buffer 
	SizeInBytes = PositionVertexBuffer.GetNumVertices() * sizeof(FPositionVertex);
	FVertexBufferRHIRef VertexBuffer = RHICreateVertexBuffer(SizeInBytes, BUF_Static | BUF_ShaderResource, CreateInfo);
	void* Data = PositionVertexBuffer.GetVertexData();
	void* MeshModelVertexData = RHILockVertexBuffer(VertexBuffer, 0, SizeInBytes, RLM_WriteOnly);
	FPlatformMemory::Memcpy(MeshModelVertexData, Data, SizeInBytes);
	RHIUnlockVertexBuffer(VertexBuffer);
	
	TShaderMapRef<FGenerateMeshMaskShaderVS> VertexShader(GetGlobalShaderMap(FeatureLevel));
	TShaderMapRef<FGenerateMeshMaskShaderPS> PixelShader(GetGlobalShaderMap(FeatureLevel));

	VertexShader->SetParameters(RHICmdList, ViewProjMatrix);
	RHICmdList.SetViewport(0.f, 0.f, 0.f, TextureSize, TextureSize, 1.f);
	
	TRefCountPtr<IPooledRenderTarget> MaskRT;
	FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(FIntPoint(TextureSize, TextureSize), PF_A32B32G32R32F, FClearValueBinding::Transparent, TexCreate_GenerateMipCapable, TexCreate_RenderTargetable | TexCreate_ShaderResource, false));
	//Desc.NumSamples = 4;  //can not open mipmap and multisample simultaneously
	GRenderTargetPool.FindFreeElement(RHICmdList, Desc, MaskRT, TEXT("MaskRT"));
	
	FRHITexture* ColorRTs[1] = { MaskRT->GetRenderTargetItem().TargetableTexture };
	FRHIRenderPassInfo PassInfo(MaskRT->GetRenderTargetItem().TargetableTexture, ERenderTargetActions::Load_Store, MaskRT->GetRenderTargetItem().ShaderResourceTexture);
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
	RHICmdList.DrawIndexedPrimitive(IndexBuffer, 0, 0, Indices.GetNumIndices(), 0, Indices.GetNumIndices() / 3, ModelInstance.Num());
	RHICmdList.CopyToResolveTarget(MaskRT->GetRenderTargetItem().TargetableTexture, MaskRT->GetRenderTargetItem().ShaderResourceTexture, FResolveRect(0, 0, TextureSize, TextureSize));
	MaskRT->GetRenderTargetItem().TargetableTexture;
	FGenerateMips::Execute(RHICmdList, MaskRT->GetRenderTargetItem().TargetableTexture);
	//RHICmdList.GenerateMips(MaskRT->GetRenderTargetItem().TargetableTexture);
	VertexBuffer.SafeRelease();
	IndexBuffer.SafeRelease();
	MeshModelInstancedVB.SafeRelease();

	RHICmdList.EndRenderPass();

	FVector2D DFSize(16, 16);
	TRefCountPtr<IPooledRenderTarget> DistanceFieldRT;
	FPooledRenderTargetDesc DFDesc(FPooledRenderTargetDesc::Create2DDesc(FIntPoint(TextureSize, TextureSize), PF_A32B32G32R32F, FClearValueBinding::Transparent, TexCreate_None, TexCreate_RenderTargetable, false));
	GRenderTargetPool.FindFreeElement(RHICmdList, DFDesc, DistanceFieldRT, TEXT("DistanceFieldRT"));
	uint32 MaxLevel = FMath::RoundToInt(FMath::Log2(TextureSize));

	FTextureRHIRef CurRenderTarget = DistanceFieldRT->GetRenderTargetItem().TargetableTexture;
	FTextureRHIRef CurMaskTexture = MaskRT->GetRenderTargetItem().TargetableTexture;
	FRHICopyTextureInfo CopyInfo;
	RHICmdList.CopyTexture(CurMaskTexture, CurRenderTarget, CopyInfo);

	// Generate DistanceField Texture
	
	for (uint32 i = 1; i <= MaxLevel; ++i)
	{
		GenerateDistanceFieldTexture(RHICmdList, FeatureLevel, TextureSize, CurRenderTarget, CurMaskTexture, DFSize, i);
		Swap(CurRenderTarget, CurMaskTexture);
	}
}

void GenerateDistanceFieldTexture(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, uint32 TextureSize, FTextureRHIRef ResultRT, FTextureRHIRef MaskTexture, const FVector2D& DFDimension, uint32 CurLevel)
{
	RHICmdList.SetViewport(0, 0, 0.0f, TextureSize, TextureSize, 1.0f);

	FRHIRenderPassInfo RPInfo(ResultRT, ERenderTargetActions::Load_Store);
	RHICmdList.BeginRenderPass(RPInfo, TEXT("DistanceFieldTexturePass"));

	TShaderMap<FGlobalShaderType>* GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);
	TShaderMapRef<FGenerateDistanceFieldShaderVS> VertexShader(GlobalShaderMap);
	TShaderMapRef<FGenerateDistanceFieldShaderPS> PixelShader(GlobalShaderMap);    

	FCommonVertexDeclaration VertexDeclaration;
	VertexDeclaration.InitRHI();

	FGraphicsPipelineStateInitializer GraphicPSPoint;
	RHICmdList.ApplyCachedRenderTargets(GraphicPSPoint);
	GraphicPSPoint.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	GraphicPSPoint.BlendState = TStaticBlendState<>::GetRHI();
	GraphicPSPoint.RasterizerState = TStaticRasterizerState<>::GetRHI();
	GraphicPSPoint.PrimitiveType = PT_TriangleList;
	GraphicPSPoint.BoundShaderState.VertexDeclarationRHI = VertexDeclaration.VertexDeclarationRHI;
	GraphicPSPoint.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
	GraphicPSPoint.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
	SetGraphicsPipelineState(RHICmdList, GraphicPSPoint);

	FVertexInput Vertices[4];
	Vertices[0].Position.Set(-1.0f, 1.0f, 0, 1.0f);
	Vertices[1].Position.Set(1.0f, 1.0f, 0, 1.0f);
	Vertices[2].Position.Set(-1.0f, -1.0f, 0, 1.0f);
	Vertices[3].Position.Set(1.0f, -1.0f, 0, 1.0f);
	Vertices[0].UV = FVector2D(0.0f, 0.0f);
	Vertices[1].UV = FVector2D(1.0f, 0.0f);
	Vertices[2].UV = FVector2D(0.0f, 1.0f);
	Vertices[3].UV = FVector2D(1.0f, 1.0f);
	static const uint16 Indices[6] =
	{
		0, 1, 2,
		2, 1, 3
	};
	
	PixelShader->SetParameters(RHICmdList, CurLevel, DFDimension, MaskTexture);
	DrawIndexedPrimitiveUP_Custom(RHICmdList, PT_TriangleList, 0, UE_ARRAY_COUNT(Vertices), 2, Indices, sizeof(Indices[0]), Vertices, sizeof(Vertices[0]));

	RHICmdList.EndRenderPass();
}
