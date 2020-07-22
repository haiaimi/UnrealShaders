#include "MobileClusterForwardLighting.h"
#include "ScenePrivate.h"
#include "SceneRendering.h"
#include "LightSceneInfo.h"
#include "SceneManagement.h"

int32 GMobileLightGridPixel = 64;
FAutoConsoleVariableRef CVarMobileLightGridPixelSize(
	TEXT("r.Mobile.LightGridPixelSize"),
	GMobileLightGridPixel,
	TEXT("Size of a cell in the light grid, in pixels."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GMobileLightGridSizeZ = 32;
FAutoConsoleVariableRef CVarMobileLightGridSizeZ(
	TEXT("r.Mobile.LightGridSizeZ"),
	GMobileLightGridSizeZ,
	TEXT("Number of Z slices in the light grid."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GMobileMaxCulledLightsPerCell = 8;
FAutoConsoleVariableRef CVarMobileMaxCulledLightsPerCell(
	TEXT("r.Mobile.MaxCulledLightsPerCell"),
	GMobileMaxCulledLightsPerCell,
	TEXT("Controls how much memory is allocated for each cell for light culling."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FMobileClusterLightingUniformParameters, "MobileClusterLight");

FMobileClusterLightingResources* GetMobileClusterLightingResources();

struct FMobileLocalLightData
{
public:
	FVector4 LightPositionAndInvRadius;
	FVector4 LightColorAndFalloffExponent;
	FVector4 SpotLightAngles;
	FVector4 SpotLightDirectionAndSpecularScale;
};

TArray<FMobileLocalLightData, SceneRenderingAllocator> GMobileLocalLightData;
TArray<FVector4, SceneRenderingAllocator> GLightViewSpaceDirAndPreprocAngle;
TArray<FVector4, SceneRenderingAllocator> GLightViewSpacePosAndRadius;
TArray<uint16, SceneRenderingAllocator> GNumCulledLightData;
TArray<uint8, SceneRenderingAllocator> GCulledLightDataGrid;

void SetupMobileClusterLightingUniformBuffer(FRHICommandListImmediate& RHICmdList,
	const FViewInfo& View,
	FMobileClusterLightingUniformParameters& ClusterLightingParameters)
{
	FMobileClusterLightingResources* ClusterLightRes = GetMobileClusterLightingResources();
	if (ClusterLightRes && GMobileLocalLightData.Num() > 0 && GNumCulledLightData.Num() > 0 && GCulledLightDataGrid.Num() > 0)
	{
		checkf(ClusterLightRes->MobileLocalLight.NumBytes < GMobileLocalLightData.Num() * sizeof(FMobileLocalLightData) &&
			ClusterLightRes->NumCulledLightsGrid.NumBytes < GNumCulledLightData.Num() * sizeof(uint16) &&
			ClusterLightRes->CulledLightDataGrid.NumBytes < GNumCulledLightData.Num() * sizeof(uint8), TEXT("------Cluster gpu data have not prepared-------"));

		ClusterLightingParameters.MobileLocalLightBuffer = ClusterLightRes->MobileLocalLight.SRV;
		ClusterLightingParameters.NumCulledLightsGrid = ClusterLightRes->NumCulledLightsGrid.SRV;
		ClusterLightingParameters.CulledLightDataGrid = ClusterLightRes->CulledLightDataGrid.SRV;
	}
}

void CreateMobileClusterLightingUniformBuffer(
	FRHICommandListImmediate& RHICmdList,
	const FViewInfo& View,
	TUniformBufferRef<FMobileClusterLightingUniformParameters>& ClusterLightUniformBuffer)
{
	FMobileClusterLightingUniformParameters ClusterLightingParameters;
	SetupMobileClusterLightingUniformBuffer(RHICmdList, View, ClusterLightingParameters);
	ClusterLightUniformBuffer = TUniformBufferRef<FMobileClusterLightingUniformParameters>::CreateUniformBufferImmediate(ClusterLightingParameters, UniformBuffer_SingleFrame);
}

void UpdateClusterLightingBufferData()
{
	// Prepare for
	FMobileClusterLightingResources* ClusterLightRes = GetMobileClusterLightingResources();

	uint32 NumRequired = GMobileLocalLightData.Num() * GMobileLocalLightData.GetTypeSize();
	if (ClusterLightRes->MobileLocalLight.NumBytes < NumRequired)
	{
		ClusterLightRes->MobileLocalLight.Release();
		ClusterLightRes->MobileLocalLight.Initialize(sizeof(FVector4), NumRequired / sizeof(FVector4), EPixelFormat::PF_A32B32G32R32F, BUF_Dynamic);
	}
	NumRequired = GNumCulledLightData.Num() * GNumCulledLightData.GetTypeSize();
	if (ClusterLightRes->NumCulledLightsGrid.NumBytes < NumRequired)
	{
		ClusterLightRes->NumCulledLightsGrid.Release();
		ClusterLightRes->NumCulledLightsGrid.Initialize(sizeof(uint16), NumRequired / sizeof(uint16), EPixelFormat::PF_R16_UINT, BUF_Dynamic);
	}
	NumRequired = GCulledLightDataGrid.Num() * GCulledLightDataGrid.GetTypeSize();
	if (ClusterLightRes->CulledLightDataGrid.NumBytes < NumRequired)
	{
		ClusterLightRes->CulledLightDataGrid.Release();
		ClusterLightRes->CulledLightDataGrid.Initialize(sizeof(uint8), NumRequired / sizeof(uint8), EPixelFormat::PF_R8_UINT, BUF_Dynamic);
	}

	ClusterLightRes->MobileLocalLight.Lock();
	FPlatformMemory::Memcpy(ClusterLightRes->MobileLocalLight.MappedBuffer, GMobileLocalLightData.GetData(), GMobileLocalLightData.Num() * GMobileLocalLightData.GetTypeSize());
	ClusterLightRes->MobileLocalLight.Unlock();

	ClusterLightRes->NumCulledLightsGrid.Lock();
	FPlatformMemory::Memcpy(ClusterLightRes->NumCulledLightsGrid.MappedBuffer, GNumCulledLightData.GetData(), GNumCulledLightData.Num() * GNumCulledLightData.GetTypeSize());
	ClusterLightRes->NumCulledLightsGrid.Unlock();

	ClusterLightRes->CulledLightDataGrid.Lock();
	FPlatformMemory::Memcpy(ClusterLightRes->CulledLightDataGrid.MappedBuffer, GCulledLightDataGrid.GetData(), GCulledLightDataGrid.Num() * GCulledLightDataGrid.GetTypeSize());
	ClusterLightRes->CulledLightDataGrid.Unlock();
}

class FMobileClusterLightingResourceManager : FRenderResource
{
public:
	FMobileClusterLightingResources LightingResources;

	virtual void InitRHI()
	{
		LightingResources.MobileLocalLight.Initialize(sizeof(FVector4), sizeof(FMobileLocalLightData) / sizeof(FVector4), EPixelFormat::PF_A32B32G32R32F, BUF_Dynamic);
		LightingResources.NumCulledLightsGrid.Initialize(sizeof(uint16), 1, EPixelFormat::PF_R16_UINT, BUF_Dynamic);
		LightingResources.CulledLightDataGrid.Initialize(sizeof(uint8), 1, EPixelFormat::PF_R8_UINT, BUF_Dynamic);
	}

	virtual void ReleaseRHI()
	{
		LightingResources.Release();
	}
};

FMobileClusterLightingResources* GetMobileClusterLightingResources()
{
	static TGlobalResource<FMobileClusterLightingResourceManager>* GMobileClusterLightingResources = nullptr;
	if (!GMobileClusterLightingResources)
	{
		GMobileClusterLightingResources = new TGlobalResource<FMobileClusterLightingResourceManager>();
	}
	return &GMobileClusterLightingResources->LightingResources;
}

FVector MobileGetLightGridZParams(float NearPlane, float FarPlane)
{
	// slice = log2(z*B + O)*S
	double NearOffset = 0.95f * 100.f;
	double S = 4.05f;
	double N = NearPlane + NearOffset;
	double F = FarPlane;

	double O = (F - N * exp2((GMobileLightGridSizeZ - 1) / S)) / (F - N);
	double B = (1 - O) / N;
	return FVector(B, O, S);
}

float GetTanRadAngle(float ConeAngle)
{
	if (ConeAngle < PI / 2.f)
	{
		return FMath::Tan(ConeAngle);
	}
	return 0.f;
}

// Get depth by slice, z=(exp2(slice/S)-O)/B
float ComputeCellNearViewDepthFromZSlice(const FVector& ZParam, uint32 ZSlice)
{
	if (ZSlice == GMobileLightGridSizeZ)
		return 2000000.f;

	if (ZSlice == 0)
		return 0.f;

	return (exp2(ZSlice / ZParam.Z) - ZParam.Y) / ZParam.X;
}

float ConvertDepthToDeviceZ(const FMatrix& ProjMat, float ZDepth)
{
	float A = ProjMat.M[2][2];   // always be 0
	float B = ProjMat.M[3][2];  // always be 10

	if (B == 0.f)
		B = 0.00000001f;

	float C1 = 1 / B;   
	float C2 = A / B;

	// Because the depth in UE4 is reversed
	return 1.f / ((ZDepth + C2) * C1);
}

void ComputeCellViewAABB(const FViewInfo& ViewInfo, const FVector& ZParam, const FVector& GridCoord, FVector& OutMin, FVector& OutMax)
{
	const FViewMatrices& ViewMats = ViewInfo.ViewMatrices;
	const uint32 PixelSizeShift = FMath::FloorLog2(GMobileLightGridPixel);

	const FVector2D InvCulledGridSizeF = (1 << PixelSizeShift) * FVector2D(1.f / ViewInfo.ViewRect.Width(), 1.f / ViewInfo.ViewRect.Height());
	// Because this origin is (-1,1) in NDC space, so the TileSize.Y need to be negative 
	const FVector2D TileSize = FVector2D(2.f * InvCulledGridSizeF.X, -2.f * InvCulledGridSizeF.Y);
	const FVector2D UnitPlaneMin = FVector2D(-1.0f, 1.0f);

	const FVector2D UnitPlaneTileMin = FVector2D(GridCoord) * TileSize + UnitPlaneMin;
	const FVector2D UnitPlaneTileMax = (FVector2D(GridCoord) + 1.f) * TileSize + UnitPlaneMin;

	float MinTileZ = ComputeCellNearViewDepthFromZSlice(ZParam, GridCoord.Z);
	float MaxTileZ = ComputeCellNearViewDepthFromZSlice(ZParam, GridCoord.Z + 1);

	// First get the tile pos in NDC space,then convert to view space
	float MinTileDeviceZ = ConvertDepthToDeviceZ(ViewMats.GetProjectionMatrix(), MinTileZ);
	VectorRegister MinDepthCorner0 = VectorTransformVector(MakeVectorRegister(UnitPlaneTileMin.X, UnitPlaneTileMin.Y, MinTileDeviceZ, 1.f), &ViewMats.GetInvProjectionMatrix());
	VectorRegister MinDepthCorner1 = VectorTransformVector(MakeVectorRegister(UnitPlaneTileMax.X, UnitPlaneTileMax.Y, MinTileDeviceZ, 1.f), &ViewMats.GetInvProjectionMatrix());
	VectorRegister MinDepthCorner2 = VectorTransformVector(MakeVectorRegister(UnitPlaneTileMin.X, UnitPlaneTileMax.Y, MinTileDeviceZ, 1.f), &ViewMats.GetInvProjectionMatrix());
	VectorRegister MinDepthCorner3 = VectorTransformVector(MakeVectorRegister(UnitPlaneTileMax.X, UnitPlaneTileMin.Y, MinTileDeviceZ, 1.f), &ViewMats.GetInvProjectionMatrix());

	float MaxTileDeviceZ = ConvertDepthToDeviceZ(ViewMats.GetProjectionMatrix(), MaxTileZ);
	VectorRegister MaxDepthCorner0 = VectorTransformVector(MakeVectorRegister(UnitPlaneTileMin.X, UnitPlaneTileMin.Y, MaxTileDeviceZ, 1.f), &ViewMats.GetInvProjectionMatrix());
	VectorRegister MaxDepthCorner1 = VectorTransformVector(MakeVectorRegister(UnitPlaneTileMax.X, UnitPlaneTileMax.Y, MaxTileDeviceZ, 1.f), &ViewMats.GetInvProjectionMatrix());
	VectorRegister MaxDepthCorner2 = VectorTransformVector(MakeVectorRegister(UnitPlaneTileMin.X, UnitPlaneTileMax.Y, MaxTileDeviceZ, 1.f), &ViewMats.GetInvProjectionMatrix());
	VectorRegister MaxDepthCorner3 = VectorTransformVector(MakeVectorRegister(UnitPlaneTileMax.X, UnitPlaneTileMin.Y, MaxTileDeviceZ, 1.f), &ViewMats.GetInvProjectionMatrix());

	VectorRegister ViewMinDepthCorner0_1 = VectorDivide(VectorShuffle(MinDepthCorner0, MinDepthCorner1, 0, 1, 0, 1), VectorShuffle(MinDepthCorner0, MinDepthCorner1, 3, 3, 3, 3));
	VectorRegister ViewMinDepthCorner2_3 = VectorDivide(VectorShuffle(MinDepthCorner2, MinDepthCorner3, 0, 1, 0, 1), VectorShuffle(MinDepthCorner2, MinDepthCorner3, 3, 3, 3, 3));
	VectorRegister ViewMaxDepthCorner0_1 = VectorDivide(VectorShuffle(MaxDepthCorner0, MaxDepthCorner1, 0, 1, 0, 1), VectorShuffle(MaxDepthCorner0, MaxDepthCorner1, 3, 3, 3, 3));
	VectorRegister ViewMaxDepthCorner2_3 = VectorDivide(VectorShuffle(MaxDepthCorner2, MaxDepthCorner3, 0, 1, 0, 1), VectorShuffle(MaxDepthCorner2, MaxDepthCorner3, 3, 3, 3, 3));
	
	VectorRegister Min = VectorMin(VectorMin(ViewMinDepthCorner0_1, ViewMinDepthCorner2_3), VectorMin(ViewMaxDepthCorner0_1, ViewMaxDepthCorner2_3));
	Min = VectorMin(Min, VectorSwizzle(Min, 2, 3, 0, 0));
	VectorRegister Max = VectorMax(VectorMax(ViewMinDepthCorner0_1, ViewMinDepthCorner2_3), VectorMax(ViewMaxDepthCorner0_1, ViewMaxDepthCorner2_3));
	Max = VectorMin(Max, VectorSwizzle(Max, 2, 3, 0, 0));

	VectorStoreFloat3(Min, &OutMin);
	VectorStoreFloat3(Max, &OutMax);
	OutMin.Z = MinTileZ;
	OutMax.Z = MaxTileZ;
}

bool IntersectConeWithSphere(const FVector& ConeVertex, const FVector& ConeAxis, const float& ConeRadius, const FVector2D& CosSinAngle, const FVector4& SphereToTest)
{
	FVector ConeVertexToSphereCenter = SphereToTest - ConeVertex;
	float ConeVertexToSphereCenterLenSq = ConeVertexToSphereCenter.SizeSquared();
	float SphereProjectedOntoConeAxis = FVector::DotProduct(ConeVertexToSphereCenter, -ConeAxis);
	float DistanceToClosestPoint = CosSinAngle.X * FMath::Sqrt(ConeVertexToSphereCenterLenSq - SphereProjectedOntoConeAxis * SphereProjectedOntoConeAxis) - SphereProjectedOntoConeAxis * CosSinAngle.Y;

	bool bSphereTooFarFromCone = DistanceToClosestPoint > SphereToTest.W;
	bool bSpherePastConeEnd = SphereProjectedOntoConeAxis > SphereToTest.W + ConeRadius;
	bool bSphereBehindVertex = SphereProjectedOntoConeAxis < SphereToTest.W;
	return !(bSphereTooFarFromCone || bSpherePastConeEnd || bSphereBehindVertex);
}

bool AABBOutsidePlane(const FVector& Center, const FVector& Extents, const FVector4& Plane)
{
	float Dist = Dot4(FVector4(Center, 1.f), Plane);
	float Radius = FVector::DotProduct(Extents, FVector(Plane).GetAbs());

	return Dist > Radius;
}

bool IsAABBOutsideInfiniteAcuteConeApprox(const FVector& ConeVertex, const FVector& ConeAxis, float TanConeAngle, const FVector& AABBCentre, const FVector& AABBExt)
{
	FVector Dist = AABBCentre - ConeVertex;
	FVector M = -FVector::CrossProduct(FVector::CrossProduct(Dist, ConeAxis), ConeAxis).GetSafeNormal();
	// N is the normal direction of the plane that is the edge of the cone, and the normal is faced to the box center
	// #TODO: is the N need normalized ???
	FVector N = -TanConeAngle * ConeAxis + M;

	return AABBOutsidePlane(Dist, AABBExt, FVector4(N, 1.f));
}

float ComputeSquaredDistanceFromBoxToPointNoAccurate(FVector BoxCenter, FVector BoxExtent, FVector InPoint)
{
	FVector AxisDistances = ((InPoint - BoxCenter).GetAbs() - BoxExtent).ComponentMax(FVector::ZeroVector);
	return FVector::DotProduct(AxisDistances, AxisDistances);
}

void MobileComputeLightGrid_CPU()
{
	FScene* Scene = nullptr;
	TArray<FViewInfo> Views;
	TArray<FSortedLightSceneInfo, SceneRenderingAllocator> SortedLight;
	//Scene->Lights
	//if ()

	//Gather light info
	if (GMobileLocalLightData.Max() < 100)
	{
		GMobileLocalLightData.Reserve(100);
		GLightViewSpaceDirAndPreprocAngle.Reserve(100);
		GLightViewSpacePosAndRadius.Reserve(100);
	}

	for (auto& View : Views)
	{
		GMobileLocalLightData.Reset();
		GLightViewSpaceDirAndPreprocAngle.Reset();
		GLightViewSpacePosAndRadius.Reset();
		for (TSparseArray<FLightSceneInfoCompact>::TConstIterator LightIt(Scene->Lights); LightIt; ++LightIt)
		{
			const FLightSceneInfoCompact& LightSceneInfoCompact = *LightIt;
			const FLightSceneInfo* const LightSceneInfo = LightSceneInfoCompact.LightSceneInfo;

			FLightShaderParameters LightParameters;
			const FLightSceneProxy* LightProxy = LightSceneInfo->Proxy;
			LightProxy->GetLightShaderParameters(LightParameters);

			if (GMobileLocalLightData.Num() < GMobileLocalLightData.Max() && LightSceneInfo->ShouldRenderLightViewIndependent() && LightSceneInfo->ShouldRenderLight(View))
			{
				FMobileLocalLightData& NewLight = GMobileLocalLightData.AddZeroed_GetRef();
				NewLight.LightPositionAndInvRadius = FVector4(LightParameters.Position, LightParameters.InvRadius);
				NewLight.LightColorAndFalloffExponent = FVector4(LightParameters.Color, LightParameters.FalloffExponent);
				NewLight.SpotLightDirectionAndSpecularScale = FVector4(LightParameters.Direction, LightProxy->GetSpecularScale());
				NewLight.SpotLightAngles = FVector4(LightParameters.SpotAngles.X, LightParameters.SpotAngles.Y, 0.f, LightProxy->GetLightType() == LightType_Spot ? 1.0f : 0.0f);
				GLightViewSpaceDirAndPreprocAngle.Add(FVector4(View.ViewMatrices.GetViewMatrix().TransformVector(LightParameters.Direction), LightProxy->GetLightType() == LightType_Spot ? GetTanRadAngle(LightProxy->GetOuterConeAngle()) : 0.0f));
				GLightViewSpacePosAndRadius.Add(FVector4(View.ViewMatrices.GetOverriddenTranslatedViewMatrix().TransformPosition(LightParameters.Position + View.ViewMatrices.GetPreViewTranslation()), 1 / LightParameters.InvRadius));
			}
		}

		FIntPoint CulledGridSizeXY = FIntPoint::DivideAndRoundUp(View.ViewRect.Size(), GMobileLightGridPixel);
		const FIntVector CulledGridSize = FIntVector(CulledGridSizeXY.X, CulledGridSizeXY.Y, GMobileLightGridSizeZ);
		FVector ZParams = MobileGetLightGridZParams(View.NearClippingDistance, 50000.f);
		const int32 CellNum = CulledGridSize.X * CulledGridSize.Y * CulledGridSize.Z;
		if (GNumCulledLightData.Max() < CellNum * 2)
		{
			GNumCulledLightData.Reserve(CellNum * 2);
			//GNumCulledLightData.SetNumZeroed(CellNum * 2);
		}

		if (GCulledLightDataGrid.Num() < CellNum * GMobileMaxCulledLightsPerCell)
		{
			//GCulledLightDataGrid.SetNumZeroed(CellNum * GMobileMaxCulledLightsPerCell);
			GCulledLightDataGrid.Reserve(CellNum * GMobileMaxCulledLightsPerCell);
		}

		uint32 StartOffset = 0;
		uint32 PerGridCulledLightNum = 0;
		for (int32 Z = 0; Z < CulledGridSize.Z; ++Z)
		{
			for (int32 X = 0; X < CulledGridSize.X; ++X)
			{
				for (int32 Y = 0; Y < CulledGridSize.Y; ++Y)
				{
					FVector ViewTileMin, ViewTileMax;
					FVector GridCoord(X, Y, Z);
					ComputeCellViewAABB(View, ZParams, GridCoord, ViewTileMin, ViewTileMax);
					FVector ViewTileCenter = (ViewTileMax - ViewTileMin) * 0.5f;
					FVector ViewTileExtent = ViewTileMax - ViewTileMin;
					FVector WorldTileCenter = FVector(View.ViewMatrices.GetOverriddenInvTranslatedViewMatrix().TransformFVector4(FVector4(ViewTileCenter, 1.f))) - View.ViewMatrices.GetPreViewTranslation();
					FVector4 WorldTileBoundingSphere(WorldTileCenter, ViewTileExtent.Size());

					int32 LightCount = GMobileLocalLightData.Num();
					//static const int32 LightDataStride = 4;
					uint32 CurrentCulledLight = 0;
					for (int32 i = 0; i < LightCount; ++i)
					{
						float LightRadius = 1.f / GMobileLocalLightData[i].LightPositionAndInvRadius.Z;
						FVector LightPosition(GMobileLocalLightData[i].LightPositionAndInvRadius);
						FVector ViewSpaceLightPosition = FVector(GLightViewSpacePosAndRadius[i]);

						float BoxDistanceSq = ComputeSquaredDistanceFromBoxToPointNoAccurate(ViewTileCenter, ViewTileExtent, ViewSpaceLightPosition);

						if (BoxDistanceSq < LightRadius * LightRadius)
						{
							// Test for spot light
							bool bPassSpotLightTest = true;
							float TanConeAngle = GLightViewSpaceDirAndPreprocAngle[i].W;
							if (TanConeAngle > 0.f)
							{
								FVector ViewSpaceLightDirection(GLightViewSpaceDirAndPreprocAngle[i]);
								bPassSpotLightTest = !IsAABBOutsideInfiniteAcuteConeApprox(ViewSpaceLightPosition, ViewSpaceLightDirection, TanConeAngle, ViewTileCenter, ViewTileExtent);
							}

							if (bPassSpotLightTest)
							{
								GCulledLightDataGrid.Add(i);
								PerGridCulledLightNum++;
								//CurrentCulledLight++;
							}
						}
					}
					GNumCulledLightData.Add(PerGridCulledLightNum);
					GNumCulledLightData.Add(StartOffset);
					StartOffset += PerGridCulledLightNum;
				}
			}
		}
		UpdateClusterLightingBufferData();
	}
}

void MobileComputeLightGrid_GPU()
{

}