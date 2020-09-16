#include "RenderFluidVolume.h"
#include "RHI.h"
#include "RendererInterface.h"
#include "GlobalShader.h"
#include <ShaderParameterMacros.h>
#include "UniformBuffer.h"
#include "Shader.h"
#include "RenderGraphResources.h"
#include "RenderGraphBuilder.h"
#include "ShaderPermutation.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"
#include "ScreenRendering.h"
#include "../Private/SceneRendering.h"

struct FVolumeVertex
{
	FVector Position;
};

class FVolumeVertexDeclaration : public FRenderResource
{
public:
	FVertexDeclarationRHIRef VertexDeclarationRHI;

	// Destructor.
	virtual ~FVolumeVertexDeclaration() {}

	virtual void InitRHI() override
	{
		FVertexDeclarationElementList Elements;
		uint32 Stride = sizeof(FVolumeVertex);
		Elements.Add(FVertexElement(0, STRUCT_OFFSET(FVolumeVertex, Position), VET_Float3, 0, Stride));
		VertexDeclarationRHI = RHICreateVertexDeclaration(Elements);
	}

	virtual void ReleaseRHI() override
	{
		VertexDeclarationRHI.SafeRelease();
	}
};

static TGlobalResource<FVolumeVertexDeclaration> GVolumeVertexDeclaration;

static const TArray<FVector> VolumeVertices = { FVector(0, 0, 0),
										FVector(0, 1, 0),
										FVector(1, 0 ,0),
										FVector(1, 1, 0),
										FVector(0, 0, 1),
										FVector(0, 1, 1),
										FVector(1, 0, 1),
										FVector(1, 1, 1) };

static const  TArray<uint32> VolumeIndices = { 0, 4, 5,  5, 1, 0,
								1, 5, 3,  5, 7, 3,
								3, 7, 6,  6, 2, 3,
								6, 4, 0,  0, 2, 6,
								0, 1, 3,  3, 2, 0,
								4, 6, 7,  7, 5, 4 };

class FVolumeStreamBuffer : public FRenderResource
{
public:
	FVertexBufferRHIRef VolumeVertexBuffer;

	FIndexBufferRHIRef VolumeIndexBuffer;

	virtual ~FVolumeStreamBuffer() {}

	virtual void InitRHI() override
	{
		FRHIResourceCreateInfo CreateInfo;
		VolumeVertexBuffer = RHICreateVertexBuffer(sizeof(FVolumeVertex) * VolumeVertices.Num(), BUF_Static, CreateInfo);
		void* VoidPtr = RHILockVertexBuffer(VolumeVertexBuffer, 0, sizeof(FVolumeVertex) * VolumeVertices.Num(), RLM_WriteOnly);
		FMemory::Memcpy(VoidPtr, VolumeVertices.GetData(), sizeof(FVolumeVertex) * VolumeVertices.Num());
		RHIUnlockVertexBuffer(VolumeVertexBuffer);

		VolumeIndexBuffer = RHICreateIndexBuffer(sizeof(uint32), sizeof(uint32) * VolumeIndices.Num(), BUF_Static, CreateInfo);
		void* VoidPtr = RHILockIndexBuffer(VolumeIndexBuffer, 0, sizeof(uint32) * VolumeIndices.Num(), RLM_WriteOnly);
		FMemory::Memcpy(VoidPtr, VolumeIndices.GetData(), sizeof(uint32) * VolumeIndices.Num());
		RHIUnlockIndexBuffer(VolumeIndexBuffer);
	}

	virtual void ReleaseRHI() override
	{
		VolumeVertexBuffer.SafeRelease();
		VolumeIndexBuffer.SafeRelease();
	}
};

static TGlobalResource<FVolumeStreamBuffer> GVolumeStreamBuffer;

class FFluidVolumeBackVS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FFluidVolumeBackVS, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	FFluidVolumeBackVS() {}

public:
	FFluidVolumeBackVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		WorldViewProjection.Bind(Initializer.ParameterMap, TEXT("WorldViewProjection"));
		EyePosToVolume.Bind(Initializer.ParameterMap, TEXT("EyePosToVolume"));
	}

	void SetParameters(FRHICommandListImmediate& RHICmdList, const FMatrix& WVP, const FVector& EyePosToVol)
	{
		
		SetShaderValue(RHICmdList, RHICmdList.GetBoundVertexShader(), WorldViewProjection, WVP);
		SetShaderValue(RHICmdList, RHICmdList.GetBoundVertexShader(), EyePosToVolume, EyePosToVol);
	}

	LAYOUT_FIELD(FShaderParameter, WorldViewProjection)
	LAYOUT_FIELD(FShaderParameter, EyePosToVolume)
};

