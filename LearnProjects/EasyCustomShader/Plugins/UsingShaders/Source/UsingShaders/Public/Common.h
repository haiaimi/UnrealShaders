#pragma once
#include "RHI.h"

struct FUVertexInput
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
		//设置顶点输入布局
		FVertexDeclarationElementList Elements;
		uint32 Stride = sizeof(FUVertexInput);   //步长
		Elements.Add(FVertexElement(0, STRUCT_OFFSET(FUVertexInput, Position), EVertexElementType::VET_Float4, 0, Stride));  //第一个参数Stream应该是配合Instance来使用
		Elements.Add(FVertexElement(0, STRUCT_OFFSET(FUVertexInput, UV), EVertexElementType::VET_Float2, 1, Stride));
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
	FVertexBufferRHIRef VertexBufferRHI = RHICreateVertexBuffer(VertexDataStride * NumVertices, BUF_Volatile, CreateInfo);        //Buf类型是改动类型，没帧写入，所以声明为BUF_Volatile
	void* VoidPtr = RHILockVertexBuffer(VertexBufferRHI, 0, VertexDataStride * NumVertices, RLM_WriteOnly);        //lock顶点缓冲buffer，并且只能写入
	FPlatformMemory::Memcpy(VoidPtr, VertexData, VertexDataStride * NumVertices);       //传入顶点数据
	RHIUnlockVertexBuffer(VertexBufferRHI);    //unlock顶点缓冲

	//同上创建一个索引缓冲
	FIndexBufferRHIRef IndexBufferRHI = RHICreateIndexBuffer(IndexDataStride, IndexDataStride * NumIndices, BUF_Volatile, CreateInfo);
	void* VoidPtr2 = RHILockIndexBuffer(IndexBufferRHI, 0, IndexDataStride * NumIndices, RLM_WriteOnly);
	FPlatformMemory::Memcpy(VoidPtr2, IndexData, IndexDataStride * NumIndices);
	RHIUnlockIndexBuffer(IndexBufferRHI);

	RHICmdList.SetStreamSource(0, VertexBufferRHI, 0);     //针对不同的图形接口设置顶点缓冲
	RHICmdList.DrawIndexedPrimitive(IndexBufferRHI, MinVertexIndex, 0, NumVertices, 0, NumPrimitives, 1);        //绘制图元

	IndexBufferRHI.SafeRelease();
	VertexBufferRHI.SafeRelease();
}