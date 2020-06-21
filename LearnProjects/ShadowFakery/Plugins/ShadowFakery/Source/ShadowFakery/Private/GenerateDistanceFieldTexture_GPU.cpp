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
#include "ClearQuad.h"
#include "Engine/TextureRenderTarget.h"
#include "AssetRegistryModule.h"

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

void GenerateDistanceFieldTexture(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, uint32 TextureSize, FRHITexture* ResultRTs[], FRHITexture* MaskTextures[], const FVector2D& DFDimension, uint32 CurLevel);

#if GENERATE_TEXTURE_DF
struct FDrawMaskInstance
{
	FDrawMaskInstance(const FMatrix& InWorld):
		World(InWorld)
	{}

	FMatrix World;
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

template<class VertexShaderType, class PixelShaderType, typename... ArgsType>
void DrawQuadWithPSParams(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, const TCHAR* PassName, FIntPoint ViewportSize, uint32 RTCounts, FRHITexture* ResultRTs[], ArgsType&&... Args)
{
	RHICmdList.SetViewport(0, 0, 0.0f, ViewportSize.X, ViewportSize.Y, 1.0f);
	
	FRHIRenderPassInfo RPInfo(RTCounts, ResultRTs, ERenderTargetActions::Load_Store);
	RHICmdList.BeginRenderPass(RPInfo, PassName);

	FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);
	TShaderMapRef<VertexShaderType> VertexShader(GlobalShaderMap);
	TShaderMapRef<PixelShaderType> PixelShader(GlobalShaderMap);    

	FCommonVertexDeclaration VertexDeclaration;
	VertexDeclaration.InitRHI();

	FGraphicsPipelineStateInitializer GraphicPSPoint;
	RHICmdList.ApplyCachedRenderTargets(GraphicPSPoint);
	GraphicPSPoint.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	GraphicPSPoint.BlendState = TStaticBlendState<>::GetRHI();
	GraphicPSPoint.RasterizerState = TStaticRasterizerState<>::GetRHI();
	GraphicPSPoint.PrimitiveType = PT_TriangleList;
	GraphicPSPoint.BoundShaderState.VertexDeclarationRHI = VertexDeclaration.VertexDeclarationRHI;
	GraphicPSPoint.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
	GraphicPSPoint.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
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
	
	PixelShader->SetParameters(RHICmdList, Forward<ArgsType>(Args)...);
	DrawIndexedPrimitiveUP_Custom(RHICmdList, PT_TriangleList, 0, UE_ARRAY_COUNT(Vertices), 2, Indices, sizeof(Indices[0]), Vertices, sizeof(Vertices[0]));

	RHICmdList.EndRenderPass();
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
		SetShaderValue(RHICmdList, RHICmdList.GetBoundVertexShader(), ViewProjMatrix, ViewProjMat);
	}

private:
	LAYOUT_FIELD(FShaderParameter, ViewProjMatrix);
	//FShaderParameter ViewProjMatrix;
};

class FGenerateMeshMaskShaderPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FGenerateMeshMaskShaderPS, Global)

public:
	FGenerateMeshMaskShaderPS() {};

	FGenerateMeshMaskShaderPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		TextureSize.Bind(Initializer.ParameterMap, TEXT("TextureSize1"));
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
		float InTextureSize
	)
	{
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), TextureSize, InTextureSize);
	}

	/*virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);

		Ar << TextureSize;

		return bShaderHasOutdatedParameters;
	}*/

private:
	LAYOUT_FIELD(FShaderParameter, TextureSize);
};

IMPLEMENT_SHADER_TYPE(, FGenerateMeshMaskShaderVS, TEXT("/Plugins/Shaders/ShadowFakery.usf"), TEXT("GenerateMeshMaskShaderVS"), SF_Vertex)
IMPLEMENT_SHADER_TYPE(, FGenerateMeshMaskShaderPS, TEXT("/Plugins/Shaders/ShadowFakery.usf"), TEXT("GenerateMeshMaskShaderPS"), SF_Pixel)

