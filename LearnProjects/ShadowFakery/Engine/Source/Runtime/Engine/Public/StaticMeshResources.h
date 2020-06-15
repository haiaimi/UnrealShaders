// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	StaticMesh.h: Static mesh class definition.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Containers/IndirectArray.h"
#include "Misc/Guid.h"
#include "Engine/EngineTypes.h"
#include "UObject/UObjectIterator.h"
#include "Materials/MaterialInterface.h"
#include "RenderResource.h"
#include "PackedNormal.h"
#include "Containers/DynamicRHIResourceArray.h"
#include "RawIndexBuffer.h"
#include "Components.h"
#include "LocalVertexFactory.h"
#include "PrimitiveViewRelevance.h"
#include "PrimitiveSceneProxy.h"
#include "Engine/MeshMerging.h"
#include "UObject/UObjectHash.h"
#include "MeshBatch.h"
#include "SceneManagement.h"
#include "Components/StaticMeshComponent.h"
#include "PhysicsEngine/BodySetupEnums.h"
#include "Materials/MaterialInterface.h"
#include "Rendering/ColorVertexBuffer.h"
#include "Rendering/StaticMeshVertexBuffer.h"
#include "Rendering/PositionVertexBuffer.h"
#include "Rendering/StaticMeshVertexDataInterface.h"
#include "Templates/UniquePtr.h"
#include "WeightedRandomSampler.h"
#include "PerPlatformProperties.h"

class FDistanceFieldVolumeData;
class UBodySetup;

/** The maximum number of static mesh LODs allowed. */
#define MAX_STATIC_MESH_LODS 8

/** Whether FStaticMeshSceneProxy should to store data and enable codepaths needed for debug rendering */
#define STATICMESH_ENABLE_DEBUG_RENDERING (!(UE_BUILD_SHIPPING || UE_BUILD_TEST) || WITH_EDITOR)

struct FStaticMaterial;
struct FStaticMeshBuffersSize;

/**
 * The LOD settings to use for a group of static meshes.
 */
class FStaticMeshLODGroup
{
public:
	/** Default values. */
	FStaticMeshLODGroup()
		: DefaultNumLODs(1)
		, DefaultMaxNumStreamedLODs(0)
		, DefaultMaxNumOptionalLODs(0)
		, DefaultLightMapResolution(64)
		, BasePercentTrianglesMult(1.0f)
		, bSupportLODStreaming(false)
		, DisplayName( NSLOCTEXT( "UnrealEd", "None", "None" ) )
	{
		FMemory::Memzero(SettingsBias);
		SettingsBias.PercentTriangles = 1.0f;
	}

	/** Returns the default number of LODs to build. */
	int32 GetDefaultNumLODs() const
	{
		return DefaultNumLODs;
	}

	/** Returns the default maximum of streamed LODs */
	int32 GetDefaultMaxNumStreamedLODs() const
	{
		return DefaultMaxNumStreamedLODs;
	}

	/** Returns the default maximum of optional LODs */
	int32 GetDefaultMaxNumOptionalLODs() const
	{
		return DefaultMaxNumOptionalLODs;
	}

	/** Returns the default lightmap resolution. */
	int32 GetDefaultLightMapResolution() const
	{
		return DefaultLightMapResolution;
	}

	/** Returns whether this LOD group supports LOD streaming. */
	bool IsLODStreamingSupported() const
	{
		return bSupportLODStreaming;
	}

	/** Returns default reduction settings for the specified LOD. */
	FMeshReductionSettings GetDefaultSettings(int32 LODIndex) const
	{
		check(LODIndex >= 0 && LODIndex < MAX_STATIC_MESH_LODS);
		return DefaultSettings[LODIndex];
	}

	/** Applies global settings tweaks for the specified LOD. */
	ENGINE_API FMeshReductionSettings GetSettings(const FMeshReductionSettings& InSettings, int32 LODIndex) const;

private:
	/** FStaticMeshLODSettings initializes group entries. */
	friend class FStaticMeshLODSettings;
	/** The default number of LODs to build. */
	int32 DefaultNumLODs;
	/** Maximum number of streamed LODs */
	int32 DefaultMaxNumStreamedLODs;
	/** Maximum number of optional LODs (currently, need to be either 0 or > max number of LODs below MinLOD) */
	int32 DefaultMaxNumOptionalLODs;
	/** Default lightmap resolution. */
	int32 DefaultLightMapResolution;
	/** An additional reduction of base meshes in this group. */
	float BasePercentTrianglesMult;
	/** Whether static meshes in this LOD group can be streamed. */
	bool bSupportLODStreaming;
	/** Display name. */
	FText DisplayName;
	/** Default reduction settings for meshes in this group. */
	FMeshReductionSettings DefaultSettings[MAX_STATIC_MESH_LODS];
	/** Biases applied to reduction settings. */
	FMeshReductionSettings SettingsBias;
};

/**
 * Per-group LOD settings for static meshes.
 */
class FStaticMeshLODSettings
{
public:

	/**
	 * Initializes LOD settings by reading them from the passed in config file section.
	 * @param IniFile Preloaded ini file object to load from
	 */
	ENGINE_API void Initialize(const FConfigFile& IniFile);

	/** Retrieve the settings for the specified LOD group. */
	const FStaticMeshLODGroup& GetLODGroup(FName LODGroup) const
	{
		const FStaticMeshLODGroup* Group = Groups.Find(LODGroup);
		if (Group == NULL)
		{
			Group = Groups.Find(NAME_None);
		}
		check(Group);
		return *Group;
	}

	int32 GetLODGroupIdx(FName GroupName) const
	{
		const int32* IdxPtr = GroupName2Index.Find(GroupName);
		return IdxPtr ? *IdxPtr : INDEX_NONE;
	}

	/** Retrieve the names of all defined LOD groups. */
	void GetLODGroupNames(TArray<FName>& OutNames) const;

	/** Retrieves the localized display names of all LOD groups. */
	void GetLODGroupDisplayNames(TArray<FText>& OutDisplayNames) const;

private:
	/** Reads an entry from the INI to initialize settings for an LOD group. */
	void ReadEntry(FStaticMeshLODGroup& Group, FString Entry);
	/** Per-group settings. */
	TMap<FName,FStaticMeshLODGroup> Groups;
	/** For fast index lookup. Must not change after initialization */
	TMap<FName, int32> GroupName2Index;
};


/**
 * A set of static mesh triangles which are rendered with the same material.
 */
struct FStaticMeshSection
{
	/** The index of the material with which to render this section. */
	int32 MaterialIndex;

	/** Range of vertices and indices used when rendering this section. */
	uint32 FirstIndex;
	uint32 NumTriangles;
	uint32 MinVertexIndex;
	uint32 MaxVertexIndex;

	/** If true, collision is enabled for this section. */
	bool bEnableCollision;
	/** If true, this section will cast a shadow. */
	bool bCastShadow;

#if WITH_EDITORONLY_DATA
	/** The UV channel density in LocalSpaceUnit / UV Unit. */
	float UVDensities[MAX_STATIC_TEXCOORDS];
	/** The weigths to apply to the UV density, based on the area. */
	float Weights[MAX_STATIC_TEXCOORDS];
#endif

	/** Constructor. */
	FStaticMeshSection()
		: MaterialIndex(0)
		, FirstIndex(0)
		, NumTriangles(0)
		, MinVertexIndex(0)
		, MaxVertexIndex(0)
		, bEnableCollision(false)
		, bCastShadow(true)
	{
#if WITH_EDITORONLY_DATA
		FMemory::Memzero(UVDensities);
		FMemory::Memzero(Weights);
#endif
	}

	/** Serializer. */
	friend FArchive& operator<<(FArchive& Ar,FStaticMeshSection& Section);
};


struct FStaticMeshLODResources;

/** Creates distribution for uniformly sampling a mesh section. */
struct ENGINE_API FStaticMeshSectionAreaWeightedTriangleSampler : FWeightedRandomSampler
{
	FStaticMeshSectionAreaWeightedTriangleSampler();
	void Init(FStaticMeshLODResources* InOwner, int32 InSectionIdx);
	virtual float GetWeights(TArray<float>& OutWeights) override;

protected:

	FStaticMeshLODResources* Owner;
	int32 SectionIdx;
};

struct ENGINE_API FStaticMeshAreaWeightedSectionSampler : FWeightedRandomSampler
{
	FStaticMeshAreaWeightedSectionSampler();
	void Init(FStaticMeshLODResources* InOwner);
	virtual float GetWeights(TArray<float>& OutWeights)override;

protected:
	FStaticMeshLODResources* Owner;
};

typedef TArray<FStaticMeshSectionAreaWeightedTriangleSampler> FStaticMeshSectionAreaWeightedTriangleSamplerArray;

/** Represents GPU resource needed for area weighted uniform sampling of a mesh surface. */
class FStaticMeshSectionAreaWeightedTriangleSamplerBuffer : public FRenderResource
{
public:

	ENGINE_API FStaticMeshSectionAreaWeightedTriangleSamplerBuffer();
	ENGINE_API ~FStaticMeshSectionAreaWeightedTriangleSamplerBuffer();

	ENGINE_API void Init(FStaticMeshSectionAreaWeightedTriangleSamplerArray* SamplerToUpload) { Samplers = SamplerToUpload; }

	// FRenderResource interface.
	ENGINE_API virtual void InitRHI() override;
	ENGINE_API virtual void ReleaseRHI() override;
	virtual FString GetFriendlyName() const override { return TEXT("FStaticMeshSectionAreaWeightedTriangleSamplerBuffer"); }

