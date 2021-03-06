#include "MobileClusterForwardLighting.h"
#include "ScenePrivate.h"
#include "SceneRendering.h"
#include "LightSceneInfo.h"
#include "SceneManagement.h"
#include "Stats/Stats.h"
#include "DrawDebugHelpers.h"

int32 GMobileEnableClusterLighting = 1;
static TAutoConsoleVariable<int32> CVarMobileEnableClusterLighting(
	TEXT("r.Mobile.EnableClusterLighting"),
	GMobileEnableClusterLighting,
	TEXT("Weather support cluster lighting."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

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

int32 GMobileShowClusterDebug = 0;
FAutoConsoleVariableRef CVarMobileShowClusterDebug(
	TEXT("r.Mobile.ShowClusterDebug"),
	GMobileShowClusterDebug,
	TEXT("Show Debug."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GMobileSupportParallelCluster = 1;
FAutoConsoleVariableRef CVarMobileSupportParallelCluster(
	TEXT("r.Mobile.EnableParallelCluster"),
	GMobileSupportParallelCluster,
	TEXT("Enable parallel compute cluster."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GMobileSupportGPUCluster = 1;
FAutoConsoleVariableRef CVarMobileSupportGPUCluster(
	TEXT("r.Mobile.EnableGPUCluster"),
	GMobileSupportGPUCluster,
	TEXT("Enable use GPU to compute cluster."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GMobileMaxLightCullDistance = 10000.f;
FAutoConsoleVariableRef CVarMobileMaxLightCullDistance(
	TEXT("r.Mobile.MaxLightCullDistance"),
	GMobileMaxLightCullDistance,
	TEXT("Mobile max light cull distnace."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

#define PARALLEL_DATA_SIZE_SCALE 3
#define MAX_NUM_LIGHTS_IN_VIEW_SPACE 100

DEFINE_STAT(STAT_MobileComputeGrid);

typedef uint16 FNumCulledDataType;
typedef uint8 FCulledDataType;

static float MaxCullDistance = 15000.f;


FMatrix GInvViewMatrix;

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FMobileClusterLightingUniformParameters, "MobileClusterLight");

FMobileClusterLightingResources* GetMobileClusterLightingResources();
FVector MobileGetLightGridZParams(float NearPlane, float FarPlane);
uint32 ComputeSingleLightGrid(const FViewInfo& View, const FIntVector& GridCoord, const FVector& ZParams, uint32& StartOffset, FCulledDataType* CulledDataPtr, FNumCulledDataType* NumCulledDataPtr);

struct FMobileLocalLightData
{
public:
	FVector4 LightPositionAndInvRadius;
	FVector4 LightColorAndFalloffExponent;
};

struct FMobileSpotLightData
{
public:
	FVector4 SpotLightAngles;
	FVector4 SpotLightDirectionAndSpecularScale;
};

TArray<FMobileLocalLightData> GMobileLocalLightData;
TArray<FMobileSpotLightData> GMobileSpotLightData;
TArray<FVector4> GLightViewSpaceDirAndPreprocAngle;
TArray<FVector4> GLightViewSpacePosAndRadius;
TArray<FNumCulledDataType> GNumCulledLightData;
TArray<FCulledDataType> GCulledLightGridData;
uint32 GCurrentGridZ = GMobileLightGridSizeZ;

TArray<struct FComputeLightGridTaskContext> GAllCLusterTaskContext;

void SetupMobileClusterLightingUniformBuffer(FRHICommandListImmediate& RHICmdList,
	const FViewInfo& View,
	FMobileClusterLightingUniformParameters& ClusterLightingParameters)
{
	if(CVarMobileEnableClusterLighting.GetValueOnRenderThread() <= 0)
		return;
	FMobileClusterLightingResources* ClusterLightRes = GetMobileClusterLightingResources();
	if (ClusterLightRes)
	{
		/*checkf(ClusterLightRes->MobileLocalLight.NumBytes <= GMobileLocalLightData.Num() * sizeof(FMobileLocalLightData) &&
			ClusterLightRes->NumCulledLightsGrid.NumBytes <= GNumCulledLightData.Num() * sizeof(uint16) &&
			ClusterLightRes->CulledLightDataGrid.NumBytes <= GCulledLightDataGrid.Num() * sizeof(uint8), TEXT("------Cluster gpu data have not prepared-------"));*/

		ClusterLightingParameters.MobileLocalLightBuffer = ClusterLightRes->MobileLocalLight.SRV;
		ClusterLightingParameters.MobileSpotLightBuffer = ClusterLightRes->MobileSpotLight.SRV;
		if (GMobileSupportGPUCluster)
		{
			ClusterLightingParameters.NumCulledLightsGrid = ClusterLightRes->RWNumCulledLightsGrid.MipBuffers[0].SRV;
			ClusterLightingParameters.CulledLightDataGrid = ClusterLightRes->RWCulledLightDataGrid.MipBuffers[0].SRV;
			if (ClusterLightRes->MobileLocalLight.SRV && ClusterLightRes->RWNumCulledLightsGrid.MipBuffers[0].SRV && ClusterLightRes->RWCulledLightDataGrid.MipBuffers[0].SRV)
			{
				//UE_LOG(LogTemp, Log, TEXT("Set cluster resource success"));
			}
		}
		else
		{
			ClusterLightingParameters.NumCulledLightsGrid = ClusterLightRes->NumCulledLightsGrid.GetCurLevelBuffer().SRV;
			ClusterLightingParameters.CulledLightDataGrid = ClusterLightRes->CulledLightDataGrid.GetCurLevelBuffer().SRV;
			if (ClusterLightRes->MobileLocalLight.SRV && ClusterLightRes->NumCulledLightsGrid.GetCurLevelBuffer().SRV && ClusterLightRes->CulledLightDataGrid.GetCurLevelBuffer().SRV)
			{
				//UE_LOG(LogTemp, Log, TEXT("Set cluster resource success"));
			}
		}
	}
	ClusterLightingParameters.LightGridZParams = MobileGetLightGridZParams(View.NearClippingDistance, MaxCullDistance);
	FIntPoint CulledGridSizeXY = FIntPoint::DivideAndRoundUp(View.ViewRect.Size(), GMobileLightGridPixel);
	ClusterLightingParameters.CulledGridSizeParams = FUintVector4((uint32)CulledGridSizeXY.X, (uint32)CulledGridSizeXY.Y, GMobileSupportGPUCluster ? GMobileLightGridSizeZ : GCurrentGridZ, (uint32)FMath::FloorLog2(GMobileLightGridPixel));
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

void UpdateClusterLightingBufferData(uint32 CulledDataSize, uint32 NumCulledDataSize)
{
	if (GMobileLocalLightData.Num() <= 0 || GMobileSpotLightData.Num() <= 0 || GNumCulledLightData.Num() <= 0 || GCulledLightGridData.Num() <= 0)
		return;
	FMobileClusterLightingResources* ClusterLightRes = GetMobileClusterLightingResources();

	uint32 NumRequired = GMobileLocalLightData.Num() * GMobileLocalLightData.GetTypeSize();
	if (ClusterLightRes->MobileLocalLight.NumBytes < NumRequired)
	{
		ClusterLightRes->MobileLocalLight.Release();
		ClusterLightRes->MobileLocalLight.Initialize(sizeof(FVector4), NumRequired / sizeof(FVector4), EPixelFormat::PF_A32B32G32R32F, BUF_Dynamic);
	}
	NumRequired = GMobileSpotLightData.Num() * GMobileSpotLightData.GetTypeSize();
	if (ClusterLightRes->MobileSpotLight.NumBytes < NumRequired)
	{
		ClusterLightRes->MobileSpotLight.Release();
		ClusterLightRes->MobileSpotLight.Initialize(sizeof(FVector4), NumRequired / sizeof(FVector4), EPixelFormat::PF_A32B32G32R32F, BUF_Dynamic);
	}
	NumRequired = GNumCulledLightData.Num() * GNumCulledLightData.GetTypeSize();
	if (ClusterLightRes->NumCulledLightsGrid.MipBuffers[0].NumBytes < NumRequired)
	{
		ClusterLightRes->NumCulledLightsGrid.Release();
		ClusterLightRes->NumCulledLightsGrid.Initialize(sizeof(uint32), NumRequired, EPixelFormat::PF_R32_UINT, BUF_Dynamic);
	}
	NumRequired = GCulledLightGridData.Num() * GCulledLightGridData.GetTypeSize();
	if (ClusterLightRes->CulledLightDataGrid.MipBuffers[0].NumBytes < NumRequired)
	{
		ClusterLightRes->CulledLightDataGrid.Release();
		ClusterLightRes->CulledLightDataGrid.Initialize(sizeof(uint32), NumRequired, EPixelFormat::PF_R32_UINT, BUF_Dynamic);
	}

	ClusterLightRes->MobileLocalLight.Lock();
	FPlatformMemory::Memcpy(ClusterLightRes->MobileLocalLight.MappedBuffer, GMobileLocalLightData.GetData(), GMobileLocalLightData.Num() * GMobileLocalLightData.GetTypeSize());
	ClusterLightRes->MobileLocalLight.Unlock();

	ClusterLightRes->MobileSpotLight.Lock();
	FPlatformMemory::Memcpy(ClusterLightRes->MobileSpotLight.MappedBuffer, GMobileSpotLightData.GetData(), GMobileSpotLightData.Num() * GMobileSpotLightData.GetTypeSize());
	ClusterLightRes->MobileSpotLight.Unlock();

	uint32 CurrentLevel = FMath::Clamp(FMath::FloorToInt(FMath::LogX(BUFFER_MIP_LEVEL_SCALE, ((float)ClusterLightRes->NumCulledLightsGrid.MipBuffers[0].NumBytes / NumCulledDataSize))), 0, (int32)MAX_BUFFER_MIP_LEVEL - 1);
	ClusterLightRes->NumCulledLightsGrid.CurLevel = CurrentLevel;
	ClusterLightRes->NumCulledLightsGrid.GetCurLevelBuffer().Lock();
	FPlatformMemory::Memcpy(ClusterLightRes->NumCulledLightsGrid.MipBuffers[CurrentLevel].MappedBuffer, GNumCulledLightData.GetData(), ClusterLightRes->NumCulledLightsGrid.MipBuffers[CurrentLevel].NumBytes);
	ClusterLightRes->NumCulledLightsGrid.MipBuffers[CurrentLevel].Unlock();

	CurrentLevel = FMath::Clamp(FMath::FloorToInt(FMath::LogX(BUFFER_MIP_LEVEL_SCALE, ((float)ClusterLightRes->CulledLightDataGrid.MipBuffers[0].NumBytes / CulledDataSize))), 0, (int32)MAX_BUFFER_MIP_LEVEL - 1);
	ClusterLightRes->CulledLightDataGrid.CurLevel = CurrentLevel;
	ClusterLightRes->CulledLightDataGrid.GetCurLevelBuffer().Lock();
	FPlatformMemory::Memcpy(ClusterLightRes->CulledLightDataGrid.MipBuffers[CurrentLevel].MappedBuffer, GCulledLightGridData.GetData(), ClusterLightRes->CulledLightDataGrid.MipBuffers[CurrentLevel].NumBytes);
	ClusterLightRes->CulledLightDataGrid.MipBuffers[CurrentLevel].Unlock();
}

class FMobileClusterLightingResourceManager : public FRenderResource
{
public:
	FMobileClusterLightingResources LightingResources;

	virtual void InitRHI()
	{
		LightingResources.MobileLocalLight.Initialize(sizeof(FVector4), sizeof(FMobileLocalLightData) / sizeof(FVector4), EPixelFormat::PF_A32B32G32R32F, BUF_Dynamic);
		LightingResources.MobileSpotLight.Initialize(sizeof(FVector4), sizeof(FMobileSpotLightData) / sizeof(FVector4), EPixelFormat::PF_A32B32G32R32F, BUF_Dynamic);
		LightingResources.ViewSpacePosAndRadiusData.Initialize(sizeof(FVector4), 1, EPixelFormat::PF_A32B32G32R32F, BUF_Dynamic);
		LightingResources.ViewSpaceDirAndPreprocAngleData.Initialize(sizeof(FVector4), 1, EPixelFormat::PF_A32B32G32R32F, BUF_Dynamic);

		LightingResources.NumCulledLightsGrid.Initialize(sizeof(uint32), sizeof(uint32), EPixelFormat::PF_R32_UINT, BUF_Dynamic);
		LightingResources.CulledLightDataGrid.Initialize(sizeof(uint32), sizeof(uint32), EPixelFormat::PF_R32_UINT, BUF_Dynamic);		

		LightingResources.RWNumCulledLightsGrid.Initialize(sizeof(uint32), sizeof(uint32), EPixelFormat::PF_R32_UINT);
		LightingResources.RWCulledLightDataGrid.Initialize(sizeof(uint32), sizeof(uint32), EPixelFormat::PF_R32_UINT);
	}

	virtual void ReleaseRHI()
	{
		LightingResources.Release();
	}
};

FAutoConsoleTaskPriority CPrio_FComputeLightGridTask(
	TEXT("TaskGraph.TaskPriorities.FComputeLightGridTask"),
	TEXT("Task and thread priority for FComputeLightGridTask."),
	ENamedThreads::NormalThreadPriority,
	ENamedThreads::HighTaskPriority
);

struct FComputeLightGridTaskContext
{
	uint32 SizeX = 0;
	uint32 SizeY = 0;
	uint32 TaskIndex = 0;

	uint32 CulledNum = 0;

	FVector ZParams;
	const FViewInfo* View = nullptr;
	
	FNumCulledDataType* NumCulledLightData = nullptr;
	FCulledDataType* CulledLightDataGrid = nullptr;
};

class FComputeLightGridTask
{
public:
	FComputeLightGridTask(FComputeLightGridTaskContext* InContext):
		Context(InContext)
	{
	}

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FComputeLightGridTask, STATGROUP_TaskGraphTasks);
	}

	ENamedThreads::Type GetDesiredThread()
	{
		//return ENamedThreads::AnyThread;
		return CPrio_FComputeLightGridTask.Get();
	}

	static ESubsequentsMode::Type GetSubsequentsMode()
	{
		return ESubsequentsMode::TrackSubsequents;
	}

	void AnyThreadTask()
	{
		//TRACE_CPUPROFILER_EVENT_SCOPE(ComputeLightGridTask);

		uint32 StartOffset = 0;
 		uint32 PerGridCulledNum = 0;
		for (uint32 Y = 0; Y < Context->SizeY; ++Y)
		{
			for (uint32 X = 0; X < Context->SizeX; ++X)
			{
				PerGridCulledNum = ComputeSingleLightGrid(*Context->View, FIntVector(X, Y, Context->TaskIndex), Context->ZParams, StartOffset, Context->CulledLightDataGrid, Context->NumCulledLightData);
				Context->CulledLightDataGrid += PerGridCulledNum;
				Context->NumCulledLightData += 2;
				Context->CulledNum += PerGridCulledNum;
			}
		}
	}

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		AnyThreadTask();
	}

private: 
	FComputeLightGridTaskContext* Context;
};

class FUpdateLightGridDataTask
{
public:
	FUpdateLightGridDataTask()
	{
	}

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FUpdateLightGridDataTask, STATGROUP_TaskGraphTasks);
	}

	ENamedThreads::Type GetDesiredThread()
	{
		return ENamedThreads::GetRenderThread_Local();
		//return ENamedThreads::AnyThread;
	}

	static ESubsequentsMode::Type GetSubsequentsMode()
	{
		return ESubsequentsMode::TrackSubsequents;
	}

	void AnyThreadTask()
	{
		// #TODO Compact data
		if (GAllCLusterTaskContext.Num() <= 0)return;
		FCulledDataType* CpyStartDataPtr = GAllCLusterTaskContext[0].CulledLightDataGrid;
		uint32 NumStart = GAllCLusterTaskContext[0].CulledNum / 4;
		uint32 MaxZ = 0, NumCulledDataSize = 0, CulledDataSize = GAllCLusterTaskContext[0].CulledNum;
		for (uint32 i = 1; i < (uint32)GAllCLusterTaskContext.Num(); ++i)
		{
			if (GAllCLusterTaskContext[i].CulledNum > 0)
			{
				MaxZ = i;
				CulledDataSize += GAllCLusterTaskContext[i].CulledNum * sizeof(FCulledDataType);
				FPlatformMemory::Memcpy(CpyStartDataPtr, GAllCLusterTaskContext[i].CulledLightDataGrid - GAllCLusterTaskContext[i].CulledNum, sizeof(FCulledDataType) * GAllCLusterTaskContext[i].CulledNum);
				CpyStartDataPtr += GAllCLusterTaskContext[i].CulledNum;

				uint32 GridNumXY = GAllCLusterTaskContext[i].SizeX * GAllCLusterTaskContext[i].SizeY;
				for (uint32 j = 0; j < GridNumXY; ++j)
				{
					*(GAllCLusterTaskContext[i].NumCulledLightData - GridNumXY * 2 + j * 2 + 1) += NumStart;
				}
				NumStart += GAllCLusterTaskContext[i].CulledNum / 4;
			}
		}
		
		GCurrentGridZ = FMath::Min(MaxZ + 2, (uint32)GMobileLightGridSizeZ);
		UpdateClusterLightingBufferData(CulledDataSize, GCurrentGridZ * GAllCLusterTaskContext[0].SizeX * GAllCLusterTaskContext[0].SizeY * 2 * sizeof(FNumCulledDataType));
		GAllCLusterTaskContext.Reset();
	}

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		AnyThreadTask();
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
	double NearOffset = 0.65f * 100.f;
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

void GatherLocalLightInfo(FScene* Scene, const FViewInfo& View)
{
	if (GMobileLocalLightData.Max() < MAX_NUM_LIGHTS_IN_VIEW_SPACE || GMobileSpotLightData.Max() < MAX_NUM_LIGHTS_IN_VIEW_SPACE)
	{
		GMobileLocalLightData.Reserve(MAX_NUM_LIGHTS_IN_VIEW_SPACE);
		GMobileSpotLightData.Reserve(MAX_NUM_LIGHTS_IN_VIEW_SPACE);
		GLightViewSpaceDirAndPreprocAngle.Reserve(MAX_NUM_LIGHTS_IN_VIEW_SPACE);
		GLightViewSpacePosAndRadius.Reserve(MAX_NUM_LIGHTS_IN_VIEW_SPACE);
	}

	GMobileLocalLightData.Reset();
	GMobileSpotLightData.Reset();
	GLightViewSpaceDirAndPreprocAngle.Reset();
	GLightViewSpacePosAndRadius.Reset();
	float MaxZ = 0.f, Radius = 0.f;
	if (View.VisibleLightInfos.Num() <= 0)return;

	static IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Mobile.EnableMovableSpotLights"));
	const bool bSupportSpotLight = CVar && (CVar->GetInt() > 0);
	for (TSparseArray<FLightSceneInfoCompact>::TConstIterator LightIt(Scene->Lights); LightIt; ++LightIt)
	{
		const FVisibleLightViewInfo& VisibleLightViewInfo = View.VisibleLightInfos[LightIt.GetIndex()];
		if (!VisibleLightViewInfo.bInViewFrustum)
			continue;

		const FLightSceneInfoCompact& LightSceneInfoCompact = *LightIt;
		const FLightSceneInfo* const LightSceneInfo = LightSceneInfoCompact.LightSceneInfo;

		FLightShaderParameters LightParameters;
		const FLightSceneProxy* LightProxy = LightSceneInfo->Proxy;
		LightProxy->GetLightShaderParameters(LightParameters);

		if (LightProxy->GetLightType() != LightType_Directional && GMobileLocalLightData.Num() < GMobileLocalLightData.Max() && LightSceneInfo->ShouldRenderLightViewIndependent()/* && LightSceneInfo->ShouldRenderLight(View)*/)
		{
			
			if (LightProxy->GetLightType() == LightType_Spot && !bSupportSpotLight)
				continue;
			FVector LightPosInView = View.ViewMatrices.GetViewMatrix().TransformPosition(LightParameters.Position);

			if(LightPosInView.Z <= GMobileMaxLightCullDistance)
			{
				FMobileLocalLightData& NewLight = GMobileLocalLightData.AddZeroed_GetRef();
				NewLight.LightPositionAndInvRadius = FVector4(LightParameters.Position, LightParameters.InvRadius);
				NewLight.LightColorAndFalloffExponent = FVector4(LightParameters.Color, LightParameters.FalloffExponent);
				FMobileSpotLightData& NewSpotLight = GMobileSpotLightData.AddZeroed_GetRef();
				NewSpotLight.SpotLightDirectionAndSpecularScale = FVector4(LightParameters.Direction, LightParameters.SpecularScale);
				NewSpotLight.SpotLightAngles = FVector4(LightParameters.SpotAngles.X, LightParameters.SpotAngles.Y, 0.f, LightProxy->GetLightType() == LightType_Spot ? 1.0f : 0.0f);
				if (LightProxy->IsInverseSquared())
				{
					NewLight.LightColorAndFalloffExponent.W = 0;
				}
				GLightViewSpacePosAndRadius.Add(FVector4(LightPosInView, 1 / LightParameters.InvRadius));
				GLightViewSpaceDirAndPreprocAngle.Add(FVector4(View.ViewMatrices.GetViewMatrix().TransformVector(LightParameters.Direction).GetSafeNormal(), LightProxy->GetLightType() == LightType_Spot ? GetTanRadAngle(LightProxy->GetOuterConeAngle()) : 0.0f));

				Radius = 1 / LightParameters.InvRadius;
				MaxZ = FMath::Max(LightPosInView.Z + Radius / 2.f, MaxZ);
			}
		}
	}
	//UE_LOG(LogTemp, Log, TEXT("Light in view space: %d "), GLightViewSpacePosAndRadius.Num());
	//MaxCullDistance = MaxZ;
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

void ComputeCellViewAABB(const FViewInfo& ViewInfo, const FVector& ZParam, const FIntVector& GridCoord, FVector& OutMin, FVector& OutMax)
{
	const FViewMatrices& ViewMats = ViewInfo.ViewMatrices;
	const uint32 PixelSizeShift = FMath::FloorLog2(GMobileLightGridPixel);

	const FVector2D InvCulledGridSizeF = (1 << PixelSizeShift) * FVector2D(1.f / ViewInfo.ViewRect.Width(), 1.f / ViewInfo.ViewRect.Height());
	// Because this origin is (-1,1) in NDC space, so the TileSize.Y need to be negative 
	const FVector2D TileSize = FVector2D(2.f * InvCulledGridSizeF.X, -2.f * InvCulledGridSizeF.Y);
	const FVector2D UnitPlaneMin = FVector2D(-1.0f, 1.0f);

	const FVector2D UnitPlaneTileMin = FVector2D(GridCoord.X, GridCoord.Y) * TileSize + UnitPlaneMin;
	const FVector2D UnitPlaneTileMax = (FVector2D(GridCoord.X, GridCoord.Y) + 1.f) * TileSize + UnitPlaneMin;

	float MinTileZ = FMath::Max(ComputeCellNearViewDepthFromZSlice(ZParam, GridCoord.Z), 10.f);
	float MaxTileZ = FMath::Max(ComputeCellNearViewDepthFromZSlice(ZParam, GridCoord.Z + 1), 10.f);

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
	Max = VectorMax(Max, VectorSwizzle(Max, 2, 3, 0, 0));
	
#if WITH_EDITOR
	if (GMobileShowClusterDebug == 1)
	{
		FVector4 MinCorner0, MinCorner1, MinCorner2, MinCorner3;
		VectorStore(VectorTransformVector(MinDepthCorner0, &GInvViewMatrix), &MinCorner0);
		VectorStore(VectorTransformVector(MinDepthCorner1, &GInvViewMatrix), &MinCorner1);
		VectorStore(VectorTransformVector(MinDepthCorner2, &GInvViewMatrix), &MinCorner2);
		VectorStore(VectorTransformVector(MinDepthCorner3, &GInvViewMatrix), &MinCorner3);
		MinCorner0 = MinCorner0 / MinCorner0.W;
		MinCorner1 = MinCorner1 / MinCorner1.W;
		MinCorner2 = MinCorner2 / MinCorner2.W;
		MinCorner3 = MinCorner3 / MinCorner3.W;

		FVector4 MaxCorner0, MaxCorner1, MaxCorner2, MaxCorner3;
		VectorStore(VectorTransformVector(MaxDepthCorner0, &GInvViewMatrix), &MaxCorner0);
		VectorStore(VectorTransformVector(MaxDepthCorner1, &GInvViewMatrix), &MaxCorner1);
		VectorStore(VectorTransformVector(MaxDepthCorner2, &GInvViewMatrix), &MaxCorner2);
		VectorStore(VectorTransformVector(MaxDepthCorner3, &GInvViewMatrix), &MaxCorner3);
		MaxCorner0 = MaxCorner0 / MaxCorner0.W;
		MaxCorner1 = MaxCorner1 / MaxCorner1.W;
		MaxCorner2 = MaxCorner2 / MaxCorner2.W;
		MaxCorner3 = MaxCorner3 / MaxCorner3.W;

		FFunctionGraphTask::CreateAndDispatchWhenReady([MinCorner0, MinCorner1, MinCorner2, MinCorner3, MaxCorner0, MaxCorner1, MaxCorner2, MaxCorner3]
		{
			DrawDebugLine(GWorld, MinCorner0, MinCorner3, FColor::Red);
			DrawDebugLine(GWorld, MinCorner3, MinCorner1, FColor::Red);
			DrawDebugLine(GWorld, MinCorner1, MinCorner2, FColor::Red);
			DrawDebugLine(GWorld, MinCorner2, MinCorner0, FColor::Red);

			DrawDebugLine(GWorld, MaxCorner0, MaxCorner3, FColor::Red);
			DrawDebugLine(GWorld, MaxCorner3, MaxCorner1, FColor::Red);
			DrawDebugLine(GWorld, MaxCorner1, MaxCorner2, FColor::Red);
			DrawDebugLine(GWorld, MaxCorner2, MaxCorner0, FColor::Red);

			DrawDebugLine(GWorld, MaxCorner0, MinCorner0, FColor::Red);
			DrawDebugLine(GWorld, MaxCorner3, MinCorner3, FColor::Red);
			DrawDebugLine(GWorld, MaxCorner1, MinCorner1, FColor::Red);
			DrawDebugLine(GWorld, MaxCorner2, MinCorner2, FColor::Red);
		}, TStatId(), nullptr, ENamedThreads::GameThread);
	}
	else
	{
		GInvViewMatrix = ViewMats.GetInvViewMatrix();
	}
#endif

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
	FVector M = FVector::CrossProduct(ConeAxis, FVector::CrossProduct(Dist, ConeAxis)).GetSafeNormal();
	// N is the normal direction of the plane that is the edge of the cone, and the normal is faced to the box center
	// #TODO: is the N need normalized ???
	FVector N = (-TanConeAngle * ConeAxis.GetSafeNormal() + M);

	return AABBOutsidePlane(Dist, AABBExt, FVector4(N, 0.f));
}

float ComputeSquaredDistanceFromBoxToPointNoAccurate(FVector BoxCenter, FVector BoxExtent, FVector InPoint)
{
	FVector AxisDistances = ((InPoint - BoxCenter).GetAbs() - BoxExtent).ComponentMax(FVector::ZeroVector);
	return FVector::DotProduct(AxisDistances, AxisDistances);
}

uint32 ComputeSingleLightGrid(const FViewInfo& View, const FIntVector& GridCoord, const FVector& ZParams, uint32& StartOffset, FCulledDataType* CulledDataPtr, FNumCulledDataType* NumCulledDataPtr)
{
	FVector ViewTileMin, ViewTileMax;
	ComputeCellViewAABB(View, ZParams, GridCoord, ViewTileMin, ViewTileMax);
	FVector ViewTileCenter = (ViewTileMax + ViewTileMin) * 0.5f;
	FVector ViewTileExtent = ViewTileMax - ViewTileCenter;
	FVector WorldTileCenter = FVector(View.ViewMatrices.GetOverriddenInvTranslatedViewMatrix().TransformFVector4(FVector4(ViewTileCenter, 1.f))) - View.ViewMatrices.GetPreViewTranslation();
	FVector4 WorldTileBoundingSphere(WorldTileCenter, ViewTileExtent.Size());

	uint32 LightCount = GMobileLocalLightData.Num();
	//static const int32 LightDataStride = 4;
	uint32 CurrentCulledLight = 0;
	uint32 PerGridCulledLightNum = 0;
	for (uint32 i = 0; i < LightCount; ++i)
	{
		//if (PerGridCulledLightNum >= (uint32)GMobileMaxCulledLightsPerCell)break;
		const FVector4 PositionAndInvRadius = GMobileLocalLightData[i].LightPositionAndInvRadius;
		float LightRadius = 1.f / PositionAndInvRadius.W;
		FVector LightPosition(PositionAndInvRadius);
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
				if (GMobileShowClusterDebug)
				{
					FVector WorldViewDir = View.ViewMatrices.GetInvViewMatrix().TransformVector(ViewSpaceLightDirection);
					FFunctionGraphTask::CreateAndDispatchWhenReady([WorldViewDir, LightPosition]
					{
						DrawDebugLine(GWorld, LightPosition, LightPosition + WorldViewDir * 400.f, FColor::Red);
					}, TStatId(), nullptr, ENamedThreads::GameThread);
				}

				// #NOTE This ViewSpaceLightDirection is reversed, because the direction in the lightParam is opposite to the actual light dir
				bPassSpotLightTest = !IsAABBOutsideInfiniteAcuteConeApprox(ViewSpaceLightPosition, -ViewSpaceLightDirection, TanConeAngle, ViewTileCenter, ViewTileExtent);
			}

			if (bPassSpotLightTest)
			{
				*(CulledDataPtr + PerGridCulledLightNum) = FCulledDataType(i);
				++PerGridCulledLightNum;
			}
		}
	}
	*(NumCulledDataPtr++) = PerGridCulledLightNum;
	*(NumCulledDataPtr++) = StartOffset / 4;
	PerGridCulledLightNum = (PerGridCulledLightNum / 4 + FMath::Sign(PerGridCulledLightNum % 4)) * 4;
	StartOffset += PerGridCulledLightNum;

	return PerGridCulledLightNum;
}

void MobileComputeLightGrid_CPU(const FViewInfo& View, FGraphEventRef& TaskEventRef)
{
	FIntPoint CulledGridSizeXY = FIntPoint::DivideAndRoundUp(View.ViewRect.Size(), GMobileLightGridPixel);
	const FIntVector CulledGridSize = FIntVector(CulledGridSizeXY.X, CulledGridSizeXY.Y, GMobileLightGridSizeZ);
	FVector ZParams = MobileGetLightGridZParams(View.NearClippingDistance, MaxCullDistance);
	const int32 CellNum = CulledGridSize.X * CulledGridSize.Y * CulledGridSize.Z;
	
	if (GNumCulledLightData.Max() < CellNum * 2)
	{
		GNumCulledLightData.Reserve(CellNum * 2);
	}

	if (GAllCLusterTaskContext.Max() < GMobileLightGridSizeZ)
	{
		GAllCLusterTaskContext.Reserve(GMobileLightGridSizeZ);
	}

	if (GMobileSupportParallelCluster == 1)
	{
		if (GCulledLightGridData.Max() < CellNum * GMobileMaxCulledLightsPerCell * PARALLEL_DATA_SIZE_SCALE)
		{
			GCulledLightGridData.Reserve(CellNum * GMobileMaxCulledLightsPerCell * PARALLEL_DATA_SIZE_SCALE);
		}

		GAllCLusterTaskContext.SetNumZeroed(GMobileLightGridSizeZ);
		GNumCulledLightData.SetNumUninitialized(CellNum * 2);
		GCulledLightGridData.SetNumUninitialized(CellNum * GMobileMaxCulledLightsPerCell * 2);
	}
	else
	{
		if (GCulledLightGridData.Max() < CellNum * GMobileMaxCulledLightsPerCell)
		{
			GCulledLightGridData.Reserve(CellNum * GMobileMaxCulledLightsPerCell);
		}
		GAllCLusterTaskContext.Reset();
		GNumCulledLightData.Reset();
		GCulledLightGridData.Reset();
	}
		
	//FPlatformMemory::Memset(GNumCulledLightData.GetData(), 0, GNumCulledLightData.Max() * GNumCulledLightData.GetTypeSize());

	uint32 StartOffset = 0;
	uint32 PerGridCulledLightNum = 0;
		
	if (GMobileSupportParallelCluster == 1)
	{
		FGraphEventArray DependentGraphEvents;
		for (int32 Z = CulledGridSize.Z - 1; Z >= 0; --Z)
		{
			GAllCLusterTaskContext[Z].CulledLightDataGrid = GCulledLightGridData.GetData() + Z * (CulledGridSizeXY.X * CulledGridSizeXY.Y) * GMobileMaxCulledLightsPerCell * PARALLEL_DATA_SIZE_SCALE;
			GAllCLusterTaskContext[Z].NumCulledLightData = GNumCulledLightData.GetData() + Z * (CulledGridSizeXY.X * CulledGridSizeXY.Y) * 2;
			GAllCLusterTaskContext[Z].SizeX = CulledGridSizeXY.X;
			GAllCLusterTaskContext[Z].SizeY = CulledGridSizeXY.Y;
			GAllCLusterTaskContext[Z].TaskIndex = Z;
			GAllCLusterTaskContext[Z].View = &View;
			GAllCLusterTaskContext[Z].ZParams = ZParams;
				
			DependentGraphEvents.Add(TGraphTask<FComputeLightGridTask>::CreateTask(nullptr, ENamedThreads::GetRenderThread()).ConstructAndDispatchWhenReady(&GAllCLusterTaskContext[Z]));
		}
		TaskEventRef = TGraphTask<FUpdateLightGridDataTask>::CreateTask(&DependentGraphEvents, ENamedThreads::GetRenderThread()).ConstructAndDispatchWhenReady();
	}
	else
	{
		uint32 PerGirdCulledCount = 0, MaxZ = 0;
		for (int32 Z = 0; Z < CulledGridSize.Z; ++Z)
		{
			for (int32 Y = 0; Y < CulledGridSize.Y; ++Y)
			{
				for (int32 X = 0; X < CulledGridSize.X; ++X)
				{
					FIntVector GridCoord(X, Y, Z);
					PerGridCulledLightNum = ComputeSingleLightGrid(View, GridCoord, ZParams, StartOffset, GCulledLightGridData.GetData() + GCulledLightGridData.Num(), GNumCulledLightData.GetData() + GNumCulledLightData.Num());
					GCulledLightGridData.SetNumUninitialized(GCulledLightGridData.Num() + PerGridCulledLightNum);
					GNumCulledLightData.SetNumUninitialized(GNumCulledLightData.Num() + 2);
					PerGirdCulledCount += PerGridCulledLightNum;
				}
			}
			if (PerGirdCulledCount > 0)
				MaxZ = Z;
			PerGirdCulledCount = 0;
		}
		GCurrentGridZ = FMath::Min(MaxZ + 2, (uint32)GMobileLightGridSizeZ);
		UpdateClusterLightingBufferData(GCulledLightGridData.Num() * GCulledLightGridData.GetTypeSize(), GNumCulledLightData.Num() * GNumCulledLightData.GetTypeSize());
	}
	//UE_LOG(LogTemp, Log, TEXT("Culled Light Count: %d "), GCulledLightDataGrid.Num());
}

/// For Gpu Version
#define MOBILE_CLUSTER_LIGHT_GROUP_SIZE 4

class FMobileComputeClusterCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FMobileComputeClusterCS);
	SHADER_USE_PARAMETER_STRUCT(FMobileComputeClusterCS, FGlobalShader)
public:

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FUintVector4, LightGridSize)
		SHADER_PARAMETER(FVector, ZParams)
		SHADER_PARAMETER(uint32, PixelSizeShift)
		SHADER_PARAMETER(FVector2D, ViewInvSize)
		SHADER_PARAMETER(FVector2D, WorldZToDeviceZ)
		SHADER_PARAMETER(FMatrix, ClipToView)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWNextCulledLightLink)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWStartOffsetGrid)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWCulledLightLinks)
		SHADER_PARAMETER_SRV(Buffer<float4>, LocalLightData)
		SHADER_PARAMETER_SRV(Buffer<float4>, LightViewSpacePositionAndRadius)
		SHADER_PARAMETER_SRV(Buffer<float4>, LightViewSpaceDirAndPreprocAngle)
		END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), MOBILE_CLUSTER_LIGHT_GROUP_SIZE);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMobileComputeClusterCS, "/Engine/Private/MobileClusterForwardLighting.usf", "ComputeClusterCS", SF_Compute);

class FMobileClusterDataCompactCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FMobileClusterDataCompactCS)
	SHADER_USE_PARAMETER_STRUCT(FMobileClusterDataCompactCS, FGlobalShader)
public:
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FUintVector4, LightGridSize)
		SHADER_PARAMETER_UAV(RWBuffer<uint>, RWNumCulledLightsGrid)
		SHADER_PARAMETER_UAV(RWBuffer<uint>, RWCulledLightDataGrid)
		SHADER_PARAMETER_SRV(Buffer<float4>, LocalLightData)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWNextCulledLightData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, StartOffsetGrid)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, CulledLightLinks)
		END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		//return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::ES3_1);
		return true;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), MOBILE_CLUSTER_LIGHT_GROUP_SIZE);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMobileClusterDataCompactCS, "/Engine/Private/MobileClusterForwardLighting.usf", "ClusterDataCompactCS", SF_Compute);

void OnlyUpdateLightDataBuffer(uint32 CellNum = 1)
{
	FMobileClusterLightingResources* ClusterResources = GetMobileClusterLightingResources();

	// Update precompte light data use by shader
	auto& LightDataRef = ClusterResources->MobileLocalLight;
	auto& SpotLightDataRef = ClusterResources->MobileSpotLight;
	auto& PosAndRadiusRef = ClusterResources->ViewSpacePosAndRadiusData;
	auto& DirAndAngleRef = ClusterResources->ViewSpaceDirAndPreprocAngleData;

	uint32 SizeBytes = GMobileLocalLightData.Num() * GMobileLocalLightData.GetTypeSize();
	if (LightDataRef.NumBytes < SizeBytes)
	{
		LightDataRef.Initialize(sizeof(FVector4), SizeBytes / sizeof(FVector4), EPixelFormat::PF_A32B32G32R32F, BUF_Dynamic);
		SpotLightDataRef.Initialize(sizeof(FVector4), SizeBytes / sizeof(FVector4), EPixelFormat::PF_A32B32G32R32F, BUF_Dynamic);
	}

	SizeBytes = GLightViewSpacePosAndRadius.Num() * GLightViewSpacePosAndRadius.GetTypeSize();
	if (PosAndRadiusRef.NumBytes < SizeBytes)
	{
		PosAndRadiusRef.Initialize(sizeof(FVector4), SizeBytes / sizeof(FVector4), EPixelFormat::PF_A32B32G32R32F, BUF_Dynamic);
		DirAndAngleRef.Initialize(sizeof(FVector4), SizeBytes / sizeof(FVector4), EPixelFormat::PF_A32B32G32R32F, BUF_Dynamic);
	}

	LightDataRef.Lock();
	FPlatformMemory::Memcpy(LightDataRef.MappedBuffer, GMobileLocalLightData.GetData(), GMobileLocalLightData.Num() * GMobileLocalLightData.GetTypeSize());
	LightDataRef.Unlock();

	SpotLightDataRef.Lock();
	FPlatformMemory::Memcpy(SpotLightDataRef.MappedBuffer, GMobileSpotLightData.GetData(), GMobileSpotLightData.Num() * GMobileSpotLightData.GetTypeSize());
	SpotLightDataRef.Unlock();

	PosAndRadiusRef.Lock();
	FPlatformMemory::Memcpy(PosAndRadiusRef.MappedBuffer, GLightViewSpacePosAndRadius.GetData(), SizeBytes);
	PosAndRadiusRef.Unlock();
	DirAndAngleRef.Lock();
	FPlatformMemory::Memcpy(DirAndAngleRef.MappedBuffer, GLightViewSpaceDirAndPreprocAngle.GetData(), SizeBytes);
	DirAndAngleRef.Unlock();

	auto& RWNumCulledLightsData = ClusterResources->RWNumCulledLightsGrid;
	auto& RWCulledLightDataGrid = ClusterResources->RWCulledLightDataGrid;

	if (RWNumCulledLightsData.GetMaxSizeBytes() < CellNum * sizeof(uint32))
	{
		RWNumCulledLightsData.Initialize(sizeof(uint32), CellNum * sizeof(uint32), EPixelFormat::PF_R32_UINT);
	}

	if (RWCulledLightDataGrid.GetMaxSizeBytes() < CellNum * GMobileMaxCulledLightsPerCell)
	{
		RWCulledLightDataGrid.Initialize(sizeof(uint32), CellNum * GMobileMaxCulledLightsPerCell, EPixelFormat::PF_R32_UINT);
	}
}