class FGeneralShaderVS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FGeneralShaderVS, Global)

public:
	FGeneralShaderVS() {};

	FGeneralShaderVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
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

	/*virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		return bShaderHasOutdatedParameters;
	}*/

private:
};

IMPLEMENT_SHADER_TYPE(, FGeneralShaderVS, TEXT("/Plugins/Shaders/ShadowFakery.usf"), TEXT("GeneralShaderVS"), SF_Vertex)

//IMPLEMENT_SHADER_TYPE(, FPrepareDistanceFieldShaderPS, TEXT("/Shaders/ShadowFakery.usf"), TEXT("PrepareDistanceFieldShaderPS"), SF_Pixel)

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
		MaskTexture0.Bind(Initializer.ParameterMap, TEXT("MaskTexture0"));
		MaskTexture1.Bind(Initializer.ParameterMap, TEXT("MaskTexture1"));
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
		FRHITexture* InMaskTexture0,
		FRHITexture* InMaskTexture1
	)
	{
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), CurLevel, InLevel);
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), DistanceFieldDimension, DFDimension);
		SetTextureParameter(RHICmdList, RHICmdList.GetBoundPixelShader(), MaskTexture0, TextureSampler, TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), InMaskTexture0);
		SetTextureParameter(RHICmdList, RHICmdList.GetBoundPixelShader(), MaskTexture1, InMaskTexture1);
	}

	/*virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);

		Ar << CurLevel;
		Ar << DistanceFieldDimension;
		Ar << MaskTexture0;
		Ar << MaskTexture1;
		Ar << TextureSampler;

		return bShaderHasOutdatedParameters;
	}*/

private:
	LAYOUT_FIELD(FShaderParameter, CurLevel);
	LAYOUT_FIELD(FShaderParameter, DistanceFieldDimension);
	LAYOUT_FIELD(FShaderResourceParameter, MaskTexture0);
	LAYOUT_FIELD(FShaderResourceParameter, MaskTexture1);
	LAYOUT_FIELD(FShaderResourceParameter, TextureSampler);
};

//IMPLEMENT_SHADER_TYPE(, FGeneralShaderVS, TEXT("/Shaders/ShadowFakery.usf"), TEXT("GeneralShaderVS"), SF_Vertex)
IMPLEMENT_SHADER_TYPE(, FGenerateDistanceFieldShaderPS, TEXT("/Plugins/Shaders/ShadowFakery.usf"), TEXT("GenerateDistanceFieldShaderPS"), SF_Pixel)

class FRevertToDistanceFieldShaderPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FRevertToDistanceFieldShaderPS, Global)

public:
	FRevertToDistanceFieldShaderPS() {};

	FRevertToDistanceFieldShaderPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		MaskTexture0.Bind(Initializer.ParameterMap, TEXT("MaskTexture0"));
		MaskTexture1.Bind(Initializer.ParameterMap, TEXT("MaskTexture1"));
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
		FRHITexture* InTexture0, 
		FRHITexture* InTexture1 
	)
	{
		SetTextureParameter(RHICmdList, RHICmdList.GetBoundPixelShader(), MaskTexture0, TextureSampler, TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), InTexture0);
		SetTextureParameter(RHICmdList, RHICmdList.GetBoundPixelShader(), MaskTexture1, InTexture1);
	}

	/*virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);

		Ar << MaskTexture0;
		Ar << MaskTexture1;
		Ar << TextureSampler;

		return bShaderHasOutdatedParameters;
	}*/

private:
	LAYOUT_FIELD(FShaderResourceParameter, MaskTexture0);
	LAYOUT_FIELD(FShaderResourceParameter, MaskTexture1);
	LAYOUT_FIELD(FShaderResourceParameter, TextureSampler);
};