	ENGINE_API const FShaderResourceViewRHIRef& GetBufferSRV() const { return BufferSectionTriangleSRV; }

private:
	struct SectionTriangleInfo
	{
		float  Prob;
		uint32 Alias;
		uint32 pad0;
		uint32 pad1;
	};

	FVertexBufferRHIRef BufferSectionTriangleRHI = nullptr;
	FShaderResourceViewRHIRef BufferSectionTriangleSRV = nullptr;

	FStaticMeshSectionAreaWeightedTriangleSamplerArray* Samplers = nullptr;
};


struct FDynamicMeshVertex;
struct FModelVertex;

struct FStaticMeshVertexBuffers
{
	/** The buffer containing vertex data. */
	FStaticMeshVertexBuffer StaticMeshVertexBuffer;
	/** The buffer containing the position vertex data. */
	FPositionVertexBuffer PositionVertexBuffer;
	/** The buffer containing the vertex color data. */
	FColorVertexBuffer ColorVertexBuffer;

	/* This is a temporary function to refactor and convert old code, do not copy this as is and try to build your data as SoA from the beginning.*/
	void ENGINE_API InitWithDummyData(FLocalVertexFactory* VertexFactory, uint32 NumVerticies, uint32 NumTexCoords = 1, uint32 LightMapIndex = 0);

	/* This is a temporary function to refactor and convert old code, do not copy this as is and try to build your data as SoA from the beginning.*/
	void ENGINE_API InitFromDynamicVertex(FLocalVertexFactory* VertexFactory, TArray<FDynamicMeshVertex>& Vertices, uint32 NumTexCoords = 1, uint32 LightMapIndex = 0);

	/* This is a temporary function to refactor and convert old code, do not copy this as is and try to build your data as SoA from the beginning.*/
	void ENGINE_API InitModelBuffers(TArray<FModelVertex>& Vertices);

	/* This is a temporary function to refactor and convert old code, do not copy this as is and try to build your data as SoA from the beginning.*/
	void ENGINE_API InitModelVF(FLocalVertexFactory* VertexFactory);
};

struct FAdditionalStaticMeshIndexBuffers
{
	/** Reversed index buffer, used to prevent changing culling state between drawcalls. */
	FRawStaticIndexBuffer ReversedIndexBuffer;
	/** Reversed depth only index buffer, used to prevent changing culling state between drawcalls. */
	FRawStaticIndexBuffer ReversedDepthOnlyIndexBuffer;
	/** Index buffer resource for rendering wireframe mode. */
	FRawStaticIndexBuffer WireframeIndexBuffer;
	/** Index buffer containing adjacency information required by tessellation. */
	FRawStaticIndexBuffer AdjacencyIndexBuffer;
};

/** Rendering resources needed to render an individual static mesh LOD. */
struct FStaticMeshLODResources
{
	FStaticMeshVertexBuffers VertexBuffers;

	/** Index buffer resource for rendering. */
	FRawStaticIndexBuffer IndexBuffer;

	/** Index buffer resource for rendering in depth only passes. */
	FRawStaticIndexBuffer DepthOnlyIndexBuffer;

	FAdditionalStaticMeshIndexBuffers* AdditionalIndexBuffers;

#if RHI_RAYTRACING
	/** Geometry for ray tracing. */
	FRayTracingGeometry RayTracingGeometry;
#endif // RHI_RAYTRACING

	/** Sections for this LOD. */
	TArray<FStaticMeshSection> Sections;

	/** Distance field data associated with this mesh, null if not present.  */
	class FDistanceFieldVolumeData* DistanceFieldData; 

	/** The maximum distance by which this LOD deviates from the base from which it was generated. */
	float MaxDeviation;

	/** True if the adjacency index buffer contained data at init. Needed as it will not be available to the CPU afterwards. */
	uint32 bHasAdjacencyInfo : 1;

	/** True if the depth only index buffers contained data at init. Needed as it will not be available to the CPU afterwards. */
	uint32 bHasDepthOnlyIndices : 1;

	/** True if the reversed index buffers contained data at init. Needed as it will not be available to the CPU afterwards. */
	uint32 bHasReversedIndices : 1;

	/** True if the reversed index buffers contained data at init. Needed as it will not be available to the CPU afterwards. */
	uint32 bHasReversedDepthOnlyIndices: 1;

	uint32 bHasColorVertexData : 1;

	uint32 bHasWireframeIndices : 1;

	/** True if vertex and index data are serialized inline */
	uint32 bBuffersInlined : 1;

	/** True if this LOD is optional. That is, vertex and index data may not be available */
	uint32 bIsOptionalLOD : 1;
	
	/**	Allows uniform random selection of mesh sections based on their area. */
	FStaticMeshAreaWeightedSectionSampler AreaWeightedSampler;
	/**	Allows uniform random selection of triangles on each mesh section based on triangle area. */
	FStaticMeshSectionAreaWeightedTriangleSamplerArray AreaWeightedSectionSamplers;
	/** Allows uniform random selection of triangles on GPU. It is not cooked and serialised but created at runtime from AreaWeightedSectionSamplers when it is available and static mesh bSupportGpuUniformlyDistributedSampling=true*/
	FStaticMeshSectionAreaWeightedTriangleSamplerBuffer AreaWeightedSectionSamplersBuffer;

	uint32 DepthOnlyNumTriangles;

	/** Sum of all vertex and index buffer sizes. Calculated in SerializeBuffers */
	uint32 BuffersSize;

#if USE_BULKDATA_STREAMING_TOKEN
	FBulkDataStreamingToken BulkDataStreamingToken;
#else
	FByteBulkData StreamingBulkData;
#endif

#if STATS
	uint32 StaticMeshIndexMemory;
#endif

#if WITH_EDITOR
	FByteBulkData BulkData;

	FString DerivedDataKey;
#endif
	
	/** Default constructor. */
	ENGINE_API FStaticMeshLODResources();

	ENGINE_API ~FStaticMeshLODResources();

	/** Initializes all rendering resources. */
	void InitResources(UStaticMesh* Parent);

	/** Releases all rendering resources. */
	void ReleaseResources();

	/** Serialize. */
	void Serialize(FArchive& Ar, UObject* Owner, int32 Idx);

	/** Return the triangle count of this LOD. */
	ENGINE_API int32 GetNumTriangles() const;

	/** Return the number of vertices in this LOD. */
	ENGINE_API int32 GetNumVertices() const;

	ENGINE_API int32 GetNumTexCoords() const;

private:
	enum EClassDataStripFlag : uint8
	{
		CDSF_AdjacencyData = 1,
		CDSF_MinLodData = 2,
		CDSF_ReversedIndexBuffer = 4,
	};

	/**
	 * Due to discard on load, size of an static mesh LOD is not known at cook time and
	 * this struct is used to keep track of all the information needed to compute LOD size
	 * at load time
	 */
	struct FStaticMeshBuffersSize
	{
		uint32 SerializedBuffersSize;
		uint32 DepthOnlyIBSize;
		uint32 ReversedIBsSize;

		void Clear()
		{
			SerializedBuffersSize = 0;
			DepthOnlyIBSize = 0;
			ReversedIBsSize = 0;
		}

		uint32 CalcBuffersSize() const;

		friend FArchive& operator<<(FArchive& Ar, FStaticMeshBuffersSize& Info)
		{
			Ar << Info.SerializedBuffersSize;
			Ar << Info.DepthOnlyIBSize;
			Ar << Info.ReversedIBsSize;
			return Ar;
		}
	};

	static int32 GetPlatformMinLODIdx(const ITargetPlatform* TargetPlatform, UStaticMesh* StaticMesh);

	static uint8 GenerateClassStripFlags(FArchive& Ar, UStaticMesh* OwnerStaticMesh, int32 Index);

	static bool IsLODCookedOut(const ITargetPlatform* TargetPlatform, UStaticMesh* StaticMesh, bool bIsBelowMinLOD);

	static bool IsLODInlined(const ITargetPlatform* TargetPlatform, UStaticMesh* StaticMesh, int32 LODIdx, bool bIsBelowMinLOD);

	static int32 GetNumOptionalLODsAllowed(const ITargetPlatform* TargetPlatform, UStaticMesh* StaticMesh);

	/** Compute the size of VertexBuffers and add the result to OutSize */
	static void AccumVertexBuffersSize(const FStaticMeshVertexBuffers& VertexBuffers, uint32& OutSize);

	/** Compute the size of IndexBuffer and add the result to OutSize */
	static void AccumIndexBufferSize(const FRawStaticIndexBuffer& IndexBuffer, uint32& OutSize);

	/**
	 * Serialize vertex and index buffer data for this LOD
	 * OutBuffersSize - Size of all serialized data in bytes
	 */
	void SerializeBuffers(FArchive& Ar, UStaticMesh* OwnerStaticMesh, uint8 InStripFlags, FStaticMeshBuffersSize& OutBuffersSize);

	/**
	 * Serialize availability information such as bHasDepthOnlyIndices and size of buffers so it
	 * can be retrieved without loading in actual vertex or index data
	 */
	void SerializeAvailabilityInfo(FArchive& Ar);

	void ClearAvailabilityInfo();

	template <bool bIncrement>
	void UpdateIndexMemoryStats();

	template <bool bIncrement>
	void UpdateVertexMemoryStats() const;

	void ConditionalForce16BitIndexBuffer(EShaderPlatform MaxShaderPlatform, UStaticMesh* Parent);

	void IncrementMemoryStats();

	void DecrementMemoryStats();

	/** Discard loaded vertex and index data. Used when a streaming request is cancelled */
	void DiscardCPUData();

	friend class FStaticMeshRenderData;
	friend class FStaticMeshStreamIn;
	friend class FStaticMeshStreamIn_IO;
	friend class FStaticMeshStreamOut;
};

