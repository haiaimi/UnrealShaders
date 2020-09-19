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
		VoidPtr = RHILockIndexBuffer(VolumeIndexBuffer, 0, sizeof(uint32) * VolumeIndices.Num(), RLM_WriteOnly);
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

enum : int32
{
	BackwardVolume = 0,
	FrontVolume = 1
};

template<int32 Index>
class FFluidVolumeVS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FFluidVolumeVS, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	FFluidVolumeVS() {}

public:
	FFluidVolumeVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		WorldViewProjection.Bind(Initializer.ParameterMap, TEXT("WorldViewProjection"));
	}

	void SetParameters(FRHICommandListImmediate& RHICmdList, const FMatrix& WVP)
	{
		
		SetShaderValue(RHICmdList, RHICmdList.GetBoundVertexShader(), WorldViewProjection, WVP);
	}

	LAYOUT_FIELD(FShaderParameter, WorldViewProjection)
};

IMPLEMENT_SHADER_TYPE(, FFluidVolumeVS<0>, TEXT("/Shaders/Private/RenderFluidVolume.usf"), TEXT("VolumeBackVS"), SF_Vertex);
IMPLEMENT_SHADER_TYPE(, FFluidVolumeVS<1>, TEXT("/Shaders/Private/RenderFluidVolume.usf"), TEXT("VolumeFrontVS"), SF_Vertex);

template<int32 Index>
class FFluidVolumePS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FFluidVolumePS, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	FFluidVolumePS() {}

public:
	FFluidVolumePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{

	}
};

IMPLEMENT_SHADER_TYPE(, FFluidVolumePS<0>, TEXT("/Shaders/Private/RenderFluidVolume.usf"), TEXT("VolumeBackPS"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(, FFluidVolumePS<1>, TEXT("/Shaders/Private/RenderFluidVolume.usf"), TEXT("VolumeFrontPS"), SF_Pixel);

void DrawVolumeBox(FRHICommandListImmediate& RHICmdList, TRefCountPtr<IPooledRenderTarget> RenderTarget, const FViewInfo& View, ERHIFeatureLevel::Type FeatureLevel)
{	
	FScaleMatrix VolumeScale = FScaleMatrix(FVector::OneVector * 200);
	auto WorldViewProj = VolumeScale * FTranslationMatrix(FVector::UpVector * 200.f) * View.ViewMatrices.GetViewProjectionMatrix();
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
		TShaderMapRef<FFluidVolumeVS<BackwardVolume>> BackVertexShader(ShaderMap);
		TShaderMapRef<FFluidVolumePS<BackwardVolume>> BackPixelShader(View.ShaderMap);

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GVolumeVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = BackVertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = BackPixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;
		
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
		BackVertexShader->SetParameters(RHICmdList, WorldViewProj);

		RHICmdList.SetStreamSource(0, GVolumeStreamBuffer.VolumeVertexBuffer, 0);
		RHICmdList.DrawIndexedPrimitive(GVolumeStreamBuffer.VolumeIndexBuffer, 0, 0, VolumeVertices.Num(), 0, VolumeIndices.Num() / 3, 1);

		TShaderMapRef<FFluidVolumeVS<FrontVolume>> FrontVertexShader(ShaderMap);
		TShaderMapRef<FFluidVolumePS<FrontVolume>> FrontPixelShader(ShaderMap);
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Subtract, BF_One, BF_Zero>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, ERasterizerCullMode::CM_CW>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = FrontVertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = FrontPixelShader.GetPixelShader();
		
		FrontVertexShader->SetParameters(RHICmdList, WorldViewProj);
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		RHICmdList.SetStreamSource(0, GVolumeStreamBuffer.VolumeVertexBuffer, 0);
		RHICmdList.DrawIndexedPrimitive(GVolumeStreamBuffer.VolumeIndexBuffer, 0, 0, VolumeVertices.Num(), 0, VolumeIndices.Num() / 3, 1);
	}
	RHICmdList.EndRenderPass();
}

void RenderFluidVolume(FRHICommandListImmediate& RHICmdList, FIntVector FluidVolumeSize, const FViewInfo& View, ERHIFeatureLevel::Type FeatureLevel)
{
	FPooledRenderTargetDesc FluidVloumeDesc = FPooledRenderTargetDesc::CreateVolumeDesc(FluidVolumeSize.X, FluidVolumeSize.Y, FluidVolumeSize.Z, EPixelFormat::PF_A32B32G32R32F, FClearValueBinding::None, ETextureCreateFlags::TexCreate_None, ETextureCreateFlags::TexCreate_UAV | ETextureCreateFlags::TexCreate_ShaderResource, false);
	FPooledRenderTargetDesc RayMarchDesc = FPooledRenderTargetDesc::Create2DDesc(FIntPoint(View.ViewRect.Width(), View.ViewRect.Height()), EPixelFormat::PF_A32B32G32R32F, FClearValueBinding::Black, ETextureCreateFlags::TexCreate_None, ETextureCreateFlags::TexCreate_RenderTargetable | ETextureCreateFlags::TexCreate_ShaderResource, false);
	TRefCountPtr<IPooledRenderTarget> ColorTexture3D_0, RayMarchData;
	GRenderTargetPool.FindFreeElement(RHICmdList, FluidVloumeDesc, ColorTexture3D_0, TEXT("ColorTexture3D_0"));
	GRenderTargetPool.FindFreeElement(RHICmdList, RayMarchDesc, RayMarchData, TEXT("RayMarchData"));

	DrawVolumeBox(RHICmdList, RayMarchData, View, FeatureLevel);
}