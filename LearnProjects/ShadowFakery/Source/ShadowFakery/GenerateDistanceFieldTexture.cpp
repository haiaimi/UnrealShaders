// Fill out your copyright notice in the Description page of Project Settings.


#include "GenerateDistanceFieldTexture.h"
#include "Math/RandomStream.h"
#include "StaticMeshResources.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"

#include <embree2/rtcore.h>
#include <embree2/rtcore_ray.h>
#include "AssetRegistryModule.h"

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

UGenerateDistanceFieldTexture::UGenerateDistanceFieldTexture()
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


void UGenerateDistanceFieldTexture::GenerateDistanceFieldTexture(UStaticMesh* GenerateStaticMesh, FIntVector DistanceFieldDimension)
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
	SampleDirections.Reserve(2 * NumThetaSteps * NumPhiSteps);
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

	rtcCommit(EmbreeScene);
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
	DistanceFieldData.AddZeroed(DistanceFieldDimension.X * DistanceFieldDimension.Y);
	const float VolumeScale = 1.3f;
	FBox MeshBox(Bounds.GetBox());
	FBox DistanceFieldVolumeBox = FBox(MeshBox.GetCenter() - VolumeScale * MeshBox.GetExtent(), MeshBox.GetCenter() + VolumeScale * MeshBox.GetExtent());
	const float DistanceFieldVolumeMaxDistance = DistanceFieldVolumeBox.GetExtent().Size();
	const FVector DistanceFieldVoxelSize(DistanceFieldVolumeBox.GetSize() / FVector(DistanceFieldDimension.X, DistanceFieldDimension.Z, DistanceFieldDimension.Y));
	const float VoxelDiameterSqr = DistanceFieldVoxelSize.Size();
	const FVector DistanceFieldVolumeExtent = DistanceFieldVolumeBox.GetExtent();
	const FVector StartPos[4] = { DistanceFieldVolumeBox.Min + FVector(0.f, 2 * DistanceFieldVolumeExtent.Y, 0.f),
							DistanceFieldVolumeBox.Min + FVector(2 * DistanceFieldVolumeExtent.X, 2 * DistanceFieldVolumeExtent.Y, 0.f),
							DistanceFieldVolumeBox.Min + FVector(2 * DistanceFieldVolumeExtent.X, 0.f, 0.f),
							DistanceFieldVolumeBox.Min + FVector(0.f, 0.f, 0.f) };
	const FVector HoriAddDir[4] = { FVector(1.f, 0.f, 0.f), FVector(0.f, -1.f, 0.f), FVector(-1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f) };

	for (int32 i = 0; i < 1; ++i)
	{
		const int32 XSign = (i % 2) == 0 ? ((i / 2 == 0) ? 1 : -1) : 0;
		const int32 YSign = (i % 2) ? ((i / 2) ? 1 : -1) : 0;
		for (int32 YIndex = 0; YIndex < DistanceFieldDimension.Y; YIndex++)
		{
			for (int32 XIndex = 0; XIndex < DistanceFieldDimension.X; XIndex++)
			{
				const FVector VoxelPosition = FVector(XSign * XIndex, YSign * YIndex, DistanceFieldDimension.Y - YIndex) * DistanceFieldVoxelSize + StartPos[i];
				const int32 Index = (i * DistanceFieldDimension.Y * DistanceFieldDimension.X + YIndex * DistanceFieldDimension.X + XIndex);

				float MinDistance = DistanceFieldVolumeMaxDistance;
				int32 Hit = 0;
				int32 HitBack = 0;

				// Begin detect
				//for (int32 SampleIndex = 0; SampleIndex < SampleDirections.Num(); ++SampleIndex)
				
					//const FVector UnitRayDir = SampleDirections[SampleIndex];
					const FVector UnitRayDir = HoriAddDir[(i + 1) % 4];
					const FVector EndPosition = VoxelPosition + UnitRayDir * DistanceFieldVolumeMaxDistance;

					float FinalVolumeSpaceDistance = 1.f;
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
							FinalVolumeSpaceDistance = 0.f;
							//Hit++;
							//const FVector GeoNormal = FVector(EmbreeRay.Ng[0], EmbreeRay.Ng[1], EmbreeRay.Ng[2]).GetSafeNormal();

							//if (FVector::DotProduct(UnitRayDir, GeoNormal) > 0  // check weather the ray is in backface
							//	&& EmbreeRay.ElementIndex == 0)
							//{
							//	HitBack++;
							//}

							//const float CurrentDistance = DistanceFieldVolumeMaxDistance * EmbreeRay.tfar;
							//if (CurrentDistance < MinDistance)
							//{
							//	MinDistance = CurrentDistance;
							//}
						}
					}
				
				//const float UnsignedDistance = MinDistance;
				//// If more than 50% ray is in backface, the distance should < 0
				//MinDistance *= (Hit == 0 || HitBack < SampleDirections.Num() * 0.5f) ? 1 : -1;

				//if (FMath::Square(UnsignedDistance) < VoxelDiameterSqr && HitBack > .95f * Hit)
				//{
				//	MinDistance = -UnsignedDistance;
				//}

				//const float FinalVolumeSpaceDistance = FMath::Min(MinDistance, DistanceFieldVolumeMaxDistance) / DistanceFieldVolumeBox.GetExtent().GetMax();
				const int32 CurIndex = YIndex * DistanceFieldDimension.X + XIndex;
				float* Data = nullptr;
				{
					Data = reinterpret_cast<float*>(DistanceFieldData.GetData() + CurIndex);
				}
				*(Data + i) = FinalVolumeSpaceDistance;
			}
		}
	}
	float MaxRadius = 32;

	for (int32 YIndex = 0; YIndex < DistanceFieldDimension.Y; ++YIndex)
	{
		for (int32 XIndex = 0; XIndex < DistanceFieldDimension.X; ++XIndex)
		{
			int32 StartIndex = YIndex * DistanceFieldDimension.X + XIndex;
			float MinDist = MaxRadius;
			float StartSample = DistanceFieldData[StartIndex].X;
			for (int32 i = -MaxRadius; i < MaxRadius; ++i)
			{
				for (int32 j = -MaxRadius; j < MaxRadius; ++j)
				{
					FVector2D Offset(i, j);
					if (Offset.Size() > MinDist)
						continue;
					if (i + XIndex < 0 || i + XIndex >= DistanceFieldDimension.X)
						continue;
					if (j + YIndex < 0 || j + YIndex >= DistanceFieldDimension.Y)
						continue;

					int32 CurIndex = (YIndex + j) * DistanceFieldDimension.X + (XIndex + i);
					float CurSample = DistanceFieldData[CurIndex].X;
					if (CurSample != StartSample)
					{
						MinDist = FMath::Min(MinDist, Offset.Size());
					}
				}
			}
			float Result = (MinDist - 0.5f) / (MaxRadius - 0.5f);
			Result *= (StartSample == 0.f) ? -1.f : 1.f;
			DistanceFieldData[StartIndex].X = (Result + 1.f) * 0.5f;
		}
	}

	rtcDeleteScene(EmbreeScene);
	rtcDeleteDevice(EmbreeDevice);

	FString TextureName = TEXT("Tex_ShadowFakery_1");
	FString PackageName = TEXT("/Game/ShadowFakeryTextures/");
	PackageName += TextureName;
	UPackage* Package = CreatePackage(NULL, *PackageName);
	Package->FullyLoad();

	UTexture2D* TargetTex = NewObject<UTexture2D>(Package, *TextureName, RF_Public | RF_Standalone | RF_MarkAsRootSet);
	TargetTex->AddToRoot();				// This line prevents garbage collection of the texture
	TargetTex->PlatformData = new FTexturePlatformData();	// Then we initialize the PlatformData
	TargetTex->PlatformData->SizeX = DistanceFieldDimension.X;
	TargetTex->PlatformData->SizeY = DistanceFieldDimension.Y;
	TargetTex->PlatformData->SetNumSlices(1);
	TargetTex->PlatformData->PixelFormat = EPixelFormat::PF_A32B32G32R32F;

	int32 SizeX = TargetTex->GetSizeX();
	int32 SizeY = TargetTex->GetSizeY();
	/*if (DistanceFieldDimension.X <= SizeX && DistanceFieldDimension.Y <= SizeY)
	{
		FUpdateTextureRegion2D UpdateRegion(0, 0, 0, 0, DistanceFieldDimension.X, DistanceFieldDimension.Y);
		TargetTex->UpdateTextureRegions(0, 1, &UpdateRegion, DistanceFieldDimension.X * sizeof(FVector4), sizeof(FVector4), (uint8*)DistanceFieldData.GetData());
	}*/

	/*TArray<float> DistanceFieldDataHalf;
	DistanceFieldDataHalf.Reserve(DistanceFieldDimension.X* DistanceFieldDimension.Y * 4);
	for (int32 j = 0; j < DistanceFieldDimension.Y; ++j)
		for (int32 i = 0; i < DistanceFieldDimension.X; ++i)
		{
			DistanceFieldDataHalf.Add(1.f);
			DistanceFieldDataHalf.Add(0);
			DistanceFieldDataHalf.Add(0);
			DistanceFieldDataHalf.Add(1.f);
		}

	for (auto& Iter : DistanceFieldData)
		Iter = FVector4(1.f, 1.f, 1.f, 1.f);*/

	FTexture2DMipMap* Mip = new(TargetTex->PlatformData->Mips) FTexture2DMipMap();
	Mip->SizeX = DistanceFieldDimension.X;
	Mip->SizeY = DistanceFieldDimension.Y;

	// Lock the texture so it can be modified
	Mip->BulkData.Lock(LOCK_READ_WRITE);
	uint8* TextureData = (uint8*)Mip->BulkData.Realloc(DistanceFieldDimension.X * DistanceFieldDimension.Y * 4 * sizeof(float));
	FMemory::Memcpy(TextureData, DistanceFieldData.GetData(), sizeof(float) * DistanceFieldDimension.X * DistanceFieldDimension.Y * 4);
	Mip->BulkData.Unlock();


	TargetTex->UpdateResource();
	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(TargetTex);

	FString PackageFileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	bool bSaved = UPackage::SavePackage(Package, TargetTex, EObjectFlags::RF_Public | EObjectFlags::RF_Standalone, *PackageFileName, GError, nullptr, true, true, SAVE_NoError);
}

//UGenerateDistanceFieldTexture::~UGenerateDistanceFieldTexture()
//{
//}