struct ENGINE_API FStaticMeshVertexFactories
{
	FStaticMeshVertexFactories(ERHIFeatureLevel::Type InFeatureLevel)
		: VertexFactory(InFeatureLevel, "FStaticMeshVertexFactories")
		, VertexFactoryOverrideColorVertexBuffer(InFeatureLevel, "FStaticMeshVertexFactories_Override")
		, SplineVertexFactory(nullptr)
		, SplineVertexFactoryOverrideColorVertexBuffer(nullptr)
	{
		// FLocalVertexFactory::InitRHI requires valid current feature level to setup streams properly
		check(InFeatureLevel < ERHIFeatureLevel::Num);
	}

	~FStaticMeshVertexFactories();

	/** The vertex factory used when rendering this mesh. */
	FLocalVertexFactory VertexFactory;

	/** The vertex factory used when rendering this mesh with vertex colors. This is lazy init.*/
	FLocalVertexFactory VertexFactoryOverrideColorVertexBuffer;

	struct FSplineMeshVertexFactory* SplineVertexFactory;

	struct FSplineMeshVertexFactory* SplineVertexFactoryOverrideColorVertexBuffer;

	/**
	* Initializes a vertex factory for rendering this static mesh
	*
	* @param	InOutVertexFactory				The vertex factory to configure
	* @param	InParentMesh					Parent static mesh
	* @param	bInOverrideColorVertexBuffer	If true, make a vertex factory ready for per-instance colors
	*/
	void InitVertexFactory(const FStaticMeshLODResources& LodResources, FLocalVertexFactory& InOutVertexFactory, uint32 LODIndex, const UStaticMesh* InParentMesh, bool bInOverrideColorVertexBuffer);

	/** Initializes all rendering resources. */
	void InitResources(const FStaticMeshLODResources& LodResources, uint32 LODIndex, const UStaticMesh* Parent);

	/** Releases all rendering resources. */
	void ReleaseResources();
};

/**
 * FStaticMeshRenderData - All data needed to render a static mesh.
 */
class FStaticMeshRenderData
{
public:
	/** Default constructor. */
	ENGINE_API FStaticMeshRenderData();

	/** Per-LOD resources. */
	TIndirectArray<FStaticMeshLODResources> LODResources;
	TIndirectArray<FStaticMeshVertexFactories> LODVertexFactories;

	/** Screen size to switch LODs */
	FPerPlatformFloat ScreenSize[MAX_STATIC_MESH_LODS];

	/** Bounds of the renderable mesh. */
	FBoxSphereBounds Bounds;

	bool IsInitialized()
	{
		return bIsInitialized;
	}

	/** True if LODs share static lighting data. */
	bool bLODsShareStaticLighting;

	/** True if rhi resources are initialized */
	bool bReadyForStreaming;

	uint8 NumInlinedLODs;

	uint8 CurrentFirstLODIdx;

#if WITH_EDITORONLY_DATA
	/** The derived data key associated with this render data. */
	FString DerivedDataKey;

	/** Map of wedge index to vertex index. */
	TArray<int32> WedgeMap;

	/** Map of material index -> original material index at import time. */
	TArray<int32> MaterialIndexToImportIndex;

	/** UV data used for streaming accuracy debug view modes. In sync for rendering thread */
	TArray<FMeshUVChannelInfo> UVChannelDataPerMaterial;

	void SyncUVChannelData(const TArray<FStaticMaterial>& ObjectData);

	/** The next cached derived data in the list. */
	TUniquePtr<class FStaticMeshRenderData> NextCachedRenderData;

	/**
	 * Cache derived renderable data for the static mesh with the provided
	 * level of detail settings.
	 */
	void Cache(UStaticMesh* Owner, const FStaticMeshLODSettings& LODSettings);
#endif // #if WITH_EDITORONLY_DATA

	/** Serialization. */
	void Serialize(FArchive& Ar, UStaticMesh* Owner, bool bCooked);

	/** Initialize the render resources. */
	void InitResources(ERHIFeatureLevel::Type InFeatureLevel, UStaticMesh* Owner);

	/** Releases the render resources. */
	ENGINE_API void ReleaseResources();

	/** Compute the size of this resource. */
	void GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize) const;

	/** Allocate LOD resources. */
	ENGINE_API void AllocateLODResources(int32 NumLODs);

	/** Update LOD-SECTION uv densities. */
	void ComputeUVDensities();

	void BuildAreaWeighedSamplingData();

#if WITH_EDITOR
	/** Resolve all per-section settings. */
	ENGINE_API void ResolveSectionInfo(UStaticMesh* Owner);
#endif // #if WITH_EDITORONLY_DATA

private:
#if WITH_EDITORONLY_DATA
	/** Allow the editor to explicitly update section information. */
	friend class FLevelOfDetailSettingsLayout;
#endif // #if WITH_EDITORONLY_DATA
	bool bIsInitialized = false;
};

/**
 * This geometry is used to rasterize mesh for software occlusion
 * Generated only for platforms that support ETargetPlatformFeatures::SoftwareOcclusion
 */
class FStaticMeshOccluderData
{
public:
	FStaticMeshOccluderData();

	FOccluderVertexArraySP VerticesSP;
	FOccluderIndexArraySP IndicesSP;

	SIZE_T GetResourceSizeBytes() const;

	static TUniquePtr<FStaticMeshOccluderData> Build(UStaticMesh* Owner);
	/** Serialization. */
	static void SerializeCooked(FArchive& Ar, UStaticMesh* Owner);
};

/**
 * FStaticMeshComponentRecreateRenderStateContext - Destroys render state for all StaticMeshComponents using a given StaticMesh and 
 * recreates them when it goes out of scope. Used to ensure stale rendering data isn't kept around in the components when importing
 * over or rebuilding an existing static mesh.
 */
class FStaticMeshComponentRecreateRenderStateContext
{
public:

	/** Initialization constructor. */
	FStaticMeshComponentRecreateRenderStateContext(UStaticMesh* InStaticMesh, bool InUnbuildLighting = true, bool InRefreshBounds = false)
		: FStaticMeshComponentRecreateRenderStateContext(TArray<UStaticMesh*>{ InStaticMesh }, InUnbuildLighting, InRefreshBounds)
	{
	}

	/** Initialization constructor. */
	FStaticMeshComponentRecreateRenderStateContext(const TArray<UStaticMesh*>& InStaticMeshes, bool InUnbuildLighting = true, bool InRefreshBounds = false)
		: bUnbuildLighting(InUnbuildLighting)
		, bRefreshBounds(InRefreshBounds)
	{
		StaticMeshComponents.Reserve(InStaticMeshes.Num());
		for (UStaticMesh* StaticMesh : InStaticMeshes)
		{
			StaticMeshComponents.Add(StaticMesh);
		}

		TSet<FSceneInterface*> Scenes;

		for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
		{
			UStaticMesh* StaticMesh = It->GetStaticMesh();

			if (StaticMeshComponents.Contains(StaticMesh))
			{
				checkf(!It->IsUnreachable(), TEXT("%s"), *It->GetFullName());

				if (It->bRenderStateCreated)
				{
					check(It->IsRegistered());
					It->DestroyRenderState_Concurrent();
					StaticMeshComponents[StaticMesh].Add(*It);
					Scenes.Add(It->GetScene());
				}
			}
			// Recreate dirty render state, if needed, only for components not using the static mesh we currently have released resources for.
			else if (It->IsRenderStateDirty() && It->IsRegistered() && !It->IsTemplate() && !It->IsPendingKill())
			{
				It->DoDeferredRenderUpdates_Concurrent();
			}
		}

		UpdateAllPrimitiveSceneInfosForScenes(MoveTemp(Scenes));

		// Flush the rendering commands generated by the detachments.
		// The static mesh scene proxies reference the UStaticMesh, and this ensures that they are cleaned up before the UStaticMesh changes.
		FlushRenderingCommands();
	}

	/**
	 * Get all static mesh components that are using the provided static mesh.
	 * @param StaticMesh	The static mesh from which you want to obtain a list of components.
	 * @return An reference to an array of static mesh components that are using this mesh.
	 * @note Will only work using the static meshes provided at construction.
	 */
	const TArray<UStaticMeshComponent*>& GetComponentsUsingMesh(UStaticMesh* StaticMesh) const
	{
		return StaticMeshComponents.FindChecked(StaticMesh);
	}

	/** Destructor: recreates render state for all components that had their render states destroyed in the constructor. */
	~FStaticMeshComponentRecreateRenderStateContext()
	{
		TSet<FSceneInterface*> Scenes;

		for (const auto& MeshComponents : StaticMeshComponents)
		{
			for (UStaticMeshComponent * Component : MeshComponents.Value)
			{
				if (bUnbuildLighting)
				{
					// Invalidate the component's static lighting.
					// This unregisters and reregisters so must not be in the constructor
					Component->InvalidateLightingCache();
				}

				if (bRefreshBounds)
				{
					Component->UpdateBounds();
				}

				if (Component->IsRegistered() && !Component->bRenderStateCreated)
				{
					Component->CreateRenderState_Concurrent();
					Scenes.Add(Component->GetScene());
				}
			}
		}

		UpdateAllPrimitiveSceneInfosForScenes(MoveTemp(Scenes));
	}

private:

	TMap<void*, TArray<UStaticMeshComponent*>> StaticMeshComponents;
	bool bUnbuildLighting;
	bool bRefreshBounds;
};

/**
 * A static mesh component scene proxy.
 */
class ENGINE_API FStaticMeshSceneProxy : public FPrimitiveSceneProxy
{
public:
	SIZE_T GetTypeHash() const override;