IMPLEMENT_SHADER_TYPE(, FRevertToDistanceFieldShaderPS, TEXT("/Plugins/Shaders/ProcessShadowFakery.usf"), TEXT("RevertToDistanceFieldShaderPS"), SF_Pixel)

class FReconstructAndReverseDistanceFieldShaderPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FReconstructAndReverseDistanceFieldShaderPS, Global)

public:
	FReconstructAndReverseDistanceFieldShaderPS() {};

	FReconstructAndReverseDistanceFieldShaderPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		DistanceOffset.Bind(Initializer.ParameterMap, TEXT("DistanceOffset"));
		UnsignedDistanceField.Bind(Initializer.ParameterMap, TEXT("UnsignedDistanceField"));
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
		float InDistanceOffset,
		FRHITexture* InUnSignedDistanceField
	)
	{
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), DistanceOffset, InDistanceOffset);
		SetTextureParameter(RHICmdList, RHICmdList.GetBoundPixelShader(), UnsignedDistanceField, TextureSampler, TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), InUnSignedDistanceField);
	}

	/*virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);

		Ar << DistanceOffset;
		Ar << UnsignedDistanceField;
		Ar << TextureSampler;

		return bShaderHasOutdatedParameters;
	}*/

private:
	LAYOUT_FIELD(FShaderParameter, DistanceOffset);
	LAYOUT_FIELD(FShaderResourceParameter, UnsignedDistanceField);
	LAYOUT_FIELD(FShaderResourceParameter, TextureSampler);
};

IMPLEMENT_SHADER_TYPE(, FReconstructAndReverseDistanceFieldShaderPS, TEXT("/Plugins/Shaders/ProcessShadowFakery.usf"), TEXT("ReconstructDistanceFieldShaderPS"), SF_Pixel)

#define THREADGROUP_SIZE 32u

class FMergeToAtlasCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FMergeToAtlasCS, Global)

public:
	FMergeToAtlasCS() {};

	FMergeToAtlasCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		TileIndex.Bind(Initializer.ParameterMap, TEXT("TileIndex"));
		SignedDistanceField.Bind(Initializer.ParameterMap, TEXT("SignedDistanceField"));
		UnsignedDistanceField.Bind(Initializer.ParameterMap, TEXT("UnsignedDistanceField"));
		RWAtlasTexture.Bind(Initializer.ParameterMap, TEXT("RWAtlasTexture"));
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

		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), THREADGROUP_SIZE);
	}

	void SetParameters(
		FRHICommandList& RHICmdList,
		uint32 InTileIndex,
		FRHITexture* InSignedDistanceField,
		FRHITexture* InUnSignedDistanceField,
		FRHIUnorderedAccessView* AtlasTex
	)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();

		SetShaderValue(RHICmdList, ShaderRHI, TileIndex, InTileIndex);
		SetTextureParameter(RHICmdList, ShaderRHI, SignedDistanceField, InSignedDistanceField);
		SetTextureParameter(RHICmdList, ShaderRHI, UnsignedDistanceField, InUnSignedDistanceField);
		SetUAVParameter(RHICmdList, ShaderRHI, RWAtlasTexture, AtlasTex);
	}

	void UnsetParameters(FRHICommandList& RHICmdList)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();

		SetUAVParameter(RHICmdList, ShaderRHI, RWAtlasTexture, 0);
	}

private:

	LAYOUT_FIELD(FShaderParameter, TileIndex);
	LAYOUT_FIELD(FShaderResourceParameter, SignedDistanceField);
	LAYOUT_FIELD(FShaderResourceParameter, UnsignedDistanceField);
	LAYOUT_FIELD(FShaderResourceParameter, RWAtlasTexture);
};

IMPLEMENT_SHADER_TYPE(, FMergeToAtlasCS, TEXT("/Plugins/Shaders/ProcessShadowFakery.usf"), TEXT("MergeToAtlasCS"), SF_Compute)
#endif

