#pragma once
// This file was used to bake the data of Precomputed Radiance Transfer

#include "CoreMinimal.h"
#include "RHI.h"
#include "HitProxies.h"
#include "ShaderBaseClasses.h"
#include "MeshPassProcessor.h"
#include "ShaderParameterMacros.h"
#include "UniformBuffer.h"
#include "MeshDrawCommands.h"

class FPRTDiffusebakePassMeshProcessor : public FMeshPassProcessor
{
public:

	FPRTDiffusebakePassMeshProcessor(const FScene* Scene,
		ERHIFeatureLevel::Type InFeatureLevel,
		const FSceneView* InViewIfDynamicMeshCommand,
		const FMeshPassProcessorRenderState& InPassDrawRenderState,
		FMeshPassDrawListContext* InDrawListContext,
		ERasterizerCullMode CullMode);

	virtual void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) override final;

private:
	void Process(
		const FMeshBatch& MeshBatch,
		uint64 BatchElementMask,
		int32 StaticMeshId,
		EBlendMode BlendMode,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
		const FMaterial& RESTRICT MaterialResource);

	FMeshPassProcessorRenderState PassDrawRenderState;

	ERasterizerCullMode TargetCullMode;
};