	/** Initialization constructor. */
	FStaticMeshSceneProxy(UStaticMeshComponent* Component, bool bForceLODsShareStaticLighting);

	virtual ~FStaticMeshSceneProxy();

	/** Gets the number of mesh batches required to represent the proxy, aside from section needs. */
	virtual int32 GetNumMeshBatches() const
	{
		return 1;
	}

	/** Sets up a shadow FMeshBatch for a specific LOD. */
	virtual bool GetShadowMeshElement(int32 LODIndex, int32 BatchIndex, uint8 InDepthPriorityGroup, FMeshBatch& OutMeshBatch, bool bDitheredLODTransition) const;

	/** Sets up a FMeshBatch for a specific LOD and element. */
	virtual bool GetMeshElement(
		int32 LODIndex, 
		int32 BatchIndex, 
		int32 ElementIndex, 
		uint8 InDepthPriorityGroup, 
		bool bUseSelectionOutline,
		bool bAllowPreCulledIndices,
		FMeshBatch& OutMeshBatch) const;

	virtual int32 CollectOccluderElements(class FOccluderElementsCollector& Collector) const override;

	virtual void CreateRenderThreadResources() override;
		
	virtual void DestroyRenderThreadResources() override;

	/** Sets up a wireframe FMeshBatch for a specific LOD. */
	virtual bool GetWireframeMeshElement(int32 LODIndex, int32 BatchIndex, const FMaterialRenderProxy* WireframeRenderProxy, uint8 InDepthPriorityGroup, bool bAllowPreCulledIndices, FMeshBatch& OutMeshBatch) const;

	/** Sets up a collision FMeshBatch for a specific LOD and element. */
	virtual bool GetCollisionMeshElement(
		int32 LODIndex,
		int32 BatchIndex,
		int32 ElementIndex,
		uint8 InDepthPriorityGroup,
		const FMaterialRenderProxy* RenderProxy,
		FMeshBatch& OutMeshBatch) const;

	virtual uint8 GetCurrentFirstLODIdx_RenderThread() const final override
	{
		return GetCurrentFirstLODIdx_Internal();
	}

protected:
	/** Configures mesh batch vertex / index state. Returns the number of primitives used in the element. */
	uint32 SetMeshElementGeometrySource(
		int32 LODIndex,
		int32 ElementIndex,
		bool bWireframe,
		bool bRequiresAdjacencyInformation,
		bool bUseInversedIndices,
		bool bAllowPreCulledIndices,
		const FVertexFactory* VertexFactory,
		FMeshBatch& OutMeshElement) const;

	/** Sets the screen size on a mesh element. */
	void SetMeshElementScreenSize(int32 LODIndex, bool bDitheredLODTransition, FMeshBatch& OutMeshBatch) const;

	/** Returns whether this mesh needs reverse culling when using reversed indices. */
	bool IsReversedCullingNeeded(bool bUseReversedIndices) const;

	bool IsCollisionView(const FEngineShowFlags& EngineShowFlags, bool& bDrawSimpleCollision, bool& bDrawComplexCollision) const;

	/** Only call on render thread timeline */
	uint8 GetCurrentFirstLODIdx_Internal() const
	{
		return RenderData->CurrentFirstLODIdx;
	}

public:
	// FPrimitiveSceneProxy interface.
#if WITH_EDITOR
	virtual HHitProxy* CreateHitProxies(UPrimitiveComponent* Component, TArray<TRefCountPtr<HHitProxy> >& OutHitProxies) override;
#endif
	virtual void DrawStaticElements(FStaticPrimitiveDrawInterface* PDI) override;
	virtual int32 GetLOD(const FSceneView* View) const override;
	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;
	virtual bool CanBeOccluded() const override;
	virtual bool IsUsingDistanceCullFade() const override;
	virtual void GetLightRelevance(const FLightSceneProxy* LightSceneProxy, bool& bDynamic, bool& bRelevant, bool& bLightMapped, bool& bShadowMapped) const override;
	virtual void GetDistancefieldAtlasData(FBox& LocalVolumeBounds, FVector2D& OutDistanceMinMax, FIntVector& OutBlockMin, FIntVector& OutBlockSize, bool& bOutBuiltAsIfTwoSided, bool& bMeshWasPlane, float& SelfShadowBias, TArray<FMatrix>& ObjectLocalToWorldTransforms, bool& bOutThrottled) const override;
	virtual void GetDistanceFieldInstanceInfo(int32& NumInstances, float& BoundsSurfaceArea) const override;
	virtual bool HasDistanceFieldRepresentation() const override;
	virtual bool HasDynamicIndirectShadowCasterRepresentation() const override;
	virtual uint32 GetMemoryFootprint( void ) const override { return( sizeof( *this ) + GetAllocatedSize() ); }
	uint32 GetAllocatedSize( void ) const { return( FPrimitiveSceneProxy::GetAllocatedSize() + LODs.GetAllocatedSize() ); }

	virtual void GetMeshDescription(int32 LODIndex, TArray<FMeshBatch>& OutMeshElements) const override;

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override;

#if RHI_RAYTRACING
	virtual void GetDynamicRayTracingInstances(FRayTracingMaterialGatheringContext& Context, TArray<FRayTracingInstance>& OutRayTracingInstances) override;
	virtual bool IsRayTracingRelevant() const override { return true; }
	virtual bool IsRayTracingStaticRelevant() const override 
	{ 
		const bool bAllowStaticLighting = FReadOnlyCVARCache::Get().bAllowStaticLighting;
		const bool bIsStaticInstance = !bDynamicRayTracingGeometry;
		return bIsStaticInstance && IsStaticPathAvailable() && !HasViewDependentDPG() && !(bAllowStaticLighting && HasStaticLighting() && !HasValidSettingsForStaticLighting());
	}
#endif // RHI_RAYTRACING

	virtual void GetLCIs(FLCIArray& LCIs) override;

#if WITH_EDITORONLY_DATA
	virtual bool GetPrimitiveDistance(int32 LODIndex, int32 SectionIndex, const FVector& ViewOrigin, float& PrimitiveDistance) const override;
	virtual bool GetMeshUVDensities(int32 LODIndex, int32 SectionIndex, FVector4& WorldUVDensities) const override;
	virtual bool GetMaterialTextureScales(int32 LODIndex, int32 SectionIndex, const FMaterialRenderProxy* MaterialRenderProxy, FVector4* OneOverScales, FIntVector4* UVChannelIndices) const override;
#endif

#if STATICMESH_ENABLE_DEBUG_RENDERING
	virtual int32 GetLightMapResolution() const override { return LightMapResolution; }
#endif

protected:
	/** Information used by the proxy about a single LOD of the mesh. */
	class FLODInfo : public FLightCacheInterface
	{
	public:

		/** Information about an element of a LOD. */
		struct FSectionInfo
		{
			/** Default constructor. */
			FSectionInfo()
				: Material(NULL)
#if WITH_EDITOR
				, bSelected(false)
				, HitProxy(NULL)
#endif
				, FirstPreCulledIndex(0)
				, NumPreCulledTriangles(-1)
			{}

			/** The material with which to render this section. */
			UMaterialInterface* Material;

#if WITH_EDITOR
			/** True if this section should be rendered as selected (editor only). */
			bool bSelected;

			/** The editor needs to be able to individual sub-mesh hit detection, so we store a hit proxy on each mesh. */
			HHitProxy* HitProxy;
#endif

#if WITH_EDITORONLY_DATA
			// The material index from the component. Used by the texture streaming accuracy viewmodes.
			int32 MaterialIndex;
#endif

			int32 FirstPreCulledIndex;
			int32 NumPreCulledTriangles;
		};

		/** Per-section information. */
		TArray<FSectionInfo> Sections;

		/** Vertex color data for this LOD (or NULL when not overridden), FStaticMeshComponentLODInfo handle the release of the memory */
		FColorVertexBuffer* OverrideColorVertexBuffer;

		TUniformBufferRef<FLocalVertexFactoryUniformShaderParameters> OverrideColorVFUniformBuffer;

		const FRawStaticIndexBuffer* PreCulledIndexBuffer;

		/** Initialization constructor. */
		FLODInfo(const UStaticMeshComponent* InComponent, const TIndirectArray<FStaticMeshVertexFactories>& InLODVertexFactories, int32 InLODIndex, int32 InClampedMinLOD, bool bLODsShareStaticLighting);

		bool UsesMeshModifyingMaterials() const { return bUsesMeshModifyingMaterials; }

		// FLightCacheInterface.
		virtual FLightInteraction GetInteraction(const FLightSceneProxy* LightSceneProxy) const override;

	private:
		TArray<FGuid> IrrelevantLights;

		/** True if any elements in this LOD use mesh-modifying materials **/
		bool bUsesMeshModifyingMaterials;
	};

	FStaticMeshRenderData* RenderData;

	FStaticMeshOccluderData* OccluderData;

	TIndirectArray<FLODInfo> LODs;

	const FDistanceFieldVolumeData* DistanceFieldData;	

#if RHI_RAYTRACING
	bool bDynamicRayTracingGeometry;
	TArray<FRayTracingGeometry, TInlineAllocator<MAX_MESH_LOD_COUNT>> DynamicRayTracingGeometries;
	TArray<FRWBuffer, TInlineAllocator<MAX_MESH_LOD_COUNT>> DynamicRayTracingGeometryVertexBuffers;
#endif
	/**
	 * The forcedLOD set in the static mesh editor, copied from the mesh component
	 */
	int32 ForcedLodModel;

	/** Minimum LOD index to use.  Clamped to valid range [0, NumLODs - 1]. */
	int32 ClampedMinLOD;

	uint32 bCastShadow : 1;

	/** This primitive has culling reversed */
	uint32 bReverseCulling : 1;

