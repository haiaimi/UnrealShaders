#include "Simulation/RenderFluidVolume.h"
#include "RHI.h"
#include "RHIDefinitions.h"
#include "RHICommandList.h"
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
#include "EngineGlobals.h"
#include "EngineModule.h"
#include "TextureResource.h"
#include "SceneViewExtension.h"

// This was used to get the viewinfo in the renderer
class FVolumeFluidViewUniformBufferExtension : public IPersistentViewUniformBufferExtension
{
public:
	virtual void BeginFrame() override
	{
		
	}

	virtual void PrepareView(const FSceneView* View) override
	{
		ViewInfo = static_cast<const FViewInfo*>(View);
	}

	virtual void BeginRenderView(const FSceneView* View, bool bShouldWaitForJobs = true) override
	{
	}

	virtual void EndFrame() override
	{
		//ViewInfo->
		int32 A = 1 + 2;
		//ViewInfo = nullptr;
	}

	const FViewInfo* GetViewInfo()const
	{
		return ViewInfo;
	}

private:
	const FViewInfo* ViewInfo = nullptr;

} VolumeFluidViewUniformBufferExtension;


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
		FRenderResource::ReleaseRHI();
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

static const TArray<uint32> VolumeIndices = { 0, 4, 5,  5, 1, 0,
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
		FRenderResource::ReleaseRHI();
		VolumeVertexBuffer.SafeRelease();
		VolumeIndexBuffer.SafeRelease();
	}
};

static TGlobalResource<FVolumeStreamBuffer> GVolumeStreamBuffer;

class FVolumeRayMarchStreamBuffer : public FRenderResource
{
public:
	FVertexBufferRHIRef RayMarchVertexBuffer;

	FIndexBufferRHIRef RayMarchIndexBuffer;

	virtual ~FVolumeRayMarchStreamBuffer() {}

	const uint16 Indices[6] = {0, 1, 2, 2, 1, 3};

	TResourceArray<FScreenVertex, VERTEXBUFFER_ALIGNMENT> Vertices;

	virtual void InitRHI() override
	{
		Vertices.SetNumUninitialized(4);
		Vertices[0].Position = FVector2D(-1.f, 1.f);
		Vertices[0].UV = FVector2D(0.f, 0.f);
		Vertices[1].Position = FVector2D(1.f, 1.f);
		Vertices[1].UV = FVector2D(1.f, 0.f);
		Vertices[2].Position = FVector2D(-1.f, -1.f);
		Vertices[2].UV = FVector2D(0.f, 1.f);
		Vertices[3].Position = FVector2D(1.f, -1.f);
		Vertices[3].UV = FVector2D(1.f, 1.f);

		FRHIResourceCreateInfo VertexCreateInfo(&Vertices);
		RayMarchVertexBuffer = RHICreateVertexBuffer(sizeof(FScreenVertex) * Vertices.Num(), BUF_Static, VertexCreateInfo);
		
		TResourceArray<uint16, INDEXBUFFER_ALIGNMENT> IndexBuffer;
		IndexBuffer.SetNumUninitialized(6);
		FMemory::Memcpy(IndexBuffer.GetData(), (const void*)Indices, sizeof(uint16) * GetIndexNum());
		FRHIResourceCreateInfo IndexCreateInfo(&IndexBuffer);
		RayMarchIndexBuffer = RHICreateIndexBuffer(sizeof(uint16), sizeof(uint16) * GetIndexNum(), BUF_Static, IndexCreateInfo);
	}

	virtual void ReleaseRHI() override
	{
		RayMarchVertexBuffer.SafeRelease();
		RayMarchIndexBuffer.SafeRelease();
	}

public:
	inline uint32 GetVertexNum()
	{
		return Vertices.Num();
	}

	inline uint32 GetIndexNum()
	{
		return UE_ARRAY_COUNT(Indices);
	}
};

static TGlobalResource<FVolumeRayMarchStreamBuffer> GVolumeRayMarchBuffer;

enum EVoulmeRenderDir
{
	BackwardVolume = 0,
	FrontVolume = 1
};

template<EVoulmeRenderDir Index>
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
		WorldView.Bind(Initializer.ParameterMap, TEXT("WorldView"));
	}

	void SetParameters(FRHICommandListImmediate& RHICmdList, const FMatrix& WVP, const FMatrix& WV)
	{
		
		SetShaderValue(RHICmdList, RHICmdList.GetBoundVertexShader(), WorldViewProjection, WVP);
		SetShaderValue(RHICmdList, RHICmdList.GetBoundVertexShader(), WorldView, WV);
	}

	LAYOUT_FIELD(FShaderParameter, WorldViewProjection)
	LAYOUT_FIELD(FShaderParameter, WorldView)
};

