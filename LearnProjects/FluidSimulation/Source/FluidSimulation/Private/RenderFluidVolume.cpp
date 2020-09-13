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
		
	}
};

IMPLEMENT_SHADER_TYPE(, FFluidVolumeBackPS, TEXT("/Shaders/Private/Fluid3D.usf"), TEXT("VolumeBackPS"), SF_Pixel);

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

IMPLEMENT_SHADER_TYPE(, FFluidVolumeFrontPS, TEXT("/Shaders/Private/Fluid3D.usf"), TEXT("VolumeFrontPS"), SF_Pixel);


void DrawVolumeBox(FRHICommandListImmediate& RHICmdList, TRefCountPtr<IPooledRenderTarget> RenderTarget, const FViewInfo& View)
{
	static TArray<FVector> Vertices = { FVector(0, 0, 0), 
										FVector(0, 1, 0),
										FVector(1, 0 ,0),
										FVector(1, 1, 0),
										FVector(0, 0, 1),
										FVector(0, 1, 1),
										FVector(1, 0, 1),
										FVector(1, 1, 1)};

	static TArray<int32> Indices = {0, 4, 5,  5, 1, 0, 
									1, 5, 3,  5, 7, 3,
									3, 7, 6,  6, 2, 3,
									6, 4, 0,  0, 2, 6,
									0, 1, 3,  3, 2, 0,
									4, 6, 7,  7, 5, 4};

	
	FRHIRenderPassInfo RPInfo(RenderTarget->GetRenderTargetItem().TargetableTexture, ERenderTargetActions::Clear_DontStore);
	RHICmdList.BeginRenderPass(RPInfo, TEXT("DrawVolumeBox"));
	{
		/*FIntRect SrcRect = View.ViewRect;
		FIntRect DestRect = View.ViewRect;

		RHICmdList.SetViewport()

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		Context.RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

		TShaderMapRef<FPostProcessVS> VertexShader(View.ShaderMap);
		TShaderMapRef<FDistortionMergePS_ES2> PixelShader(View.ShaderMap);

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		SetGraphicsPipelineState(Context.RHICmdList, GraphicsPSOInit);*/
	}
	RHICmdList.EndRenderPass();
}

void RenderFluidVolume(FRHICommandListImmediate& RHICmdList, FIntVector FluidVolumeSize, ERHIFeatureLevel::Type FeatureLevel)
{
	FPooledRenderTargetDesc FluidVloumeDesc = FPooledRenderTargetDesc::CreateVolumeDesc(FluidVolumeSize.X, FluidVolumeSize.Y, FluidVolumeSize.Z, EPixelFormat::PF_A32B32G32R32F, FClearValueBinding::None, ETextureCreateFlags::TexCreate_None, ETextureCreateFlags::TexCreate_UAV | ETextureCreateFlags::TexCreate_ShaderResource, false);
	TRefCountPtr<IPooledRenderTarget> ColorTexture3D_0;
	GRenderTargetPool.FindFreeElement(RHICmdList, FluidVloumeDesc, ColorTexture3D_0, TEXT("ColorTexture3D_0"));
	

}