	/** The view relevance for all the static mesh's materials. */
	FMaterialRelevance MaterialRelevance;

#if WITH_EDITORONLY_DATA
	/** The component streaming distance multiplier */
	float StreamingDistanceMultiplier;
	/** The cached GetTextureStreamingTransformScale */
	float StreamingTransformScale;
	/** Material bounds used for texture streaming. */
	TArray<uint32> MaterialStreamingRelativeBoxes;

	/** Index of the section to preview. If set to INDEX_NONE, all section will be rendered */
	int32 SectionIndexPreview;
	/** Index of the material to preview. If set to INDEX_NONE, all section will be rendered */
	int32 MaterialIndexPreview;

	/** Whether selection should be per section or per entire proxy. */
	bool bPerSectionSelection;
#endif

private:

	const UStaticMesh* StaticMesh;

#if STATICMESH_ENABLE_DEBUG_RENDERING
	AActor* Owner;
	/** LightMap resolution used for VMI_LightmapDensity */
	int32 LightMapResolution;
	/** Body setup for collision debug rendering */
	UBodySetup* BodySetup;
	/** Collision trace flags */
	ECollisionTraceFlag		CollisionTraceFlag;
	/** Collision Response of this component */
	FCollisionResponseContainer CollisionResponse;
	/** LOD used for collision */
	int32 LODForCollision;
	/** Draw mesh collision if used for complex collision */
	uint32 bDrawMeshCollisionIfComplex : 1;
	/** Draw mesh collision if used for simple collision */
	uint32 bDrawMeshCollisionIfSimple : 1;

protected:
	/** Hierarchical LOD Index used for rendering */
	uint8 HierarchicalLODIndex;
#endif

public:

	/**
	 * Returns the display factor for the given LOD level
	 *
	 * @Param LODIndex - The LOD to get the display factor for
	 */
	float GetScreenSize(int32 LODIndex) const;

	/**
	 * Returns the LOD mask for a view, this is like the ordinary LOD but can return two values for dither fading
	 */
	FLODMask GetLODMask(const FSceneView* View) const;

private:
	void AddSpeedTreeWind();
	void RemoveSpeedTreeWind();
};

//#Change by wh, 2020/6/12 
/*-----------------------------------------------------------------------------
	FStaticMeshInstanceData
-----------------------------------------------------------------------------*/

/** The implementation of the static mesh instance data storage type. */
class FStaticMeshInstanceData
{
	template<typename F>
	struct FInstanceTransformMatrix
	{
		F InstanceTransform1[4];
		F InstanceTransform2[4];
		F InstanceTransform3[4];

		friend FArchive& operator<<(FArchive& Ar, FInstanceTransformMatrix& V)
		{
			return Ar
				<< V.InstanceTransform1[0]
				<< V.InstanceTransform1[1]
				<< V.InstanceTransform1[2]
				<< V.InstanceTransform1[3]

				<< V.InstanceTransform2[0]
				<< V.InstanceTransform2[1]
				<< V.InstanceTransform2[2]
				<< V.InstanceTransform2[3]

				<< V.InstanceTransform3[0]
				<< V.InstanceTransform3[1]
				<< V.InstanceTransform3[2]
				<< V.InstanceTransform3[3];
		}

	};

	struct FInstanceLightMapVector
	{
		int16 InstanceLightmapAndShadowMapUVBias[4];

		friend FArchive& operator<<(FArchive& Ar, FInstanceLightMapVector& V)
		{
			return Ar
				<< V.InstanceLightmapAndShadowMapUVBias[0]
				<< V.InstanceLightmapAndShadowMapUVBias[1]
				<< V.InstanceLightmapAndShadowMapUVBias[2]
				<< V.InstanceLightmapAndShadowMapUVBias[3];
		}
	};

public:
	FStaticMeshInstanceData()
	{
	}

	/**
	 * Constructor
	 * @param bInUseHalfFloat - true if device has support for half float in vertex arrays
	 */
	FStaticMeshInstanceData(bool bInUseHalfFloat)
	:	bUseHalfFloat(PLATFORM_BUILTIN_VERTEX_HALF_FLOAT || bInUseHalfFloat)
	{
		AllocateBuffers(0);
	}

	virtual ~FStaticMeshInstanceData()
	{
		delete InstanceOriginData;
		delete InstanceLightmapData;
		delete InstanceTransformData;
	}

	void Serialize(FArchive& Ar);
	
	virtual void AllocateInstances(int32 InNumInstances, EResizeBufferFlags BufferFlags, bool DestroyExistingInstances)
	{
		NumInstances = InNumInstances;

		if (DestroyExistingInstances)
		{
			InstanceOriginData->Empty(NumInstances);
			InstanceLightmapData->Empty(NumInstances);
			InstanceTransformData->Empty(NumInstances);
		}

		// We cannot write directly to the data on all platforms,
		// so we make a TArray of the right type, then assign it
		InstanceOriginData->ResizeBuffer(NumInstances, BufferFlags);
		InstanceOriginDataPtr = InstanceOriginData->GetDataPointer();

		InstanceLightmapData->ResizeBuffer(NumInstances, BufferFlags);
		InstanceLightmapDataPtr = InstanceLightmapData->GetDataPointer();

		InstanceTransformData->ResizeBuffer(NumInstances, BufferFlags);
		InstanceTransformDataPtr = InstanceTransformData->GetDataPointer();
	}

	FORCEINLINE_DEBUGGABLE int32 IsValidIndex(int32 Index) const
	{
		return InstanceOriginData->IsValidIndex(Index);
	}

	FORCEINLINE_DEBUGGABLE void GetInstanceTransform(int32 InstanceIndex, FMatrix& Transform) const
	{
		FVector4 TransformVec[3];
		if (bUseHalfFloat)
		{
			GetInstanceTransformInternal<FFloat16>(InstanceIndex, TransformVec);
		}
		else
		{
			GetInstanceTransformInternal<float>(InstanceIndex, TransformVec);
		}

		Transform.M[0][0] = TransformVec[0][0];
		Transform.M[0][1] = TransformVec[0][1];
		Transform.M[0][2] = TransformVec[0][2];
		Transform.M[0][3] = 0.f;

		Transform.M[1][0] = TransformVec[1][0];
		Transform.M[1][1] = TransformVec[1][1];
		Transform.M[1][2] = TransformVec[1][2];
		Transform.M[1][3] = 0.f;

		Transform.M[2][0] = TransformVec[2][0];
		Transform.M[2][1] = TransformVec[2][1];
		Transform.M[2][2] = TransformVec[2][2];
		Transform.M[2][3] = 0.f;

		FVector4 Origin;
		GetInstanceOriginInternal(InstanceIndex, Origin);

		Transform.M[3][0] = Origin.X;
		Transform.M[3][1] = Origin.Y;
		Transform.M[3][2] = Origin.Z;
		Transform.M[3][3] = 0.f;
	}

	FORCEINLINE_DEBUGGABLE void GetInstanceShaderValues(int32 InstanceIndex, FVector4 (&InstanceTransform)[3], FVector4& InstanceLightmapAndShadowMapUVBias, FVector4& InstanceOrigin) const
	{
		if (bUseHalfFloat)
		{
			GetInstanceTransformInternal<FFloat16>(InstanceIndex, InstanceTransform);
		}
		else
		{
			GetInstanceTransformInternal<float>(InstanceIndex, InstanceTransform);
		}
		GetInstanceLightMapDataInternal(InstanceIndex, InstanceLightmapAndShadowMapUVBias);
		GetInstanceOriginInternal(InstanceIndex, InstanceOrigin);
	}

	FORCEINLINE_DEBUGGABLE void SetInstance(int32 InstanceIndex, const FMatrix& Transform, float RandomInstanceID)
	{
		FVector4 Origin(Transform.M[3][0], Transform.M[3][1], Transform.M[3][2], RandomInstanceID);
		SetInstanceOriginInternal(InstanceIndex, Origin);

		FVector4 InstanceTransform[3];
		InstanceTransform[0] = FVector4(Transform.M[0][0], Transform.M[0][1], Transform.M[0][2], 0.0f);
		InstanceTransform[1] = FVector4(Transform.M[1][0], Transform.M[1][1], Transform.M[1][2], 0.0f);
		InstanceTransform[2] = FVector4(Transform.M[2][0], Transform.M[2][1], Transform.M[2][2], 0.0f);

		if (bUseHalfFloat)
		{
			SetInstanceTransformInternal<FFloat16>(InstanceIndex, InstanceTransform);
		}
		else
		{
			SetInstanceTransformInternal<float>(InstanceIndex, InstanceTransform);
		}

		SetInstanceLightMapDataInternal(InstanceIndex, FVector4(0, 0, 0, 0));
	}
	
	FORCEINLINE_DEBUGGABLE void SetInstance(int32 InstanceIndex, const FMatrix& Transform, float RandomInstanceID, const FVector2D& LightmapUVBias, const FVector2D& ShadowmapUVBias)
	{
		FVector4 Origin(Transform.M[3][0], Transform.M[3][1], Transform.M[3][2], RandomInstanceID);
		SetInstanceOriginInternal(InstanceIndex, Origin);

		FVector4 InstanceTransform[3];
		InstanceTransform[0] = FVector4(Transform.M[0][0], Transform.M[0][1], Transform.M[0][2], 0.0f);
		InstanceTransform[1] = FVector4(Transform.M[1][0], Transform.M[1][1], Transform.M[1][2], 0.0f);
		InstanceTransform[2] = FVector4(Transform.M[2][0], Transform.M[2][1], Transform.M[2][2], 0.0f);

		if (bUseHalfFloat)
		{
			SetInstanceTransformInternal<FFloat16>(InstanceIndex, InstanceTransform);
		}
		else
		{
			SetInstanceTransformInternal<float>(InstanceIndex, InstanceTransform);
		}

		SetInstanceLightMapDataInternal(InstanceIndex, FVector4(LightmapUVBias.X, LightmapUVBias.Y, ShadowmapUVBias.X, ShadowmapUVBias.Y));
	}

