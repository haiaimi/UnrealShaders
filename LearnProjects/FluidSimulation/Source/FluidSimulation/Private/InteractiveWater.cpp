// Fill out your copyright notice in the Description page of Project Settings.


#include "InteractiveWater.h"
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
#include "EngineGlobals.h"
#include "EngineModule.h"
#include "TextureResource.h"
#include "SceneViewExtension.h"
#include "Engine/TextureRenderTarget.h"

class FInteractiveWaterStreamBuffer : public FRenderResource
{
public:
	FVertexBufferRHIRef VertexBuffer;

	FIndexBufferRHIRef IndexBuffer;

	virtual ~FInteractiveWaterStreamBuffer() {}

	const uint16 Indices[6] = { 0, 1, 2, 2, 1, 3 };

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
		VertexBuffer = RHICreateVertexBuffer(sizeof(FScreenVertex) * Vertices.Num(), BUF_Static, VertexCreateInfo);

		TResourceArray<uint16, INDEXBUFFER_ALIGNMENT> TempIndexBuffer;
		TempIndexBuffer.SetNumUninitialized(6);
		FMemory::Memcpy(TempIndexBuffer.GetData(), (const void*)Indices, sizeof(uint16) * GetIndexNum());
		FRHIResourceCreateInfo IndexCreateInfo(&TempIndexBuffer);
		IndexBuffer = RHICreateIndexBuffer(sizeof(uint16), sizeof(uint16) * GetIndexNum(), BUF_Static, IndexCreateInfo);
	}

	virtual void ReleaseRHI() override
	{
		VertexBuffer.SafeRelease();
		IndexBuffer.SafeRelease();
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

static TGlobalResource<FInteractiveWaterStreamBuffer> GInteractiveWaterStreamBuffer;

class FCommonQuadVS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FCommonQuadVS, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	FCommonQuadVS() {}

public:
	FCommonQuadVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{

	}
};

IMPLEMENT_SHADER_TYPE(, FCommonQuadVS, TEXT("/Shaders/Private/InteractiveWater.usf"), TEXT("CommonQuadVS"), SF_Vertex);

class FApplyForcePS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FApplyForcePS, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	FApplyForcePS() {}

public:
	FApplyForcePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		ForcePos.Bind(Initializer.ParameterMap, TEXT("ForcePos"));
		Radius.Bind(Initializer.ParameterMap, TEXT("Radius"));
		Strength.Bind(Initializer.ParameterMap, TEXT("Strength"));
		FieldOffset.Bind(Initializer.ParameterMap, TEXT("FieldOffset"));
		HeightField.Bind(Initializer.ParameterMap, TEXT("HeightField"));
		WaterSampler.Bind(Initializer.ParameterMap, TEXT("WaterSampler"));
	}

	void SetParameters(FRHICommandListImmediate& RHICmdList, FVector2D InForcePos, float InRadius, float InStrength, FVector2D InFieldOffset, FRHITexture* InHeightField)
	{
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), ForcePos, InForcePos);
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), Radius, InRadius);
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), Strength, InStrength);
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), FieldOffset, InFieldOffset);
		SetTextureParameter(RHICmdList, RHICmdList.GetBoundPixelShader(), HeightField, WaterSampler, TStaticSamplerState<SF_Bilinear, AM_Border, AM_Border, AM_Border>::CreateRHI(), InHeightField);
	}

private:
	LAYOUT_FIELD(FShaderParameter, ForcePos)
	LAYOUT_FIELD(FShaderParameter, Radius)
	LAYOUT_FIELD(FShaderParameter, Strength)
	LAYOUT_FIELD(FShaderParameter, FieldOffset)
	LAYOUT_FIELD(FShaderResourceParameter, HeightField)
	LAYOUT_FIELD(FShaderResourceParameter, WaterSampler)
};

IMPLEMENT_SHADER_TYPE(, FApplyForcePS, TEXT("/Shaders/Private/InteractiveWater.usf"), TEXT("ApplyForcePS"), SF_Pixel);

class FUpdateHeightFieldPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FUpdateHeightFieldPS, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	FUpdateHeightFieldPS() {}

public:
	FUpdateHeightFieldPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		GridDelta.Bind(Initializer.ParameterMap, TEXT("GridDelta"));
		FieldOffset.Bind(Initializer.ParameterMap, TEXT("FieldOffset"));
		HeightField.Bind(Initializer.ParameterMap, TEXT("HeightField"));
		WaterSampler.Bind(Initializer.ParameterMap, TEXT("WaterSampler"));
	}

	void SetParameters(FRHICommandListImmediate& RHICmdList, FVector2D InGridDelta, FVector2D InFieldOffset, FRHITexture* InHeightField)
	{
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), GridDelta, InGridDelta);
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), FieldOffset, InFieldOffset);
		SetTextureParameter(RHICmdList, RHICmdList.GetBoundPixelShader(), HeightField, WaterSampler, TStaticSamplerState<SF_Bilinear, AM_Border, AM_Border, AM_Border>::CreateRHI(), InHeightField);
	}

