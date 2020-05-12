// Fill out your copyright notice in the Description page of Project Settings.


#include "GenerateDistanceFieldTexture.h"
#include "Math/RandomStream.h"
#include "StaticMeshResources.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"

#include <embree2/rtcore.h>
#include <embree2/rtcore_ray.h>

static void GenerateHemisphereSamples(int32 NumThetaSteps, int32 NumPhiSteps, FRandomStream& RandomStream, TArray<FVector4>& Samples)
{
	Samples.Reserve(NumThetaSteps * NumPhiSteps);

	for (int32 ThetaIndex = 0; ThetaIndex < NumThetaSteps; ++ThetaIndex)
	{
		for (int32 PhiIndex = 0; PhiIndex < NumPhiSteps; ++PhiIndex)
		{
			const float U1 = RandomStream.GetFraction();
			const float U2 = RandomStream.GetFraction();

			const float Fraction1 = (ThetaIndex + U1) / (float)NumThetaSteps;
			const float Fraction2 = (PhiIndex + U2) / (float)NumPhiSteps;

			// The sample is normalized, so the radius of sphere is 1
			// We can see Fraction1 as height (z)
			const float R = FMath::Sqrt(1.0f - Fraction1 * Fraction1);

			// Current Phi (0 - 2*PI)
			const float Phi = 2.0f * (float)PI * Fraction2;
			// Convert to Cartesian
			Samples.Add(FVector4(FMath::Cos(Phi) * R, FMath::Sin(Phi) * R, Fraction1));
		}
	}
}

FGenerateDistanceFieldTexture::FGenerateDistanceFieldTexture()
{
}

struct FEmbreeTriangleDesc
{
	int16 ElementIndex;
};

// Mapping between Embree Geometry Id and engine Mesh/LOD Id
struct FEmbreeGeometry
{
	TArray<FEmbreeTriangleDesc> TriangleDescs; // The material ID of each triangle.
};

struct FEmbreeRay : public RTCRay
{
	FEmbreeRay() :
		ElementIndex(-1)
	{
		u = v = 0;
		time = 0;
		mask = 0xFFFFFFFF;
		geomID = -1;
		instID = -1;
		primID = -1;
	}

	// Additional Outputs.
	int32 ElementIndex; // Material Index
};

void EmbreeFilterFunc(void* UserPtr, RTCRay& InRay)
{
	FEmbreeGeometry* EmbreeGeometry = (FEmbreeGeometry*)UserPtr;
	FEmbreeRay& EmbreeRay = (FEmbreeRay&)InRay;
	FEmbreeTriangleDesc Desc = EmbreeGeometry->TriangleDescs[InRay.primID];

	EmbreeRay.ElementIndex = Desc.ElementIndex;
}