	//#Change by wh, 2020/6/12 
	//For custom instance data
	FORCEINLINE_DEBUGGABLE virtual void SetInstance(int32 InstanceIndex, const FMatrix& Transform, const TArray<FVector4>& ShadowFakeryParam, float RandomInstanceID, const FVector2D& LightmapUVBias, const FVector2D& ShadowmapUVBias){}
	//end

	/*FORCEINLINE_DEBUGGABLE void SetInstance(int32 InstanceIndex, const FMatrix& Transform, float RandomInstanceID, const FVector2D& LightmapUVBias, const FVector2D& ShadowmapUVBias)
	{
		FVector4 Origin(Transform.M[3][0], Transform.M[3][1], Transform.M[3][2], RandomInstanceID);
		SetInstanceOriginInternal(InstanceIndex, Origin);

		FVector4 InstanceTransform[3];
		InstanceTransform[0] = FVector4(Transform.M[0][0], Transform.M[0][1], Transform.M[0][2], 0.0f);
		InstanceTransform[1] = FVector4(Transform.M[1][0], Transform.M[1][1], Transform.M[1][2], 0.0f);
		InstanceTransform[2] = FVector4(Transform.M[2][0], Transform.M[2][1], Transform.M[2][2], 0.0f);

		if (bUseHalfFloat)
		{
			SetInstanceTransformInternal<FFloat16>(InstanceIndex, InstanceTransform);
		}
		else
		{
			SetInstanceTransformInternal<float>(InstanceIndex, InstanceTransform);
		}

		SetInstanceLightMapDataInternal(InstanceIndex, FVector4(LightmapUVBias.X, LightmapUVBias.Y, ShadowmapUVBias.X, ShadowmapUVBias.Y));
	}*/

	FORCEINLINE void SetInstance(int32 InstanceIndex, const FMatrix& Transform, const FVector2D& LightmapUVBias, const FVector2D& ShadowmapUVBias)
	{
		FVector4 OldOrigin;
		GetInstanceOriginInternal(InstanceIndex, OldOrigin);

		FVector4 NewOrigin(Transform.M[3][0], Transform.M[3][1], Transform.M[3][2], OldOrigin.Component(3));
		SetInstanceOriginInternal(InstanceIndex, NewOrigin);

		FVector4 InstanceTransform[3];
		InstanceTransform[0] = FVector4(Transform.M[0][0], Transform.M[0][1], Transform.M[0][2], 0.0f);
		InstanceTransform[1] = FVector4(Transform.M[1][0], Transform.M[1][1], Transform.M[1][2], 0.0f);
		InstanceTransform[2] = FVector4(Transform.M[2][0], Transform.M[2][1], Transform.M[2][2], 0.0f);

		if (bUseHalfFloat)
		{
			SetInstanceTransformInternal<FFloat16>(InstanceIndex, InstanceTransform);
		}
		else
		{
			SetInstanceTransformInternal<float>(InstanceIndex, InstanceTransform);
		}

		SetInstanceLightMapDataInternal(InstanceIndex, FVector4(LightmapUVBias.X, LightmapUVBias.Y, ShadowmapUVBias.X, ShadowmapUVBias.Y));
	}

	FORCEINLINE void SetInstanceLightMapData(int32 InstanceIndex, const FVector2D& LightmapUVBias, const FVector2D& ShadowmapUVBias)
	{
		SetInstanceLightMapDataInternal(InstanceIndex, FVector4(LightmapUVBias.X, LightmapUVBias.Y, ShadowmapUVBias.X, ShadowmapUVBias.Y));
	}
	
	FORCEINLINE_DEBUGGABLE void NullifyInstance(int32 InstanceIndex)
	{
		SetInstanceOriginInternal(InstanceIndex, FVector4(0, 0, 0, 0));

		FVector4 InstanceTransform[3];
		InstanceTransform[0] = FVector4(0, 0, 0, 0);
		InstanceTransform[1] = FVector4(0, 0, 0, 0);
		InstanceTransform[2] = FVector4(0, 0, 0, 0);

		if (bUseHalfFloat)
		{
			SetInstanceTransformInternal<FFloat16>(InstanceIndex, InstanceTransform);
		}
		else
		{
			SetInstanceTransformInternal<float>(InstanceIndex, InstanceTransform);
		}

		SetInstanceLightMapDataInternal(InstanceIndex, FVector4(0, 0, 0, 0));
	}

	FORCEINLINE_DEBUGGABLE void SetInstanceEditorData(int32 InstanceIndex, FColor HitProxyColor, bool bSelected)
	{
		FVector4 InstanceTransform[3];
		if (bUseHalfFloat)
		{
			GetInstanceTransformInternal<FFloat16>(InstanceIndex, InstanceTransform);
			InstanceTransform[0][3] = ((float)HitProxyColor.R) + (bSelected ? 256.f : 0.0f);
			InstanceTransform[1][3] = (float)HitProxyColor.G;
			InstanceTransform[2][3] = (float)HitProxyColor.B;
			SetInstanceTransformInternal<FFloat16>(InstanceIndex, InstanceTransform);
		}
		else
		{
			GetInstanceTransformInternal<float>(InstanceIndex, InstanceTransform);
			InstanceTransform[0][3] = ((float)HitProxyColor.R) + (bSelected ? 256.f : 0.0f);
			InstanceTransform[1][3] = (float)HitProxyColor.G;
			InstanceTransform[2][3] = (float)HitProxyColor.B;
			SetInstanceTransformInternal<float>(InstanceIndex, InstanceTransform);
		}
	}

	FORCEINLINE_DEBUGGABLE void ClearInstanceEditorData(int32 InstanceIndex)
	{
		FVector4 InstanceTransform[3];
		if (bUseHalfFloat)
		{
			GetInstanceTransformInternal<FFloat16>(InstanceIndex, InstanceTransform);
			InstanceTransform[0][3] = 0.0f;
			InstanceTransform[1][3] = 0.0f;
			InstanceTransform[2][3] = 0.0f;
			SetInstanceTransformInternal<FFloat16>(InstanceIndex, InstanceTransform);
		}
		else
		{
			GetInstanceTransformInternal<float>(InstanceIndex, InstanceTransform);
			InstanceTransform[0][3] = 0.0f;
			InstanceTransform[1][3] = 0.0f;
			InstanceTransform[2][3] = 0.0f;
			SetInstanceTransformInternal<float>(InstanceIndex, InstanceTransform);
		}
	}

	FORCEINLINE_DEBUGGABLE void SwapInstance(int32 Index1, int32 Index2)
	{
		if (bUseHalfFloat)
		{
			FInstanceTransformMatrix<FFloat16>* ElementData = reinterpret_cast<FInstanceTransformMatrix<FFloat16>*>(InstanceTransformDataPtr);
			uint32 CurrentSize = InstanceTransformData->Num() * InstanceTransformData->GetStride();
			check((void*)((&ElementData[Index1]) + 1) <= (void*)(InstanceTransformDataPtr + CurrentSize));
			check((void*)((&ElementData[Index1]) + 0) >= (void*)(InstanceTransformDataPtr));
			check((void*)((&ElementData[Index2]) + 1) <= (void*)(InstanceTransformDataPtr + CurrentSize));
			check((void*)((&ElementData[Index2]) + 0) >= (void*)(InstanceTransformDataPtr));

			FInstanceTransformMatrix<FFloat16> TempStore = ElementData[Index1];
			ElementData[Index1] = ElementData[Index2];
			ElementData[Index2] = TempStore;
		}
		else
		{
			FInstanceTransformMatrix<float>* ElementData = reinterpret_cast<FInstanceTransformMatrix<float>*>(InstanceTransformDataPtr);
			uint32 CurrentSize = InstanceTransformData->Num() * InstanceTransformData->GetStride();
			check((void*)((&ElementData[Index1]) + 1) <= (void*)(InstanceTransformDataPtr + CurrentSize));
			check((void*)((&ElementData[Index1]) + 0) >= (void*)(InstanceTransformDataPtr));
			check((void*)((&ElementData[Index2]) + 1) <= (void*)(InstanceTransformDataPtr + CurrentSize));
			check((void*)((&ElementData[Index2]) + 0) >= (void*)(InstanceTransformDataPtr));
			
			FInstanceTransformMatrix<float> TempStore = ElementData[Index1];
			ElementData[Index1] = ElementData[Index2];
			ElementData[Index2] = TempStore;
		}
		{

			FVector4* ElementData = reinterpret_cast<FVector4*>(InstanceOriginDataPtr);
			uint32 CurrentSize = InstanceOriginData->Num() * InstanceOriginData->GetStride();
			check((void*)((&ElementData[Index1]) + 1) <= (void*)(InstanceOriginDataPtr + CurrentSize));
			check((void*)((&ElementData[Index1]) + 0) >= (void*)(InstanceOriginDataPtr));
			check((void*)((&ElementData[Index2]) + 1) <= (void*)(InstanceOriginDataPtr + CurrentSize));
			check((void*)((&ElementData[Index2]) + 0) >= (void*)(InstanceOriginDataPtr));

			FVector4 TempStore = ElementData[Index1];
			ElementData[Index1] = ElementData[Index2];
			ElementData[Index2] = TempStore;
		}
		{
			FInstanceLightMapVector* ElementData = reinterpret_cast<FInstanceLightMapVector*>(InstanceLightmapDataPtr);
			uint32 CurrentSize = InstanceLightmapData->Num() * InstanceLightmapData->GetStride();
			check((void*)((&ElementData[Index1]) + 1) <= (void*)(InstanceLightmapDataPtr + CurrentSize));
			check((void*)((&ElementData[Index1]) + 0) >= (void*)(InstanceLightmapDataPtr));
			check((void*)((&ElementData[Index2]) + 1) <= (void*)(InstanceLightmapDataPtr + CurrentSize));
			check((void*)((&ElementData[Index2]) + 0) >= (void*)(InstanceLightmapDataPtr));
			
			FInstanceLightMapVector TempStore = ElementData[Index1];
			ElementData[Index1] = ElementData[Index2];
			ElementData[Index2] = TempStore;
		}
	}