private:
	LAYOUT_FIELD(FShaderParameter, GridDelta)
	LAYOUT_FIELD(FShaderParameter, FieldOffset)
	LAYOUT_FIELD(FShaderResourceParameter, HeightField)
	LAYOUT_FIELD(FShaderResourceParameter, WaterSampler)
};

IMPLEMENT_SHADER_TYPE(, FUpdateHeightFieldPS, TEXT("/Shaders/Private/InteractiveWater.usf"), TEXT("UpdateHeightFieldPS"), SF_Pixel);


FInteractiveWater::FInteractiveWater(/*, ERHIFeatureLevel::Type InFeatureLevel*/):
	FRenderResource()
{
	Switcher = 0;
	TimeAccumlator = 0.f;
	ForceTimeAccumlator = 0.f;
	SimulateTimePerSecond = 60;
	bShouldApplyForce = false;

	HeightMapRTs[0] = nullptr;
	HeightMapRTs[1] = nullptr;
	//FeatureLevel = InFeatureLevel;
}

FInteractiveWater::~FInteractiveWater()
{
	
}

void FInteractiveWater::SetResource(class UTextureRenderTarget* Height01, class UTextureRenderTarget* Height02)
{
	HeightMapRTs[0] = Height01->GameThread_GetRenderTargetResource();
	HeightMapRTs[1] = Height02->GameThread_GetRenderTargetResource();

	HeightMapRTs_GameThread[0] = Height01;
	HeightMapRTs_GameThread[1] = Height02;

	RectSize = HeightMapRTs[0]->GetSizeXY();
}

void FInteractiveWater::UpdateWater()
{
	if (ForceTimeAccumlator >= 0.1f && MoveDir.Size() > 0.f)
	{
		//ApplyForce_RenderThread();
		ForceTimeAccumlator = 0.f;
	}

	//if (TimeAccumlator > 1.f / SimulateTimePerSecond)
	{
		if(MoveDir.Size() > 0.f)
			ApplyForce_RenderThread();
		UpdateHeightField_RenderThread();
		TimeAccumlator = TimeAccumlator - 1.f / SimulateTimePerSecond;
	}

	TimeAccumlator += DeltaTime;
	ForceTimeAccumlator += DeltaTime;
}

void FInteractiveWater::ApplyForce_RenderThread()
{
	check(IsInRenderingThread());

	FRHICommandListImmediate& RHICmdList = GetImmediateCommandList_ForRenderCommand();

	FRHIRenderPassInfo RPInfo(GetCurrentTarget(), ERenderTargetActions::Load_Store);
	RHICmdList.BeginRenderPass(RPInfo, TEXT("ApplyForce"));
	{
		RHICmdList.SetViewport(1, 1, 0, RectSize.X - 1, RectSize.Y - 1, 1);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false>::GetRHI();

		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GetFeatureLevel());
		TShaderMapRef<FCommonQuadVS> VertexShader(ShaderMap);
		TShaderMapRef<FApplyForcePS> PixelShader(ShaderMap);

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GScreenVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		// Set Shader Params
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
		
		FVector2D CurDelta = MoveDir;
		//Offset = Offset + (CurDelta - MoveDir.GetSafeNormal() * DeltaTime * 0.3f);

		const float OffsetTolerance = 0.15f;
		ForcePos += CurDelta;
		FVector2D Offset = CurDelta * 0.5f;
		float SubX = FMath::Abs(ForcePos.X - 0.5f) - OffsetTolerance;
		if (SubX > 0.f)
		{
			Offset.X = (ForcePos.X - 0.5f) > 0.f ? SubX : -SubX;
		}

		float SubY = FMath::Abs(ForcePos.Y - 0.5f) - OffsetTolerance;
		if (SubY > 0.f)
		{
			Offset.Y = (ForcePos.Y - 0.5f) > 0.f ? SubY : -SubY;
		}

		ForcePos -= Offset;
		ForcePos = FVector2D(FMath::Frac(ForcePos.X), FMath::Frac(ForcePos.Y));
		//FVector2D Offset = (MoveDir * DeltaTime * 0.5f);
		////UE_LOG(LogTemp, Log, TEXT("%4.4f"), MoveDir.X);
		//UE_LOG(LogTemp, Log, TEXT("Offset: %s"), *Offset.ToString());
		PixelShader->SetParameters(RHICmdList, ForcePos, 0.008f, 1.f, /*(MoveDir * DeltaTime * 0.5f)*/Offset, GetPreHeightField());

		RHICmdList.SetStreamSource(0, GInteractiveWaterStreamBuffer.VertexBuffer, 0);
		RHICmdList.DrawIndexedPrimitive(GInteractiveWaterStreamBuffer.IndexBuffer, 0, 0, GInteractiveWaterStreamBuffer.GetVertexNum(), 0, GInteractiveWaterStreamBuffer.GetIndexNum() / 3, 1);
	}
	RHICmdList.EndRenderPass();

	Switcher += 1;
}

