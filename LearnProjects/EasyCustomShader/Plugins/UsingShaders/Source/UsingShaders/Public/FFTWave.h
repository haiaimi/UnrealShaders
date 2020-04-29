#pragma once
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Classes/Kismet/BlueprintFunctionLibrary.h"
#include "RHIResources.h"

///** Encapsulates a GPU read/write texture 2D with its UAV and SRV. */
struct FTextureRWBuffer2D
{
	FTexture2DRHIRef Buffer;
	FUnorderedAccessViewRHIRef UAV;
	FShaderResourceViewRHIRef SRV;
	uint32 NumBytes;

	FTextureRWBuffer2D()
		: NumBytes(0)
	{}

	~FTextureRWBuffer2D()
	{
		Release();
	}

	// @param AdditionalUsage passed down to RHICreateVertexBuffer(), get combined with "BUF_UnorderedAccess | BUF_ShaderResource" e.g. BUF_Static
	const static uint32 DefaultTextureInitFlag = TexCreate_ShaderResource | TexCreate_UAV;
	void Initialize(const uint32 BytesPerElement, const uint32 SizeX, const uint32 SizeY, const EPixelFormat Format, uint32 Flags = DefaultTextureInitFlag)
	{
		check(GMaxRHIFeatureLevel == ERHIFeatureLevel::SM5
			|| IsVulkanPlatform(GMaxRHIShaderPlatform)
			|| IsMetalPlatform(GMaxRHIShaderPlatform)
			|| (GMaxRHIFeatureLevel == ERHIFeatureLevel::ES3_1 && GSupportsResourceView)
		);

		NumBytes = SizeX * SizeY * BytesPerElement;

		FRHIResourceCreateInfo CreateInfo;
		Buffer = RHICreateTexture2D(
			SizeX, SizeY, Format, //PF_R32_FLOAT,
			/*NumMips=*/ 1,
			1,
			Flags,
			/*BulkData=*/ CreateInfo);


		UAV = RHICreateUnorderedAccessView(Buffer, 0);
		SRV = RHICreateShaderResourceView(Buffer, 0);
	}

	void AcquireTransientResource()
	{
		RHIAcquireTransientResource(Buffer);
	}
	void DiscardTransientResource()
	{
		RHIDiscardTransientResource(Buffer);
	}

	void Release()
	{
		int32 BufferRefCount = Buffer ? Buffer->GetRefCount() : -1;

		if (BufferRefCount == 1)
		{
			DiscardTransientResource();
		}

		NumBytes = 0;
		Buffer.SafeRelease();
		UAV.SafeRelease();
		SRV.SafeRelease();
	}
};