IMPLEMENT_SHADER_TYPE(template<>, FFluidVolumeVS<BackwardVolume>, TEXT("/FluidShaders/RenderFluidVolume.usf"), TEXT("VolumeBackVS"), SF_Vertex);
IMPLEMENT_SHADER_TYPE(template<>, FFluidVolumeVS<FrontVolume>, TEXT("/FluidShaders/RenderFluidVolume.usf"), TEXT("VolumeFrontVS"), SF_Vertex);

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

IMPLEMENT_SHADER_TYPE(template<>, FFluidVolumePS<BackwardVolume>, TEXT("/FluidShaders/RenderFluidVolume.usf"), TEXT("VolumeBackPS"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>, FFluidVolumePS<FrontVolume>, TEXT("/FluidShaders/RenderFluidVolume.usf"), TEXT("VolumeFrontPS"), SF_Pixel);

class FFluidVolumeQuadVS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FFluidVolumeQuadVS, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	FFluidVolumeQuadVS() {}

public:
	FFluidVolumeQuadVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		NearPlaneDistance.Bind(Initializer.ParameterMap, TEXT("NearPlaneDistance"));
		InvWorldViewProjection.Bind(Initializer.ParameterMap, TEXT("InvWorldViewProjection"));
		InvProjection.Bind(Initializer.ParameterMap, TEXT("InvProjection"));
	}

	void SetParameters(FRHICommandListImmediate& RHICmdList, float NearPlaneDist, const FMatrix& InvVolumeWorldViewProjection, const FMatrix& InInvProjection)
	{
		SetShaderValue(RHICmdList, RHICmdList.GetBoundVertexShader(), NearPlaneDistance, NearPlaneDist);
		SetShaderValue(RHICmdList, RHICmdList.GetBoundVertexShader(), InvWorldViewProjection, InvVolumeWorldViewProjection);
		SetShaderValue(RHICmdList, RHICmdList.GetBoundVertexShader(), InvProjection, InInvProjection);
	}

	LAYOUT_FIELD(FShaderParameter, NearPlaneDistance)
	LAYOUT_FIELD(FShaderParameter, InvWorldViewProjection)
	LAYOUT_FIELD(FShaderParameter, InvProjection)
};

IMPLEMENT_SHADER_TYPE(, FFluidVolumeQuadVS, TEXT("/FluidShaders/RenderFluidVolume.usf"), TEXT("VolumeRayMarchVS"), SF_Vertex);

class FFluidVolumeRayMarchPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FFluidVolumeRayMarchPS, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	FFluidVolumeRayMarchPS() {}

public:
	FFluidVolumeRayMarchPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		EyePosToVolume.Bind(Initializer.ParameterMap, TEXT("EyePosToVolume"));
		RayMarchDataTexture.Bind(Initializer.ParameterMap, TEXT("RayMarchDataTexture"));
		VolumeFluidColor.Bind(Initializer.ParameterMap, TEXT("VolumeFluidColor"));
		RayMarchSampler0.Bind(Initializer.ParameterMap, TEXT("RayMarchSampler0"));
		RayMarchSampler1.Bind(Initializer.ParameterMap, TEXT("RayMarchSampler1"));
		NearPlaneDistance.Bind(Initializer.ParameterMap, TEXT("NearPlaneDistance"));
		VolumeBoxScale.Bind(Initializer.ParameterMap, TEXT("VolumeBoxScale"));
		MaxVolumeBoxSize.Bind(Initializer.ParameterMap, TEXT("MaxVolumeBoxSize"));
		PerGridSize.Bind(Initializer.ParameterMap, TEXT("PerGridSize"));
		VolumeDimension.Bind(Initializer.ParameterMap, TEXT("VolumeDimension"));
	}

	void SetParameters(FRHICommandListImmediate& RHICmdList, 
						FVector EyePosToVol, 
						FRHITexture* RayDataTextureRHI, 
						FRHITexture* FluidColorTextureRHI,
						float NearPlane,
						float VolumeBoxSl,
						float MaxVolumeBoxEdgeSize,
						FVector PerGrid,
						FVector VolumeDim)
	{
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), EyePosToVolume, EyePosToVol);
		SetTextureParameter(RHICmdList, RHICmdList.GetBoundPixelShader(), RayMarchDataTexture, RayMarchSampler0, TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::CreateRHI(), RayDataTextureRHI);
		SetTextureParameter(RHICmdList, RHICmdList.GetBoundPixelShader(), VolumeFluidColor, RayMarchSampler1, TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::CreateRHI(), FluidColorTextureRHI);

		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), NearPlaneDistance, NearPlane);
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), VolumeBoxScale, VolumeBoxSl);
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), MaxVolumeBoxSize, MaxVolumeBoxEdgeSize);
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), PerGridSize, PerGrid);
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), VolumeDimension, VolumeDim);
	}

	LAYOUT_FIELD(FShaderParameter, EyePosToVolume)
	LAYOUT_FIELD(FShaderResourceParameter, RayMarchDataTexture)
	LAYOUT_FIELD(FShaderResourceParameter, VolumeFluidColor)
	LAYOUT_FIELD(FShaderResourceParameter, RayMarchSampler0)
	LAYOUT_FIELD(FShaderResourceParameter, RayMarchSampler1)
	LAYOUT_FIELD(FShaderParameter, NearPlaneDistance)
	LAYOUT_FIELD(FShaderParameter, VolumeBoxScale)
	LAYOUT_FIELD(FShaderParameter, MaxVolumeBoxSize)
	LAYOUT_FIELD(FShaderParameter, PerGridSize)
	LAYOUT_FIELD(FShaderParameter, VolumeDimension)
};