void FGenerateDistanceFieldTexture::GenerateDistanceFieldTexture(UStaticMesh* GenerateStaticMesh, FIntVector DistanceFieldDimension, UTexture2D* TargetTex)
{
	if (!GenerateStaticMesh)return;

	const FStaticMeshLODResources& LODModel = GenerateStaticMesh->RenderData->LODResources[0];
	const FBoxSphereBounds& Bounds = GenerateStaticMesh->RenderData->Bounds;
	const FPositionVertexBuffer& PositionVertexBuffer = LODModel.VertexBuffers.PositionVertexBuffer;
	FIndexArrayView Indices = LODModel.IndexBuffer.GetArrayView();

	const int32 NumVoxelDistanceSamples = 1200;
	TArray<FVector4> SampleDirections;
	const int32 NumThetaSteps = FMath::TruncToInt(FMath::Sqrt(NumVoxelDistanceSamples / (2.0f * (float)PI)));
	const int32 NumPhiSteps = FMath::TruncToInt(NumThetaSteps * (float)PI);
	FRandomStream RandomStream(0);
	// Compute the upper samples
	GenerateHemisphereSamples(NumThetaSteps, NumPhiSteps, RandomStream, SampleDirections);
	// Compute under samples
	TArray<FVector4> DownSampleDirections;
	GenerateHemisphereSamples(NumThetaSteps, NumPhiSteps, RandomStream, DownSampleDirections);
	for (auto& Iter : DownSampleDirections)
	{
		Iter.Z *= -1.f;
		SampleDirections.Add(Iter);
	}

	// Get blend mode of each material in static mesh
	TArray<EBlendMode> MaterialBlendModes;
	MaterialBlendModes.Reserve(GenerateStaticMesh->StaticMaterials.Num());
	for (int32 MaterialIndex = 0; MaterialIndex < GenerateStaticMesh->StaticMaterials.Num(); ++MaterialIndex)
	{
		EBlendMode BlendMode = BLEND_Opaque;

		if (GenerateStaticMesh->StaticMaterials[MaterialIndex].MaterialInterface)
		{
			BlendMode = GenerateStaticMesh->StaticMaterials[MaterialIndex].MaterialInterface->GetBlendMode();
		}
		MaterialBlendModes.Add(BlendMode);
	}

	//Generate embree data
	RTCDevice EmbreeDevice = NULL;
	RTCScene EmbreeScene = NULL;
	EmbreeDevice = rtcNewDevice(NULL);
	RTCError ReturnErrorNewDevice = rtcDeviceGetError(EmbreeDevice);
	if (ReturnErrorNewDevice != RTC_NO_ERROR)
	{
		return;
	}

	EmbreeScene = rtcDeviceNewScene(EmbreeDevice, RTC_SCENE_STATIC, RTC_INTERSECT1);

	RTCError ReturnErrorNewScene = rtcDeviceGetError(EmbreeDevice);
	if (ReturnErrorNewScene != RTC_NO_ERROR)
	{
		rtcDeleteDevice(EmbreeDevice);
		return;
	}

	TArray<int32> FilteredTriangles;
	FilteredTriangles.Empty(Indices.Num() / 3);

	for (int32 i = 0; i < Indices.Num(); i += 3)
	{
		FVector V0 = PositionVertexBuffer.VertexPosition(Indices[i + 0]);
		FVector V1 = PositionVertexBuffer.VertexPosition(Indices[i + 1]);
		FVector V2 = PositionVertexBuffer.VertexPosition(Indices[i + 2]);

		const FVector LocalNormal = ((V1 - V2) ^ (V0 - V2)).GetSafeNormal();

		if (LocalNormal.IsUnit())
		{
			bool bTriangleIsOpaqueOrMasked = false;

			for (int32 SectionIndex = 0; SectionIndex < LODModel.Sections.Num(); SectionIndex++)
			{
				const FStaticMeshSection& Section = LODModel.Sections[SectionIndex];

				if ((uint32)i >= Section.FirstIndex && (uint32)i < Section.FirstIndex + Section.NumTriangles * 3)
				{
					if (MaterialBlendModes.IsValidIndex(Section.MaterialIndex))
					{
						bTriangleIsOpaqueOrMasked = !IsTranslucentBlendMode(MaterialBlendModes[Section.MaterialIndex]);
					}

					break;
				}
			}

			// We only add triangles that is opaque 
			if (bTriangleIsOpaqueOrMasked)
			{
				FilteredTriangles.Add(i / 3);
			}
		}
	}
	
	FVector4* EmbreeVertices = NULL;
	int32* EmbreeIndices = NULL;
	uint32 GeomID = 0;
	FEmbreeGeometry Geometry;

	GeomID = rtcNewTriangleMesh(EmbreeScene, RTC_GEOMETRY_STATIC, FilteredTriangles.Num(), PositionVertexBuffer.GetNumVertices());
	rtcSetIntersectionFilterFunction(EmbreeScene, GeomID, EmbreeFilterFunc);
	rtcSetOcclusionFilterFunction(EmbreeScene, GeomID, EmbreeFilterFunc);
	rtcSetUserData(EmbreeScene, GeomID, &Geometry);

	EmbreeVertices = (FVector4*)rtcMapBuffer(EmbreeScene, GeomID, RTC_VERTEX_BUFFER);
	EmbreeIndices = (int32*)rtcMapBuffer(EmbreeScene, GeomID, RTC_INDEX_BUFFER);

	Geometry.TriangleDescs.Empty(FilteredTriangles.Num());

	for (int32 TriangleIndex = 0; TriangleIndex < FilteredTriangles.Num(); ++TriangleIndex)
	{
		int32 I0 = Indices[TriangleIndex * 3 + 0];
		int32 I1 = Indices[TriangleIndex * 3 + 1];
		int32 I2 = Indices[TriangleIndex * 3 + 2];

		FVector V0 = PositionVertexBuffer.VertexPosition(I0);
		FVector V1 = PositionVertexBuffer.VertexPosition(I1);
		FVector V2 = PositionVertexBuffer.VertexPosition(I2);

		// Set data to buffer
		EmbreeIndices[TriangleIndex * 3 + 0] = I0;
		EmbreeIndices[TriangleIndex * 3 + 1] = I1;
		EmbreeIndices[TriangleIndex * 3 + 2] = I2;

		EmbreeVertices[I0] = FVector4(V0, 0);
		EmbreeVertices[I1] = FVector4(V1, 0);
		EmbreeVertices[I2] = FVector4(V2, 0);

		FEmbreeTriangleDesc Desc;
		// We only need 1 side, so Index always 0
		Desc.ElementIndex = 0;
		Geometry.TriangleDescs.Add(Desc);
	}

	// We have set data to buffer, so unmap buffers
	rtcUnmapBuffer(EmbreeScene, GeomID, RTC_VERTEX_BUFFER);
	rtcUnmapBuffer(EmbreeScene, GeomID, RTC_INDEX_BUFFER);

	RTCError ReturnError = rtcDeviceGetError(EmbreeDevice);
	if (ReturnError != RTC_NO_ERROR)
	{
		rtcDeleteScene(EmbreeScene);
		rtcDeleteDevice(EmbreeDevice);
		return;
	}
	// Now we start create distance field
	// We only need build 4 direction of mesh

	// Distance Field volume always larger than bounding box
	TArray<FVector4> DistanceFieldData;
	DistanceFieldData.Empty(DistanceFieldDimension.X * DistanceFieldDimension.Y);

	const float VolumeScale = 1.3f;
	FBox MeshBox(Bounds.GetBox());
	FBox DistanceFieldVolumeBox = FBox(MeshBox.GetCenter() - VolumeScale * MeshBox.GetExtent(), MeshBox.GetCenter() + VolumeScale * MeshBox.GetExtent());
	const float DistanceFieldVolumeMaxDistance = DistanceFieldVolumeBox.GetExtent().Size();
	const FVector DistanceFieldVoxelSize(DistanceFieldVolumeBox.GetSize() / FVector(DistanceFieldDimension.X, DistanceFieldDimension.Y, DistanceFieldDimension.Z));
	const float VoxelDiameterSqr = DistanceFieldVoxelSize.Size();

	for (int32 i = 0; i < 1; ++i)
	{
		for (int32 YIndex = 0; YIndex < DistanceFieldDimension.Y; YIndex++)
		{
			for (int32 XIndex = 0; XIndex < DistanceFieldDimension.X; XIndex++)
			{
				const FVector VoxelPosition = FVector(XIndex + .5f, YIndex + .5f, i + .5f) * DistanceFieldVoxelSize + Bounds.GetBox().Min;
				const int32 Index = (i * DistanceFieldDimension.Y * DistanceFieldDimension.X + YIndex * DistanceFieldDimension.X + XIndex);

				float MinDistance = DistanceFieldVolumeMaxDistance;
				int32 Hit = 0;
				int32 HitBack = 0;

				// Begin detect
				for (int32 SampleIndex = 0; SampleIndex < SampleDirections.Num(); ++SampleIndex)
				{
					const FVector UnitRayDir = SampleDirections[SampleIndex];
					const FVector EndPosition = VoxelPosition + UnitRayDir * DistanceFieldVolumeMaxDistance;

					// If cur ray don't intersect with volume, skip
					if (FMath::LineBoxIntersection(DistanceFieldVolumeBox, VoxelPosition, EndPosition, UnitRayDir))
					{
						FEmbreeRay EmbreeRay;

						FVector RayDirection = EndPosition - VoxelPosition;
						EmbreeRay.org[0] = VoxelPosition.X;
						EmbreeRay.org[1] = VoxelPosition.Y;
						EmbreeRay.org[2] = VoxelPosition.Z;
						EmbreeRay.dir[0] = RayDirection.X;
						EmbreeRay.dir[1] = RayDirection.Y;
						EmbreeRay.dir[2] = RayDirection.Z;
						EmbreeRay.tnear = 0;
						EmbreeRay.tfar = 1.0f;

						rtcIntersect(EmbreeScene, EmbreeRay);

						if (EmbreeRay.geomID != -1 && EmbreeRay.primID != -1)
						{
							Hit++;
							const FVector GeoNormal = FVector(EmbreeRay.Ng[0], EmbreeRay.Ng[1], EmbreeRay.Ng[2]).GetSafeNormal();

							if (FVector::DotProduct(UnitRayDir, GeoNormal) > 0  // check weather the ray is in backface
								&& EmbreeRay.ElementIndex == 0)
							{
								HitBack++;
							}

							const float CurrentDistance = DistanceFieldVolumeMaxDistance * EmbreeRay.tfar;
							if (CurrentDistance < MinDistance)
							{
								MinDistance = CurrentDistance;
							}
						}
					}
				}
				const float UnsignedDistance = MinDistance;
				// If more than 50% ray is in backface, the distance should < 0
				MinDistance *= (Hit == 0 || HitBack < SampleDirections.Num() * 0.5f) ? 1 : -1;

				if (FMath::Square(UnsignedDistance) < VoxelDiameterSqr && HitBack > .95f * Hit)
				{
					MinDistance = -UnsignedDistance;
				}

				const float FinalVolumeSpaceDistance = FMath::Min(MinDistance, DistanceFieldVolumeMaxDistance) / DistanceFieldVolumeBox.GetExtent().GetMax();
				const int32 CurIndex = YIndex * DistanceFieldDimension.X + XIndex;
				float* Data = nullptr;
				if (DistanceFieldData.Num() <= CurIndex)
				{
					auto& CurValue = DistanceFieldData.AddZeroed_GetRef();
					Data = reinterpret_cast<float*>(&CurValue);
				}
				else
				{
					Data = reinterpret_cast<float*>(DistanceFieldData.GetData() + CurIndex);
				}
				*(Data + i) = FinalVolumeSpaceDistance;
			}
		}
	}

	//TargetTex->UpdateTextureRegions(0)
}

FGenerateDistanceFieldTexture::~FGenerateDistanceFieldTexture()
{
}