	FORCEINLINE_DEBUGGABLE int32 GetNumInstances() const
	{
		return NumInstances;
	}

	FORCEINLINE_DEBUGGABLE int32 GetNumCustomData() const
	{
		return NumCustomData;
	}


	FORCEINLINE_DEBUGGABLE void SetAllowCPUAccess(bool InNeedsCPUAccess)
	{
		if (InstanceOriginData)
		{
			InstanceOriginData->GetResourceArray()->SetAllowCPUAccess(InNeedsCPUAccess);
		}
		if (InstanceLightmapData)
		{
			InstanceLightmapData->GetResourceArray()->SetAllowCPUAccess(InNeedsCPUAccess);
		}
		if (InstanceTransformData)
		{
			InstanceTransformData->GetResourceArray()->SetAllowCPUAccess(InNeedsCPUAccess);
		}
	}

	FORCEINLINE_DEBUGGABLE bool GetTranslationUsesHalfs() const
	{
		return bUseHalfFloat;
	}

	FORCEINLINE_DEBUGGABLE FResourceArrayInterface* GetOriginResourceArray()
	{
		return InstanceOriginData->GetResourceArray();
	}

	FORCEINLINE_DEBUGGABLE FResourceArrayInterface* GetTransformResourceArray()
	{
		return InstanceTransformData->GetResourceArray();
	}

	FORCEINLINE_DEBUGGABLE FResourceArrayInterface* GetLightMapResourceArray()
	{
		return InstanceLightmapData->GetResourceArray();
	}

	FORCEINLINE_DEBUGGABLE virtual FResourceArrayInterface* GetShadowFakeryResourceArray()
	{
		return nullptr;
	}

	FORCEINLINE_DEBUGGABLE uint32 GetOriginStride()
	{
		return InstanceOriginData->GetStride();
	}

	FORCEINLINE_DEBUGGABLE uint32 GetTransformStride()
	{
		return InstanceTransformData->GetStride();
	}

	FORCEINLINE_DEBUGGABLE uint32 GetLightMapStride()
	{
		return InstanceLightmapData->GetStride();
	}
	
	FORCEINLINE_DEBUGGABLE virtual SIZE_T GetResourceSize() const
	{
		return	InstanceOriginData->GetResourceSize() + 
				InstanceTransformData->GetResourceSize() + 
				InstanceLightmapData->GetResourceSize();
	}

private:
	template<typename T>
	FORCEINLINE_DEBUGGABLE void GetInstanceTransformInternal(int32 InstanceIndex, FVector4 (&Transform)[3]) const
	{
		FInstanceTransformMatrix<T>* ElementData = reinterpret_cast<FInstanceTransformMatrix<T>*>(InstanceTransformDataPtr);
		uint32 CurrentSize = InstanceTransformData->Num() * InstanceTransformData->GetStride();
		check((void*)((&ElementData[InstanceIndex]) + 1) <= (void*)(InstanceTransformDataPtr + CurrentSize));
		check((void*)((&ElementData[InstanceIndex]) + 0) >= (void*)(InstanceTransformDataPtr));
		
		Transform[0][0] = ElementData[InstanceIndex].InstanceTransform1[0];
		Transform[0][1] = ElementData[InstanceIndex].InstanceTransform1[1];
		Transform[0][2] = ElementData[InstanceIndex].InstanceTransform1[2];
		Transform[0][3] = ElementData[InstanceIndex].InstanceTransform1[3];
		
		Transform[1][0] = ElementData[InstanceIndex].InstanceTransform2[0];
		Transform[1][1] = ElementData[InstanceIndex].InstanceTransform2[1];
		Transform[1][2] = ElementData[InstanceIndex].InstanceTransform2[2];
		Transform[1][3] = ElementData[InstanceIndex].InstanceTransform2[3];
		
		Transform[2][0] = ElementData[InstanceIndex].InstanceTransform3[0];
		Transform[2][1] = ElementData[InstanceIndex].InstanceTransform3[1];
		Transform[2][2] = ElementData[InstanceIndex].InstanceTransform3[2];
		Transform[2][3] = ElementData[InstanceIndex].InstanceTransform3[3];
	}

	FORCEINLINE_DEBUGGABLE void GetInstanceOriginInternal(int32 InstanceIndex, FVector4 &Origin) const
	{
		FVector4* ElementData = reinterpret_cast<FVector4*>(InstanceOriginDataPtr);
		uint32 CurrentSize = InstanceOriginData->Num() * InstanceOriginData->GetStride();
		check((void*)((&ElementData[InstanceIndex]) + 1) <= (void*)(InstanceOriginDataPtr + CurrentSize));
		check((void*)((&ElementData[InstanceIndex]) + 0) >= (void*)(InstanceOriginDataPtr));

		Origin = ElementData[InstanceIndex];
	}

	FORCEINLINE_DEBUGGABLE void GetInstanceLightMapDataInternal(int32 InstanceIndex, FVector4 &LightmapData) const
	{
		FInstanceLightMapVector* ElementData = reinterpret_cast<FInstanceLightMapVector*>(InstanceLightmapDataPtr);
		uint32 CurrentSize = InstanceLightmapData->Num() * InstanceLightmapData->GetStride();
		check((void*)((&ElementData[InstanceIndex]) + 1) <= (void*)(InstanceLightmapDataPtr + CurrentSize));
		check((void*)((&ElementData[InstanceIndex]) + 0) >= (void*)(InstanceLightmapDataPtr));

		LightmapData = FVector4
		(
			float(ElementData[InstanceIndex].InstanceLightmapAndShadowMapUVBias[0]) / 32767.0f, 
			float(ElementData[InstanceIndex].InstanceLightmapAndShadowMapUVBias[1]) / 32767.0f,
			float(ElementData[InstanceIndex].InstanceLightmapAndShadowMapUVBias[2]) / 32767.0f,
			float(ElementData[InstanceIndex].InstanceLightmapAndShadowMapUVBias[3]) / 32767.0f
		);
	}

	template<typename T>
	FORCEINLINE_DEBUGGABLE void SetInstanceTransformInternal(int32 InstanceIndex, FVector4(Transform)[3]) const
	{
		FInstanceTransformMatrix<T>* ElementData = reinterpret_cast<FInstanceTransformMatrix<T>*>(InstanceTransformDataPtr);
		uint32 CurrentSize = InstanceTransformData->Num() * InstanceTransformData->GetStride();
		check((void*)((&ElementData[InstanceIndex]) + 1) <= (void*)(InstanceTransformDataPtr + CurrentSize));
		check((void*)((&ElementData[InstanceIndex]) + 0) >= (void*)(InstanceTransformDataPtr));

		ElementData[InstanceIndex].InstanceTransform1[0] = Transform[0][0];
		ElementData[InstanceIndex].InstanceTransform1[1] = Transform[0][1];
		ElementData[InstanceIndex].InstanceTransform1[2] = Transform[0][2];
		ElementData[InstanceIndex].InstanceTransform1[3] = Transform[0][3];

		ElementData[InstanceIndex].InstanceTransform2[0] = Transform[1][0];
		ElementData[InstanceIndex].InstanceTransform2[1] = Transform[1][1];
		ElementData[InstanceIndex].InstanceTransform2[2] = Transform[1][2];
		ElementData[InstanceIndex].InstanceTransform2[3] = Transform[1][3];

		ElementData[InstanceIndex].InstanceTransform3[0] = Transform[2][0];
		ElementData[InstanceIndex].InstanceTransform3[1] = Transform[2][1];
		ElementData[InstanceIndex].InstanceTransform3[2] = Transform[2][2];
		ElementData[InstanceIndex].InstanceTransform3[3] = Transform[2][3];
	}

	FORCEINLINE_DEBUGGABLE void SetInstanceOriginInternal(int32 InstanceIndex, const FVector4& Origin) const
	{
		FVector4* ElementData = reinterpret_cast<FVector4*>(InstanceOriginDataPtr);
		uint32 CurrentSize = InstanceOriginData->Num() * InstanceOriginData->GetStride();
		check((void*)((&ElementData[InstanceIndex]) + 1) <= (void*)(InstanceOriginDataPtr + CurrentSize));
		check((void*)((&ElementData[InstanceIndex]) + 0) >= (void*)(InstanceOriginDataPtr));

		ElementData[InstanceIndex] = Origin;
	}
	