void GenerateMeshMaskTexture(FRHICommandListImmediate& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, class UStaticMesh* StaticMesh, FRHITexture*& MergedDistanceFieldTexture, class UTextureRenderTarget* OutputRenderTarget, float StartDegree, uint32 TextureSize)
{
	check(IsInRenderingThread());
	
#if GENERATE_TEXTURE_DF
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
		const FMatrix ModelWorldMatrix = FTranslationMatrix(-MeshBox.GetCenter()) * FRotationMatrix(FRotator(0.f, -StartDegree, 0.f)) * FRotationMatrix(FRotator(i * 30.f, 0.f, 0.f));
		ModelInstance.Emplace(ModelWorldMatrix);
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

	RHICmdList.SetViewport(0.f, 0.f, 0.f, TextureSize, TextureSize, 1.f);
	
	TRefCountPtr<IPooledRenderTarget> MaskRT0;
	TRefCountPtr<IPooledRenderTarget> MaskRT1;
	FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(FIntPoint(TextureSize, TextureSize), PF_A32B32G32R32F, FClearValueBinding::Transparent, TexCreate_GenerateMipCapable, TexCreate_RenderTargetable | TexCreate_ShaderResource, false));
	//Desc.NumSamples = 4;  //can not open mipmap and multisample simultaneously
	GRenderTargetPool.FindFreeElement(RHICmdList, Desc, MaskRT0, TEXT("MaskRT0"));
	GRenderTargetPool.FindFreeElement(RHICmdList, Desc, MaskRT1, TEXT("MaskRT1"));
	
	FRHITexture* ColorRTs[2] = { MaskRT0->GetRenderTargetItem().TargetableTexture, MaskRT1->GetRenderTargetItem().TargetableTexture };
	FRHIRenderPassInfo PassInfo(UE_ARRAY_COUNT(ColorRTs), ColorRTs, ERenderTargetActions::Clear_Store);
	RHICmdList.BeginRenderPass(PassInfo, TEXT("GenerateMeshMask"));
	// #TODO
	//DrawClearQuad(RHICmdList, FLinearColor::Transparent);
	FOnlyPosVertexDeclaration VertexDeclaration;
	VertexDeclaration.InitRHI();
	FGraphicsPipelineStateInitializer GraphicPSPoint;
	RHICmdList.ApplyCachedRenderTargets(GraphicPSPoint);
	GraphicPSPoint.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	GraphicPSPoint.BlendState = TStaticBlendState<CW_RGBA, BO_Max, BF_One, BF_One, BO_Max, BF_One, BF_One, CW_RGBA, BO_Max, BF_One, BF_One, BO_Max, BF_One, BF_One>::GetRHI();
	GraphicPSPoint.RasterizerState = TStaticRasterizerState<>::GetRHI();
	GraphicPSPoint.PrimitiveType = PT_TriangleList;
	GraphicPSPoint.BoundShaderState.VertexDeclarationRHI = VertexDeclaration.VertexDeclarationRHI;
	GraphicPSPoint.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
	GraphicPSPoint.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
	SetGraphicsPipelineState(RHICmdList, GraphicPSPoint);

	VertexShader->SetParameters(RHICmdList, ViewProjMatrix);
	PixelShader->SetParameters(RHICmdList, TextureSize);
	RHICmdList.SetStreamSource(0, VertexBuffer, 0);
	RHICmdList.SetStreamSource(1, MeshModelInstancedVB, 0);
	RHICmdList.DrawIndexedPrimitive(IndexBuffer, 0, 0, Indices.GetNumIndices(), 0, Indices.GetNumIndices() / 3, ModelInstance.Num());
	//RHICmdList.CopyToResolveTarget(MaskRT->GetRenderTargetItem().TargetableTexture, MaskRT->GetRenderTargetItem().ShaderResourceTexture, FResolveRect(0, 0, TextureSize, TextureSize));
	//MaskRT->GetRenderTargetItem().TargetableTexture;
	//FGenerateMips::Execute(RHICmdList, MaskRT->GetRenderTargetItem().TargetableTexture);
	//RHICmdList.GenerateMips(MaskRT->GetRenderTargetItem().TargetableTexture);

	VertexBuffer.SafeRelease();
	IndexBuffer.SafeRelease();
	MeshModelInstancedVB.SafeRelease();

	RHICmdList.EndRenderPass();

	FVector2D DFSize(16, 16);
	TRefCountPtr<IPooledRenderTarget> TempMaskRT0;
	TRefCountPtr<IPooledRenderTarget> TempMaskRT1;
	FPooledRenderTargetDesc DFDesc(FPooledRenderTargetDesc::Create2DDesc(FIntPoint(TextureSize, TextureSize), PF_A32B32G32R32F, FClearValueBinding::Transparent, TexCreate_None, TexCreate_RenderTargetable, false));
	GRenderTargetPool.FindFreeElement(RHICmdList, DFDesc, TempMaskRT0, TEXT("TempMaskRT0"));
	GRenderTargetPool.FindFreeElement(RHICmdList, DFDesc, TempMaskRT1, TEXT("TempMaskRT1"));
	uint32 MaxLevel = FMath::RoundToInt(FMath::Log2(TextureSize) + 0.5f);

	FRHITexture* CurRenderTargets[2] = { TempMaskRT0->GetRenderTargetItem().TargetableTexture, TempMaskRT1->GetRenderTargetItem().TargetableTexture };
	FRHITexture* CurMaskTextures[2] = { MaskRT0->GetRenderTargetItem().TargetableTexture, MaskRT1->GetRenderTargetItem().TargetableTexture };
	//FRHICopyTextureInfo CopyInfo;
	//RHICmdList.CopyTexture(CurMaskTexture, CurRenderTarget, CopyInfo);

	//Generate DistanceField Texture
	for (uint32 i = 1; i <= MaxLevel; ++i)
	{
		DrawQuadWithPSParams<FGeneralShaderVS, FGenerateDistanceFieldShaderPS>(RHICmdList, FeatureLevel, TEXT("DistanceFieldTexturePass"), FIntPoint(TextureSize, TextureSize), 2, CurRenderTargets, i, DFSize, CurMaskTextures[0], CurMaskTextures[1]);
		Swap(CurRenderTargets, CurMaskTextures);
	}

	//Revert to reversed DistanceField Texture value is 0-1
	TRefCountPtr<IPooledRenderTarget> UnsignedDistanceFieldRT;
	{
		GRenderTargetPool.FindFreeElement(RHICmdList, DFDesc, UnsignedDistanceFieldRT, TEXT("UnsignedDistanceFieldRT"));
		FRHITexture* RTs[] = { UnsignedDistanceFieldRT->GetRenderTargetItem().TargetableTexture };
		DrawQuadWithPSParams<FGeneralShaderVS, FRevertToDistanceFieldShaderPS>(RHICmdList, FeatureLevel, TEXT("RevertDistanceFieldPass"), FIntPoint(TextureSize, TextureSize), 1, RTs, CurMaskTextures[0], CurMaskTextures[1]);
	}
	
	//Prepare to draw reverse DistanceField Texture value is 0-1
	{
		DrawQuadWithPSParams<FGeneralShaderVS, FReconstructAndReverseDistanceFieldShaderPS>(RHICmdList, FeatureLevel, TEXT("ReconstructAndReverseDistanceFieldPass"), FIntPoint(TextureSize, TextureSize), 2, CurMaskTextures, 1.f / TextureSize * 8.f, UnsignedDistanceFieldRT->GetRenderTargetItem().TargetableTexture);
	}

	//Generate reversed DistanceField Texture
	for (uint32 i = 1; i <= MaxLevel; ++i)
	{
		DrawQuadWithPSParams<FGeneralShaderVS, FGenerateDistanceFieldShaderPS>(RHICmdList, FeatureLevel, TEXT("DistanceFieldTexturePass"), FIntPoint(TextureSize, TextureSize), 2, CurRenderTargets, i, DFSize, CurMaskTextures[0], CurMaskTextures[1]);
		Swap(CurRenderTargets, CurMaskTextures);
	}

	//Revert to reversed DistanceField Texture value is 0-1
	TRefCountPtr<IPooledRenderTarget> SignedDistanceFieldRT;
	{
		GRenderTargetPool.FindFreeElement(RHICmdList, DFDesc, SignedDistanceFieldRT, TEXT("SignedDistanceFieldRT"));
		FRHITexture* RTs[] = { SignedDistanceFieldRT->GetRenderTargetItem().TargetableTexture };
		DrawQuadWithPSParams<FGeneralShaderVS, FRevertToDistanceFieldShaderPS>(RHICmdList, FeatureLevel, TEXT("RevertDistanceFieldPass"), FIntPoint(TextureSize, TextureSize), 1, RTs, CurMaskTextures[0], CurMaskTextures[1]);
	}
	
	//Merge two distance field 
	TRefCountPtr<IPooledRenderTarget> MergedDistanceFieldRT;
	{
		FPooledRenderTargetDesc MDesc(FPooledRenderTargetDesc::Create2DDesc(FIntPoint(TextureSize * 4, TextureSize * 4), /*PF_A32B32G32R32F*/ PF_R32_UINT, FClearValueBinding::Transparent, TexCreate_None, TexCreate_RenderTargetable | TexCreate_UAV, false));
		GRenderTargetPool.FindFreeElement(RHICmdList, MDesc, MergedDistanceFieldRT, TEXT("MergedDistanceFieldRT"));
		//FRHITexture* FinalRenderTargets[] = { MergedDistanceFieldRT->GetRenderTargetItem().TargetableTexture };

		RHICmdList.BeginComputePass(TEXT("MergeToAtlas"));
		TShaderMapRef<FMergeToAtlasCS> MergeCS(GetGlobalShaderMap(FeatureLevel));
		FRHIComputeShader* ShaderRHI = MergeCS.GetComputeShader();
		RHICmdList.SetComputeShader(ShaderRHI);

		//RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, SignedDistanceFieldRT->GetRenderTargetItem().TargetableTexture);
		auto AtlasUAV = RHICreateUnorderedAccessView(MergedDistanceFieldRT->GetRenderTargetItem().TargetableTexture, 0);
		MergeCS->SetParameters(RHICmdList, 0, SignedDistanceFieldRT->GetRenderTargetItem().TargetableTexture, UnsignedDistanceFieldRT->GetRenderTargetItem().TargetableTexture, AtlasUAV);
		DispatchComputeShader(RHICmdList, MergeCS, FMath::DivideAndRoundUp(TextureSize, THREADGROUP_SIZE), FMath::DivideAndRoundUp(TextureSize, THREADGROUP_SIZE), 1);
		MergeCS->UnsetParameters(RHICmdList);
		RHICmdList.EndComputePass();
		//DrawQuadWithPSParams<FGeneralShaderVS, FMergeDistanceFieldShaderPS>(RHICmdList, FeatureLevel, TEXT("MergeDistanceFieldPass"), FIntPoint(TextureSize, TextureSize), 1, FinalRenderTargets, SignedDistanceFieldRT->GetRenderTargetItem().TargetableTexture, UnsignedDistanceFieldRT->GetRenderTargetItem().TargetableTexture);

		MergedDistanceFieldTexture = MergedDistanceFieldRT->GetRenderTargetItem().TargetableTexture;
	}
	
	//

	FRHICopyTextureInfo CopyInfo;
	if (OutputRenderTarget)
		RHICmdList.CopyTexture(MergedDistanceFieldRT->GetRenderTargetItem().TargetableTexture, OutputRenderTarget->GetRenderTargetResource()->TextureRHI, CopyInfo);
#endif
}