void MobileComputeLightGrid_GPU(const FViewInfo& View, FRHICommandListImmediate& RHICmdList, ERHIFeatureLevel::Type FeatureLevel)
{
	FIntPoint CulledGridSizeXY = FIntPoint::DivideAndRoundUp(View.ViewRect.Size(), GMobileLightGridPixel);
	const FIntVector CulledGridSize = FIntVector(CulledGridSizeXY.X, CulledGridSizeXY.Y, GMobileLightGridSizeZ);
	FVector ZParams = MobileGetLightGridZParams(View.NearClippingDistance, MaxCullDistance);
	const int32 CellNum = CulledGridSize.X * CulledGridSize.Y * CulledGridSize.Z;
	const int32 CulledLightLinksElements = CellNum * GMobileMaxCulledLightsPerCell * 2;

	FIntVector NumGroups = FIntVector::DivideAndRoundUp(CulledGridSize, MOBILE_CLUSTER_LIGHT_GROUP_SIZE);
	FMobileClusterLightingResources* ClusterResources = GetMobileClusterLightingResources();

	FUintVector4 LocalLightSizeData(CulledGridSizeXY.X, CulledGridSizeXY.Y, GMobileLightGridSizeZ, GLightViewSpacePosAndRadius.Num());

	// Update precompte light data use by shader
	OnlyUpdateLightDataBuffer(CellNum);

	TArray<FRHIUnorderedAccessView*, TInlineAllocator<2>> OutUAVs = { 
		ClusterResources->RWNumCulledLightsGrid.MipBuffers[0].UAV, 
		ClusterResources->RWCulledLightDataGrid.MipBuffers[0].UAV };

	RHICmdList.TransitionResources(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EGfxToCompute, OutUAVs.GetData(), OutUAVs.Num());

	FRDGBuilder GraphBuilder(RHICmdList);
	{
		FRDGBufferRef CulledLightLinkBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), CulledLightLinksElements / 2), TEXT("CulledLightLink")); //used for link
		FRDGBufferRef StartOffsetGridBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), CellNum), TEXT("StartOffsetGrid"));
		FRDGBufferRef NextCulledLightLinkBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 1), TEXT("NextCulledLightLink"));
		FRDGBufferRef NextCulledLightDataBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 1), TEXT("NextCulledLightData"));

		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(FeatureLevel);

		TShaderMapRef<FMobileComputeClusterCS> ComputeCS(View.ShaderMap);
		FMobileComputeClusterCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FMobileComputeClusterCS::FParameters>();
		FMatrix ProjMat = View.ViewMatrices.GetProjectionMatrix();
		PassParameters->LightGridSize = LocalLightSizeData;
		PassParameters->ZParams = ZParams;
		PassParameters->PixelSizeShift = FMath::FloorLog2(GMobileLightGridPixel);
		PassParameters->ClipToView = View.ViewMatrices.GetInvProjectionMatrix();
		PassParameters->ViewInvSize = FVector2D(1.f / View.ViewRect.Width(), 1.f / View.ViewRect.Height());
		PassParameters->WorldZToDeviceZ = FVector2D(1.f / ProjMat.M[3][2], ProjMat.M[2][2] / ProjMat.M[3][2]);
		PassParameters->LocalLightData = ClusterResources->MobileLocalLight.SRV;
		PassParameters->LightViewSpacePositionAndRadius = ClusterResources->ViewSpacePosAndRadiusData.SRV;
		PassParameters->LightViewSpaceDirAndPreprocAngle = ClusterResources->ViewSpaceDirAndPreprocAngleData.SRV;
		PassParameters->RWCulledLightLinks = GraphBuilder.CreateUAV(CulledLightLinkBuffer, PF_R32_UINT);
		PassParameters->RWStartOffsetGrid = GraphBuilder.CreateUAV(StartOffsetGridBuffer, PF_R32_UINT);
		PassParameters->RWNextCulledLightLink = GraphBuilder.CreateUAV(NextCulledLightLinkBuffer, PF_R32_UINT);

		AddClearUAVPass(GraphBuilder, PassParameters->RWStartOffsetGrid, 0xffffffff);
		AddClearUAVPass(GraphBuilder, PassParameters->RWNextCulledLightLink, 0);
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(NextCulledLightDataBuffer, PF_R32_UINT), 0);

		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("ComputeClusterCS"), ComputeCS, PassParameters, NumGroups);

		// Compact Data
		TShaderMapRef<FMobileClusterDataCompactCS> CompactCS(View.ShaderMap);
		FMobileClusterDataCompactCS::FParameters* PassParametersCompact = GraphBuilder.AllocParameters<FMobileClusterDataCompactCS::FParameters>();
		PassParametersCompact->LightGridSize = LocalLightSizeData;
		PassParametersCompact->RWNumCulledLightsGrid = ClusterResources->RWNumCulledLightsGrid.MipBuffers[0].UAV;
		PassParametersCompact->RWCulledLightDataGrid = ClusterResources->RWCulledLightDataGrid.MipBuffers[0].UAV;
		PassParametersCompact->RWNextCulledLightData = GraphBuilder.CreateUAV(NextCulledLightDataBuffer, PF_R32_UINT);
		PassParametersCompact->StartOffsetGrid = GraphBuilder.CreateSRV(StartOffsetGridBuffer, PF_R32_UINT);
		PassParametersCompact->CulledLightLinks = GraphBuilder.CreateSRV(CulledLightLinkBuffer, PF_R32_UINT);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("CompactDataCS"), CompactCS, PassParametersCompact, NumGroups);
	}

	GraphBuilder.Execute();
	RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, OutUAVs.GetData(), OutUAVs.Num());
}

/// End
void FMobileSceneRenderer::MobileComputeLightGrid(FRHICommandListImmediate& RHICmdList)
{
	if (CVarMobileEnableClusterLighting.GetValueOnAnyThread() != 1)
		return;

	SCOPE_CYCLE_COUNTER(STAT_MobileComputeGrid);
	for (auto& View : Views)
	{
		GatherLocalLightInfo(Scene, View);

		//if (GMobileLocalLightData.Num() > MAX_BASEPASS_DYNAMIC_POINT_LIGHTS)
		{
			if (GMobileSupportGPUCluster == 1 && Scene->GetFeatureLevel() >= ERHIFeatureLevel::ES3_1)
				MobileComputeLightGrid_GPU(View, RHICmdList, Scene->GetFeatureLevel());
			else
				MobileComputeLightGrid_CPU(View, ComputeClusterTaskEventRef);
		}
	}
}