#include "ScenePrivate.h"

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

int32 GMobileMaxCulledLightsPerCell = 32;
FAutoConsoleVariableRef CVarMobileMaxCulledLightsPerCell(
	TEXT("r.Mobile.MaxCulledLightsPerCell"),
	GMobileMaxCulledLightsPerCell,
	TEXT("Controls how much memory is allocated for each cell for light culling."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

FVector GetLightGridZParams(float NearPlane, float FarPlane)
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

// Get depth by slice, z=(exp2(slice/S)-O)/B
float ComputeCellNearViewDepthFromZSlice(const FVector& ZParam, uint ZSlice)
{
	if (ZSlice == GMobileLightGridSizeZ)
		return 2000000.f;

	if (ZSlice == 0)
		return 0.f;

	return (exp2(ZSlice / ZParam.Z) - ZParam.Y) / ZParam.X;
}

float ConvertDepthToDeviceZ(const FMatrix& ProjMat, float ZDepth)
{
	float A = ProjMat.M[2][2];
	float B = ProjMat.M[3][2];

	if (B == 0.f)
		B = 0.00000001f;

	float C1 = 1 / A;
	float C2 = A / B;

	return 1.f / ((ZDepth + C1) * C2);
}

void ComputeCellViewAABB(const FViewInfo& ViewInfo, const FVector& ZParam, const FVector& GridCoord, FVector& OutMin, FVector& OutMax)
{
	const FViewMatrices& ViewMats = ViewInfo.ViewMatrices;
	const uint32 PixelSizeShift = FMath::FloorLog2(GMobileLightGridPixel);

	const FVector2D InvCulledGridSizeF = (1 << PixelSizeShift) * FVector2D(1.f / ViewInfo.ViewRect.Width(), 1.f / ViewInfo.ViewRect.Height());
	const FVector2D TileSize = FVector2D(2.f * InvCulledGridSizeF.X, -2.f * InvCulledGridSizeF.Y);
	const FVector2D UnitPlaneMin = FVector2D(-1.0f, 1.0f);

	const FVector2D UnitPlaneTileMin = FVector2D(GridCoord) * TileSize + UnitPlaneMin;
	const FVector2D UnitPlaneTileMax = (FVector2D(GridCoord) + 1.f) * TileSize + UnitPlaneMin;

	float MinTileZ = ComputeCellNearViewDepthFromZSlice(ZParam, GridCoord.Z);
	float MaxTileZ = ComputeCellNearViewDepthFromZSlice(ZParam, GridCoord.Z + 1);

	float MinTileDeviceZ = ConvertDepthToDeviceZ(ViewMats.ProjectionMatrix, MinTileZ);
	VectorRegister MinDepthCorner0 = VectorTransformVector(MakeVectorRegister(UnitPlaneTileMin.x, UnitPlaneTileMin.Y, MinTileDeviceZ, 1.f), &ViewMats.InvProjectionMatrix);
	VectorRegister MinDepthCorner1 = VectorTransformVector(MakeVectorRegister(UnitPlaneTileMax.x, UnitPlaneTileMax.Y, MinTileDeviceZ, 1.f), &ViewMats.InvProjectionMatrix);
	VectorRegister MinDepthCorner2 = VectorTransformVector(MakeVectorRegister(UnitPlaneTileMin.x, UnitPlaneTileMax.Y, MinTileDeviceZ, 1.f), &ViewMats.InvProjectionMatrix);
	VectorRegister MinDepthCorner3 = VectorTransformVector(MakeVectorRegister(UnitPlaneTileMax.x, UnitPlaneTileMin.Y, MinTileDeviceZ, 1.f), &ViewMats.InvProjectionMatrix);

	float MaxTileDeviceZ = ConvertDepthToDeviceZ(ViewMats.ProjectionMatrix, MaxTileZ);
	VectorRegister MaxDepthCorner0 = VectorTransformVector(MakeVectorRegister(UnitPlaneTileMin.x, UnitPlaneTileMin.Y, MaxTileDeviceZ, 1.f), &ViewMats.InvProjectionMatrix);
	VectorRegister MaxDepthCorner1 = VectorTransformVector(MakeVectorRegister(UnitPlaneTileMax.x, UnitPlaneTileMax.Y, MaxTileDeviceZ, 1.f), &ViewMats.InvProjectionMatrix);
	VectorRegister MaxDepthCorner2 = VectorTransformVector(MakeVectorRegister(UnitPlaneTileMin.x, UnitPlaneTileMax.Y, MaxTileDeviceZ, 1.f), &ViewMats.InvProjectionMatrix);
	VectorRegister MaxDepthCorner3 = VectorTransformVector(MakeVectorRegister(UnitPlaneTileMax.x, UnitPlaneTileMin.Y, MaxTileDeviceZ, 1.f), &ViewMats.InvProjectionMatrix);

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
	float Dist = FVector4::Dot4(FVector4(Center, 1.f), Plane);
	float Radius = FVector::DotProduct(Extents, FVector(Plane));

	return Dist > Radius;
}

bool IsAABBOutsideInfiniteAcuteConeApprox(const FVector& ConeVertex, const FVector& ConneAxis, float TanConeAngle, const FVector& AABBCentre, const FVector& AABBExt)
{
	FVector Dist = AABBCentre - ConeVertex;
	FVector M = -FVector::CrossProduct(FVector::CrossProduct(Dist, ConneAxis), ConeVertex).GetSafeNormal();
	FVector N = -TanConeAngle * ConneAxis + M;

	return AABBOutsidePlane(Dist, AABBExt, FVector4(N, 1.f));
}

void MobileComputeLightGrid_CPU()
{
	FScene* Scene = nullptr;
	TArray<FViewInfo> Views;
	TArray<FSortedLightSceneInfo, SceneRenderingAllocator> SortedLight;
	Scene->Lights
	if ()
}

void MobileComputeLightGrid_GPU()
{

}