void FInteractiveWater::UpdateHeightField_RenderThread()
{
	check(IsInRenderingThread());

	FRHICommandListImmediate& RHICmdList = GetImmediateCommandList_ForRenderCommand();

	FRHIRenderPassInfo RPInfo(GetCurrentTarget(), ERenderTargetActions::Load_Store);
	RHICmdList.BeginRenderPass(RPInfo, TEXT("UpdateWaterHeight"));
	{
		RHICmdList.SetViewport(1, 1, 0, RectSize.X - 1, RectSize.Y - 1, 1);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false>::GetRHI();
		
		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GetFeatureLevel());
		TShaderMapRef<FCommonQuadVS> VertexShader(ShaderMap);
		TShaderMapRef<FUpdateHeightFieldPS> PixelShader(ShaderMap);

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GScreenVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		// Set Shader Params
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
		FVector2D Sub = ForcePos - FVector2D(0.5f, 0.5f);
		float ClampX = FMath::Clamp(Sub.X, -0.1f, 0.1f);
		float ClampY = FMath::Clamp(Sub.Y, -0.1f, 0.1f);
		FVector2D Offset = FVector2D(ClampX, ClampY) - Sub;
		//ForcePos += Offset;

		
		PixelShader->SetParameters(RHICmdList, FVector2D(1.f / RectSize.X, 1.f / RectSize.Y), Offset, GetPreHeightField());

		RHICmdList.SetStreamSource(0, GInteractiveWaterStreamBuffer.VertexBuffer, 0);
		RHICmdList.DrawIndexedPrimitive(GInteractiveWaterStreamBuffer.IndexBuffer, 0, 0, GInteractiveWaterStreamBuffer.GetVertexNum(), 0, GInteractiveWaterStreamBuffer.GetIndexNum() / 3, 1);
	}
	RHICmdList.EndRenderPass();

	Switcher += 1;
}

bool FInteractiveWater::IsResourceValid()
{
	return HeightMapRTs[0] && HeightMapRTs[1];
}

void FInteractiveWater::ReleaseResource()
{
	HeightMapRTs[0] = nullptr;
	HeightMapRTs[1] = nullptr;
}

class FRHITexture* FInteractiveWater::GetCurrentTarget()
{
	return HeightMapRTs[Switcher]->GetRenderTargetTexture();
}

FVector2D FInteractiveWater::GetRoleUV(FVector2D CurDir)
{
	const float OffsetTolerance = 0.15f;
	FVector2D DestUV = ForcePos + CurDir;
	FVector2D Offset = CurDir * 0.5f;
	float SubX = FMath::Abs(DestUV.X - 0.5f) - OffsetTolerance;
	if (SubX > 0.f)
	{
		Offset.X = (DestUV.X - 0.5f) > 0.f ? SubX : -SubX;
	}

	float SubY = FMath::Abs(DestUV.Y - 0.5f) - OffsetTolerance;
	if (SubY > 0.f)
	{
		Offset.Y = (DestUV.Y - 0.5f) > 0.f ? SubY : -SubY;
	}

	DestUV = DestUV - Offset;
	return FVector2D(FMath::Frac(DestUV.X), FMath::Frac(DestUV.Y));
}

class UTextureRenderTarget* FInteractiveWater::GetCurrentTarget_GameThread()
{
	check(IsInGameThread());

	return HeightMapRTs_GameThread[(Switcher + 1) & 1];
}

class UTextureRenderTarget* FInteractiveWater::GetCurrentTarget_GameThread(FVector2D CurDir)
{
	check(IsInGameThread());
	
	return HeightMapRTs_GameThread[CurDir.Size() > 0.f ? (Switcher + 1) & 1 : Switcher];
}

class FRHITexture* FInteractiveWater::GetPreHeightField()
{
	return HeightMapRTs[(Switcher + 1) & 1]->GetRenderTargetTexture();
}