IMPLEMENT_SHADER_TYPE(, FFluidVolumeRayMarchPS, TEXT("/FluidShaders/RenderFluidVolume.usf"), TEXT("VolumeRayMarchPS"), SF_Pixel);

//class FDownSampleDepthPS : public FGlobalShader
//{
//	DECLARE_SHADER_TYPE(FDownSampleDepthPS, Global);
//
//	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
//	{
//		return true;
//	}
//
//	FDownSampleDepthPS() {}
//
//public:
//	FDownSampleDepthPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
//		: FGlobalShader(Initializer)
//	{
//		
//	}
//
//
//
//	LAYOUT_FIELD(FShaderResourceParameter, SceneDepthTexture)
//};
//
//IMPLEMENT_SHADER_TYPE(, FDownSampleDepthPS, TEXT("/Shaders/Private/RenderFluidVolume.usf"), TEXT("DepthDownSamplePS"), SF_Pixel);

void DrawVolumeBox(FRHICommandListImmediate& RHICmdList, TRefCountPtr<IPooledRenderTarget> RenderTarget, const FViewInfo& View, const FIntPoint& RTSize, float ViewportScale, const FTransform& VolumeTransform, ERHIFeatureLevel::Type FeatureLevel)
{	
	const float VolumeBoxScale = 200.f;
	FScaleMatrix VolumeScale = FScaleMatrix(FVector::OneVector * VolumeBoxScale);
	const FMatrix VolumeMatrix = VolumeTransform.ToMatrixWithScale();
	auto WorldViewProj = VolumeMatrix * View.ViewMatrices.GetViewProjectionMatrix();
	auto WorldView = VolumeMatrix * View.ViewMatrices.GetViewMatrix();
	FVector EyePosToVolume = VolumeMatrix.TransformPosition(View.ViewLocation);

	// #TODO
	FMatrix TranslatedMatrix = FScaleMatrix(FVector(1.f, 1.f, -1.f)) * FRotationMatrix(FRotator(0.f, 90.f, 90.f)) * FTranslationMatrix(FVector(0.f, 0.f, VolumeBoxScale));

	FRHIRenderPassInfo RPInfo(RenderTarget->GetRenderTargetItem().TargetableTexture, ERenderTargetActions::Clear_Store);
	RHICmdList.BeginRenderPass(RPInfo, TEXT("DrawVolumeBox"));
	{
		RHICmdList.SetViewport(0, 0, 0, ViewportScale * RTSize.X, ViewportScale * RTSize.Y, 1);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, ERasterizerCullMode::CM_CW>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(FeatureLevel);
		TShaderMapRef<FFluidVolumeVS<BackwardVolume>> BackVertexShader(ShaderMap);
		TShaderMapRef<FFluidVolumePS<BackwardVolume>> BackPixelShader(ShaderMap);

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GVolumeVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = BackVertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = BackPixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;
		
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
		BackVertexShader->SetParameters(RHICmdList, WorldViewProj, WorldView);

		RHICmdList.SetStreamSource(0, GVolumeStreamBuffer.VolumeVertexBuffer, 0);
		RHICmdList.DrawIndexedPrimitive(GVolumeStreamBuffer.VolumeIndexBuffer, 0, 0, VolumeVertices.Num(), 0, VolumeIndices.Num() / 3, 1);

		TShaderMapRef<FFluidVolumeVS<FrontVolume>> FrontVertexShader(ShaderMap);
		TShaderMapRef<FFluidVolumePS<FrontVolume>> FrontPixelShader(ShaderMap);
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_Zero, BO_ReverseSubtract, BF_One, BF_One>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, ERasterizerCullMode::CM_CCW>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = FrontVertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = FrontPixelShader.GetPixelShader();
		
		FrontVertexShader->SetParameters(RHICmdList, WorldViewProj, WorldView);
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		RHICmdList.SetStreamSource(0, GVolumeStreamBuffer.VolumeVertexBuffer, 0);
		RHICmdList.DrawIndexedPrimitive(GVolumeStreamBuffer.VolumeIndexBuffer, 0, 0, VolumeVertices.Num(), 0, VolumeIndices.Num() / 3, 1);
	}
	RHICmdList.EndRenderPass();
}

