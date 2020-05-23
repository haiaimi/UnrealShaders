// Fill out your copyright notice in the Description page of Project Settings.


#include "GenerateDistanceFieldTexture_GPU.h"
#include "Engine/StaticMesh.h"

FGenerateDistanceFieldTexture_GPU::FGenerateDistanceFieldTexture_GPU()
{
}

FGenerateDistanceFieldTexture_GPU::~FGenerateDistanceFieldTexture_GPU()
{
}

void GenerateMeshMaskTexture(class UStaticMesh* StaticMesh, float StartDegree)
{
	if (!StaticMesh)return;

	const FStaticMeshLODResources& LODModel = StaticMesh->RenderData->LODResources[0];
	const FBoxSphereBounds& Bounds = StaticMesh->RenderData->Bounds;
	const FPositionVertexBuffer& PositionVertexBuffer = LODModel.VertexBuffers.PositionVertexBuffer;
	FIndexArrayView Indices = LODModel.IndexBuffer.GetArrayView();
}