IMPLEMENT_SHADER_TYPE(, FFluidVolumeBackVS, TEXT("/Shaders/Private/Fluid3D.usf"), TEXT("VolumeBackVS"), SF_Vertex);

class FFluidVolumeBackPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FFluidVolumeBackPS, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	FFluidVolumeBackPS() {}

public:
	FFluidVolumeBackPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		WorldViewProjection.Bind(Initializer.ParameterMap, TEXT("WorldViewProjection"));
		EyePosToVolume.Bind(Initializer.ParameterMap, TEXT("EyePosToVolume"));
	}

	LAYOUT_FIELD(FShaderParameter, WorldViewProjection)
	LAYOUT_FIELD(FShaderParameter, EyePosToVolume)
};

IMPLEMENT_SHADER_TYPE(, FFluidVolumeBackPS, TEXT("/Shaders/Private/RenderFluidVolume.usf"), TEXT("VolumeBackPS"), SF_Pixel);



class FFluidVolumeFrontPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FFluidVolumeFrontPS, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	FFluidVolumeFrontPS() {}

public:
	FFluidVolumeFrontPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		
	} 
};

IMPLEMENT_SHADER_TYPE(, FFluidVolumeFrontPS, TEXT("/Shaders/Private/RenderFluidVolume.usf"), TEXT("VolumeFrontPS"), SF_Pixel);


void DrawVolumeBox(FRHICommandListImmediate& RHICmdList, TRefCountPtr<IPooledRenderTarget> RenderTarget, const FViewInfo& View, ERHIFeatureLevel::Type FeatureLevel)
{	
	FScaleMatrix VolumeScale = FScaleMatrix(FVector::OneVector * 50);
	auto ViewProj = VolumeScale * View.ViewMatrices.GetViewProjectionMatrix();
	FVector EyePosToVolume = View.ViewMatrices.GetInvViewMatrix().TransformPosition(View.ViewLocation);

	// #TODO
	FMatrix TranslatedMatrix = FScaleMatrix(FVector(1.f, 1.f, -1.f)) * FRotationMatrix(FRotator(0.f, 90.f, 90.f)) * FTranslationMatrix(FVector(0.f, 0.f, 100.f));

	FRHIRenderPassInfo RPInfo(RenderTarget->GetRenderTargetItem().TargetableTexture, ERenderTargetActions::Clear_DontStore);
	RHICmdList.BeginRenderPass(RPInfo, TEXT("DrawVolumeBox"));
	{
		FIntRect SrcRect = View.ViewRect;
		FIntRect DestRect = View.ViewRect;

		RHICmdList.SetViewport(0, 0, 0, View.ViewRect.Width(), View.ViewRect.Height(), 1);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, ERasterizerCullMode::CM_CCW>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(FeatureLevel);
		TShaderMapRef<FVolumeVertex> VertexShader(ShaderMap);
		TShaderMapRef<FFluidVolumeBackPS> BackPixelShader(View.ShaderMap);

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GVolumeVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = BackPixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		RHICmdList.SetStreamSource(0, GVolumeStreamBuffer.VolumeVertexBuffer, 0);
		RHICmdList.DrawIndexedPrimitive(GVolumeStreamBuffer.VolumeIndexBuffer, 0, 0, VolumeVertices.Num(), 0, VolumeIndices.Num() / 3, 1);

		TShaderMapRef<FFluidVolumeFrontPS> FrontPixelShader(ShaderMap);
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Subtract, BF_One, BF_Zero>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, ERasterizerCullMode::CM_CW>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = FrontPixelShader.GetPixelShader();
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		RHICmdList.SetStreamSource(0, GVolumeStreamBuffer.VolumeVertexBuffer, 0);
		RHICmdList.DrawIndexedPrimitive(GVolumeStreamBuffer.VolumeIndexBuffer, 0, 0, VolumeVertices.Num(), 0, VolumeIndices.Num() / 3, 1);
	}
	RHICmdList.EndRenderPass();
}

void RenderFluidVolume(FRHICommandListImmediate& RHICmdList, FIntVector FluidVolumeSize, ERHIFeatureLevel::Type FeatureLevel)
{
	FPooledRenderTargetDesc FluidVloumeDesc = FPooledRenderTargetDesc::CreateVolumeDesc(FluidVolumeSize.X, FluidVolumeSize.Y, FluidVolumeSize.Z, EPixelFormat::PF_A32B32G32R32F, FClearValueBinding::None, ETextureCreateFlags::TexCreate_None, ETextureCreateFlags::TexCreate_UAV | ETextureCreateFlags::TexCreate_ShaderResource, false);
	TRefCountPtr<IPooledRenderTarget> ColorTexture3D_0;
	GRenderTargetPool.FindFreeElement(RHICmdList, FluidVloumeDesc, ColorTexture3D_0, TEXT("ColorTexture3D_0"));
}