void RayMarchFluidVolume(FRHICommandListImmediate& RHICmdList, FTexture2DRHIRef RayMarchRT, FTextureRHIRef FluidColor, TRefCountPtr<IPooledRenderTarget> RayMarchData, const FViewInfo& View, const FIntPoint& RTSize, float ViewportScale, const FIntVector FluidVolumeSize, const FTransform& VolumeTransform, ERHIFeatureLevel::Type FeatureLevel)
{
	FRHIRenderPassInfo RPInfo(RayMarchRT, ERenderTargetActions::Clear_Store);
	RHICmdList.BeginRenderPass(RPInfo, TEXT("RayMarchFluid"));
	{
		RHICmdList.SetViewport(0, 0, 0, ViewportScale * RTSize.X, ViewportScale * RTSize.Y, 1);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI();

		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(FeatureLevel);
		TShaderMapRef<FFluidVolumeQuadVS> VertexShader(ShaderMap);
		TShaderMapRef<FFluidVolumeRayMarchPS> PixelShader(ShaderMap);

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GScreenVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		// Set Shader Params
		const FMatrix InverseVolumeTransformMatrix = VolumeTransform.ToInverseMatrixWithScale();
		const FVector EyePosInVolume = InverseVolumeTransformMatrix.TransformPosition(View.ViewLocation);
		const float NearPlane = View.NearClippingDistance;
		const FMatrix InvVolumeViewProjection = View.ViewMatrices.GetInvViewProjectionMatrix() * InverseVolumeTransformMatrix;
		const FMatrix InvProjection = View.ViewMatrices.GetInvProjectionMatrix();

		FVector VolumeScale = VolumeTransform.GetScale3D();
		FVector VolumeDim(FluidVolumeSize);
		
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		VertexShader->SetParameters(RHICmdList, NearPlane, InvVolumeViewProjection, InvProjection);
		PixelShader->SetParameters(RHICmdList, EyePosInVolume, RayMarchData->GetRenderTargetItem().TargetableTexture, FluidColor, NearPlane, VolumeScale.GetAbsMax(), (float)FluidVolumeSize.GetMax(), VolumeDim.Reciprocal(), VolumeDim);

		RHICmdList.SetStreamSource(0, GVolumeRayMarchBuffer.RayMarchVertexBuffer, 0);
		RHICmdList.DrawIndexedPrimitive(GVolumeRayMarchBuffer.RayMarchIndexBuffer, 0, 0, GVolumeRayMarchBuffer.GetVertexNum(), 0, GVolumeRayMarchBuffer.GetIndexNum() / 3, 1);
	}
	RHICmdList.EndRenderPass();
}