	FORCEINLINE_DEBUGGABLE void SetInstanceLightMapDataInternal(int32 InstanceIndex, const FVector4& LightmapData) const
	{
		FInstanceLightMapVector* ElementData = reinterpret_cast<FInstanceLightMapVector*>(InstanceLightmapDataPtr);
		uint32 CurrentSize = InstanceLightmapData->Num() * InstanceLightmapData->GetStride();
		check((void*)((&ElementData[InstanceIndex]) + 1) <= (void*)(InstanceLightmapDataPtr + CurrentSize));
		check((void*)((&ElementData[InstanceIndex]) + 0) >= (void*)(InstanceLightmapDataPtr));

		ElementData[InstanceIndex].InstanceLightmapAndShadowMapUVBias[0] = FMath::Clamp<int32>(FMath::TruncToInt(LightmapData.X * 32767.0f), MIN_int16, MAX_int16);
		ElementData[InstanceIndex].InstanceLightmapAndShadowMapUVBias[1] = FMath::Clamp<int32>(FMath::TruncToInt(LightmapData.Y * 32767.0f), MIN_int16, MAX_int16);
		ElementData[InstanceIndex].InstanceLightmapAndShadowMapUVBias[2] = FMath::Clamp<int32>(FMath::TruncToInt(LightmapData.Z * 32767.0f), MIN_int16, MAX_int16);
		ElementData[InstanceIndex].InstanceLightmapAndShadowMapUVBias[3] = FMath::Clamp<int32>(FMath::TruncToInt(LightmapData.W * 32767.0f), MIN_int16, MAX_int16);
	}

	void AllocateBuffers(int32 InNumInstances, EResizeBufferFlags BufferFlags = EResizeBufferFlags::None)
	{
		delete InstanceOriginData;
		InstanceOriginDataPtr = nullptr;
		
		delete InstanceTransformData;
		InstanceTransformDataPtr = nullptr;
		
		delete InstanceLightmapData;
		InstanceLightmapDataPtr = nullptr;
		 		
		InstanceOriginData = new TStaticMeshVertexData<FVector4>();
		InstanceOriginData->ResizeBuffer(InNumInstances, BufferFlags);

		InstanceLightmapData = new TStaticMeshVertexData<FInstanceLightMapVector>();
		InstanceLightmapData->ResizeBuffer(InNumInstances, BufferFlags);
		if (bUseHalfFloat)
		{
			InstanceTransformData = new TStaticMeshVertexData<FInstanceTransformMatrix<FFloat16>>();
		}
		else
		{
			InstanceTransformData = new TStaticMeshVertexData<FInstanceTransformMatrix<float>>();
		}
		InstanceTransformData->ResizeBuffer(InNumInstances, BufferFlags);
	}

	FStaticMeshVertexDataInterface* InstanceOriginData = nullptr;
	uint8* InstanceOriginDataPtr = nullptr;

	FStaticMeshVertexDataInterface* InstanceTransformData = nullptr;
	uint8* InstanceTransformDataPtr = nullptr;

	FStaticMeshVertexDataInterface* InstanceLightmapData = nullptr;
	uint8* InstanceLightmapDataPtr = nullptr;	

	int32 NumInstances = 0;
	bool bUseHalfFloat = false;
protected:
	int32 NumCustomData = 0;
};

template<uint32 PerInstanceDataNum = 1>
class FStaticMeshInstanceData_CustomData : public FStaticMeshInstanceData
{
public:
	struct FDataType
	{
		FVector4 Data[PerInstanceDataNum > 0 ? PerInstanceDataNum : 1];

		friend FArchive& operator<<(FArchive& Ar, FDataType& Elem)
		{
			for (int32 i = 0; i < UE_ARRAY_COUNT(Data); ++i)
				Ar << Elem.Data[i];
			return Ar;
		}
	};

	FStaticMeshInstanceData_CustomData()
	{
		NumCustomData = PerInstanceDataNum;
	}
	
	FStaticMeshInstanceData_CustomData(bool bInUseHalfFloat)
	:	FStaticMeshInstanceData(bInUseHalfFloat)
	{
		AllocateBuffers(0);
		NumCustomData = PerInstanceDataNum;
	}

	~FStaticMeshInstanceData_CustomData()
	{
		FStaticMeshInstanceData::~FStaticMeshInstanceData();

		delete InstanceShadowFakeryData;
	}

	FORCEINLINE_DEBUGGABLE void SetInstance(int32 InstanceIndex, const FMatrix& Transform, const TArray<FVector4>& ShadowFakeryParam, float RandomInstanceID, const FVector2D& LightmapUVBias, const FVector2D& ShadowmapUVBias)
	{
		FStaticMeshInstanceData::SetInstance(InstanceIndex, Transform, RandomInstanceID, LightmapUVBias, ShadowmapUVBias);

		SetInstanceShadowFakeryInternal(InstanceIndex, ShadowFakeryParam);
	}

	FORCEINLINE_DEBUGGABLE void SetInstanceShadowFakeryInternal(int32 InstanceIndex, const TArray<FVector4>& ShadowFakery) const
	{
		if (PerInstanceDataNum == 0)return;
		FVector4* ElementData = reinterpret_cast<FVector4*>(InstanceShadowFakeryDataPtr);
		uint32 CurrentSize = InstanceShadowFakeryData->Num() * InstanceShadowFakeryData->GetStride();
		check((void*)((&ElementData[InstanceIndex * PerInstanceDataNum]) + 1) <= (void*)(InstanceShadowFakeryDataPtr + CurrentSize));
		check((void*)((&ElementData[InstanceIndex * PerInstanceDataNum]) + 0) >= (void*)(InstanceShadowFakeryDataPtr));
		check(ShadowFakery.Num() >= PerInstanceDataNum);

		for (int32 i = 0; i < PerInstanceDataNum; ++i)
		{
			ElementData[InstanceIndex++] = ShadowFakery[i];
		}
	}
	void AllocateInstances(int32 InNumInstances, EResizeBufferFlags BufferFlags, bool DestroyExistingInstances)override
	{
		FStaticMeshInstanceData::AllocateInstances(InNumInstances, BufferFlags, DestroyExistingInstances);
		if (DestroyExistingInstances)
		{
			InstanceShadowFakeryData->Empty(InNumInstances);
		}

		InstanceShadowFakeryData->ResizeBuffer(InNumInstances, BufferFlags);
		InstanceShadowFakeryDataPtr = InstanceShadowFakeryData->GetDataPointer();
	}

	void AllocateBuffers(int32 InNumInstances, EResizeBufferFlags BufferFlags = EResizeBufferFlags::None)
	{
		//FStaticMeshInstanceData::AllocateBuffers(InNumInstances, BufferFlags);

		delete InstanceShadowFakeryData;
		InstanceShadowFakeryData = nullptr;
		
		InstanceShadowFakeryData = new TStaticMeshVertexData<FDataType>();
		
		InstanceShadowFakeryData->ResizeBuffer(InNumInstances, BufferFlags);
	}
	
	FORCEINLINE_DEBUGGABLE void SwapInstance(int32 Index1, int32 Index2)
	{
		FStaticMeshInstanceData::SwapInstance(Index1, Index2);
		/*{
			FVector4* ElementData = reinterpret_cast<FVector4*>(InstanceShadowFakeryDataPtr);
			uint32 CurrentSize = InstanceShadowFakeryData->Num() * InstanceShadowFakeryData->GetStride();
			check((void*)((&ElementData[Index1]) + 1) <= (void*)(InstanceShadowFakeryDataPtr + CurrentSize));
			check((void*)((&ElementData[Index1]) + 0) >= (void*)(InstanceShadowFakeryDataPtr));
			check((void*)((&ElementData[Index2]) + 1) <= (void*)(InstanceShadowFakeryDataPtr + CurrentSize));
			check((void*)((&ElementData[Index2]) + 0) >= (void*)(InstanceShadowFakeryDataPtr));

			FVector4 TempStore = ElementData[Index1];
			ElementData[Index1] = ElementData[Index2];
			ElementData[Index2] = TempStore;
		}*/
	}

	FORCEINLINE_DEBUGGABLE void SetAllowCPUAccess(bool InNeedsCPUAccess)
	{
		FStaticMeshInstanceData::SetAllowCPUAccess(InNeedsCPUAccess);

		if (InstanceShadowFakeryData)
		{
			InstanceShadowFakeryData->GetResourceArray()->SetAllowCPUAccess(InNeedsCPUAccess);
		}
	}

	FORCEINLINE_DEBUGGABLE FResourceArrayInterface* GetShadowFakeryResourceArray()override
	{
		return InstanceShadowFakeryData->GetResourceArray();
	}

	FORCEINLINE_DEBUGGABLE uint32 GetShadowFakeryStride()
	{
		return InstanceShadowFakeryData->GetStride();
	}

	FORCEINLINE_DEBUGGABLE SIZE_T GetResourceSize() const override
	{
		return	FStaticMeshInstanceData::GetResourceSize() +
			InstanceShadowFakeryData->GetResourceSize();
	}

	FStaticMeshVertexDataInterface* InstanceShadowFakeryData = nullptr;
	uint8* InstanceShadowFakeryDataPtr = nullptr;
};
//end
	
#if WITH_EDITOR
/**
 * Remaps painted vertex colors when the renderable mesh has changed.
 * @param InPaintedVertices - The original position and normal for each painted vertex.
 * @param InOverrideColors - The painted vertex colors.
 * @param NewPositions - Positions of the new renderable mesh on which colors are to be mapped.
 * @param OptionalVertexBuffer - [optional] Vertex buffer containing vertex normals for the new mesh.
 * @param OutOverrideColors - Will contain vertex colors for the new mesh.
 */
ENGINE_API void RemapPaintedVertexColors(
	const TArray<FPaintedVertex>& InPaintedVertices,
	const FColorVertexBuffer* InOverrideColors,
	const FPositionVertexBuffer& OldPositions,
	const FStaticMeshVertexBuffer& OldVertexBuffer,
	const FPositionVertexBuffer& NewPositions,	
	const FStaticMeshVertexBuffer* OptionalVertexBuffer,
	TArray<FColor>& OutOverrideColors
	);
#endif // #if WITH_EDITOR
