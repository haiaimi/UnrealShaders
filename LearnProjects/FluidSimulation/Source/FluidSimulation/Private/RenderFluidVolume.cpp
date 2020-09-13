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
	DECLARE_SHADER_TYPE(FFluidVolumeBack, Global);

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


void RenderFluidVolume(FRHICommandListImmediate& RHICmdList, FIntVector FluidVolumeSize, ERHIFeatureLevel::Type FeatureLevel)
{
	FPooledRenderTargetDesc FluidVloumeDesc = FPooledRenderTargetDesc::CreateVolumeDesc(FluidVolumeSize.X, FluidVolumeSize.Y, FluidVolumeSize.Z, EPixelFormat::PF_A32B32G32R32F, FClearValueBinding::None, ETextureCreateFlags::TexCreate_None, ETextureCreateFlags::TexCreate_UAV | ETextureCreateFlags::TexCreate_ShaderResource, false);
	TRefCountPtr<IPooledRenderTarget> ColorTexture3D_0;
	GRenderTargetPool.FindFreeElement(RHICmdList, FluidVloumeDesc, ColorTexture3D_0, TEXT("ColorTexture3D_0"));
	

}