void RenderFluidVolume(FRHICommandListImmediate& RHICmdList, const FVolumeFluidProxy& ResourceParam, FTextureRHIRef FluidColor, const FViewInfo* InView)
{
	GetRendererModule().RegisterPersistentViewUniformBufferExtension(&VolumeFluidViewUniformBufferExtension);
	//if (!VolumeFluidViewUniformBufferExtension.GetViewInfo() && !InView)
		//return;

	const FViewInfo& View = InView ? *InView : *VolumeFluidViewUniformBufferExtension.GetViewInfo();
	const float ViewportScale = 0.5f;
	
	//UE_LOG(LogTemp, Log, TEXT("Raymarch RT size: %s"), *ResourceParam.RayMarchRTSize.ToString());
	if(ResourceParam.RayMarchRTSize.X <= 0 || ResourceParam.RayMarchRTSize.Y <= 0)
		return;

	FPooledRenderTargetDesc FluidVloumeDesc = FPooledRenderTargetDesc::CreateVolumeDesc(ResourceParam.FluidVolumeSize.X, ResourceParam.FluidVolumeSize.Y, ResourceParam.FluidVolumeSize.Z, EPixelFormat::PF_A32B32G32R32F, FClearValueBinding::None, ETextureCreateFlags::TexCreate_None, ETextureCreateFlags::TexCreate_UAV | ETextureCreateFlags::TexCreate_ShaderResource, false);
	FPooledRenderTargetDesc RayMarchDesc = FPooledRenderTargetDesc::Create2DDesc(FIntPoint(ViewportScale * ResourceParam.RayMarchRTSize.X, ViewportScale * ResourceParam.RayMarchRTSize.Y), EPixelFormat::PF_A32B32G32R32F, FClearValueBinding::Black, ETextureCreateFlags::TexCreate_None, ETextureCreateFlags::TexCreate_RenderTargetable | ETextureCreateFlags::TexCreate_ShaderResource, false);
	//FPooledRenderTargetDesc RayMarchDesc = FPooledRenderTargetDesc::Create2DDesc(FIntPoint(View.ViewRect.Width(), View.ViewRect.Height()), EPixelFormat::PF_A32B32G32R32F, FClearValueBinding::Black, ETextureCreateFlags::TexCreate_None, ETextureCreateFlags::TexCreate_RenderTargetable | ETextureCreateFlags::TexCreate_ShaderResource, false);
	TRefCountPtr<IPooledRenderTarget> ColorTexture3D_0, RayMarchData, RayMarchResult;
	GRenderTargetPool.FindFreeElement(RHICmdList, FluidVloumeDesc, ColorTexture3D_0, TEXT("ColorTexture3D_0"));
	GRenderTargetPool.FindFreeElement(RHICmdList, RayMarchDesc, RayMarchData, TEXT("RayMarchData"));
	GRenderTargetPool.FindFreeElement(RHICmdList, RayMarchDesc, RayMarchResult, TEXT("RayMarchResult"));

	const float VolumeBoxScale = 200.f;
	FTransform VolumeTransform(FRotator::ZeroRotator, FVector::UpVector * 200.f, FVector::OneVector * VolumeBoxScale);

	DrawVolumeBox(RHICmdList, RayMarchData, View, ResourceParam.RayMarchRTSize, ViewportScale, ResourceParam.FluidVolumeTransform, ResourceParam.FeatureLevel);

	FRHITexture* TranslationTextures[] = {RayMarchData->GetRenderTargetItem().ShaderResourceTexture->GetTexture2D(), FluidColor->GetTexture3D()};
	RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, TranslationTextures, UE_ARRAY_COUNT(TranslationTextures));

	if (ResourceParam.TextureRenderTargetResource)
	{
		FTexture2DRHIRef FluidRT = ResourceParam.TextureRenderTargetResource->GetRenderTargetTexture();
		RayMarchFluidVolume(RHICmdList, FluidRT, FluidColor, RayMarchData, View, ResourceParam.RayMarchRTSize, ViewportScale, ResourceParam.FluidVolumeSize, ResourceParam.FluidVolumeTransform, ResourceParam.FeatureLevel);
	}

	FRHICopyTextureInfo CopyInfo;
	CopyInfo.Size = FIntVector(ViewportScale * View.ViewRect.Width(), ViewportScale * View.ViewRect.Height(), 1);
	/*if (ResourceParam.TextureRenderTargetResource)
	{
		RHICmdList.CopyTexture(RayMarchResult->GetRenderTargetItem().TargetableTexture, ResourceParam.TextureRenderTargetResource->GetRenderTargetTexture(), CopyInfo);
	}*/

	if (ResourceParam.TextureResource)
	{
		FTexture2DResource* Resource2D = static_cast<FTexture2DResource*>(ResourceParam.TextureResource);
		FTexture2DRHIRef Texture2DRef = RayMarchResult->GetRenderTargetItem().TargetableTexture->GetTexture2D();
		//Resource2D->UpdateTexture(Texture2DRef, 0);
	}
}