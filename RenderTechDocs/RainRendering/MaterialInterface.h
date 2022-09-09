// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Misc/Guid.h"
#include "Engine/EngineTypes.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/ScriptMacros.h"
#include "RenderCommandFence.h"
#include "SceneTypes.h"
#include "RHI.h"
#include "Engine/BlendableInterface.h"
#include "Materials/MaterialLayersFunctions.h"
#include "Interfaces/Interface_AssetUserData.h"
#include "MaterialSceneTextureId.h"
#include "Materials/MaterialRelevance.h"
#if WITH_CHAOS
#include "Physics/PhysicsInterfaceCore.h"
#endif
#include "MaterialInterface.generated.h"

class FMaterialCompiler;
class FMaterialRenderProxy;
class FMaterialResource;
class UMaterial;
class UPhysicalMaterial;
class UPhysicalMaterialMask;
class USubsurfaceProfile;
class UTexture;
struct FMaterialParameterInfo;
struct FMaterialResourceLocOnDisk;
#if WITH_EDITORONLY_DATA
struct FParameterChannelNames;
#endif

typedef TArray<FMaterialResource*> FMaterialResourceDeferredDeletionArray;

UENUM(BlueprintType)
enum EMaterialUsage
{
	MATUSAGE_SkeletalMesh,
	MATUSAGE_ParticleSprites,
	MATUSAGE_BeamTrails,
	MATUSAGE_MeshParticles,
	MATUSAGE_StaticLighting,
	MATUSAGE_MorphTargets,
	MATUSAGE_SplineMesh,
	MATUSAGE_InstancedStaticMeshes,
	MATUSAGE_GeometryCollections,
	MATUSAGE_Clothing,
	MATUSAGE_NiagaraSprites,
	MATUSAGE_NiagaraRibbons,
	MATUSAGE_NiagaraMeshParticles,
	MATUSAGE_GeometryCache,
	MATUSAGE_Water,
	MATUSAGE_HairStrands,
	MATUSAGE_LidarPointCloud,

	MATUSAGE_MAX,
};

/** 
 *	UMaterial interface settings for Lightmass
 */
USTRUCT()
struct FLightmassMaterialInterfaceSettings
{
	GENERATED_USTRUCT_BODY()

	/** Scales the emissive contribution of this material to static lighting. */
	UPROPERTY()
	float EmissiveBoost;

	/** Scales the diffuse contribution of this material to static lighting. */
	UPROPERTY(EditAnywhere, Category=Material)
	float DiffuseBoost;

	/** 
	 * Scales the resolution that this material's attributes were exported at. 
	 * This is useful for increasing material resolution when details are needed.
	 */
	UPROPERTY(EditAnywhere, Category=Material)
	float ExportResolutionScale;

	/** If true, forces translucency to cast static shadows as if the material were masked. */
	UPROPERTY(EditAnywhere, Category = Material)
	uint8 bCastShadowAsMasked : 1;

	/** Boolean override flags - only used in MaterialInstance* cases. */
	/** If true, override the bCastShadowAsMasked setting of the parent material. */
	UPROPERTY()
	uint8 bOverrideCastShadowAsMasked:1;

	/** If true, override the emissive boost setting of the parent material. */
	UPROPERTY()
	uint8 bOverrideEmissiveBoost:1;

	/** If true, override the diffuse boost setting of the parent material. */
	UPROPERTY()
	uint8 bOverrideDiffuseBoost:1;

	/** If true, override the export resolution scale setting of the parent material. */
	UPROPERTY()
	uint8 bOverrideExportResolutionScale:1;

	FLightmassMaterialInterfaceSettings()
		: EmissiveBoost(1.0f)
		, DiffuseBoost(1.0f)
		, ExportResolutionScale(1.0f)
		, bCastShadowAsMasked(false)
		, bOverrideCastShadowAsMasked(false)
		, bOverrideEmissiveBoost(false)
		, bOverrideDiffuseBoost(false)
		, bOverrideExportResolutionScale(false)
	{}
};

/** 
 * This struct holds data about how a texture is sampled within a material.
 */
USTRUCT()
struct FMaterialTextureInfo
{
	GENERATED_USTRUCT_BODY()

	FMaterialTextureInfo() : SamplingScale(0), UVChannelIndex(INDEX_NONE)
	{
#if WITH_EDITORONLY_DATA
		TextureIndex = INDEX_NONE;
#endif
	}

	FMaterialTextureInfo(ENoInit) {}

	/** The scale used when sampling the texture */
	UPROPERTY()
	float SamplingScale;

	/** The coordinate index used when sampling the texture */
	UPROPERTY()
	int32 UVChannelIndex;

	/** The texture name. Used for debugging and also to for quick matching of the entries. */
	UPROPERTY()
	FName TextureName;

#if WITH_EDITORONLY_DATA
	/** The reference to the texture, used to keep the TextureName valid even if it gets renamed. */
	UPROPERTY()
	FSoftObjectPath TextureReference;

	/** 
	  * The texture index in the material resource the data was built from.
	  * This must be transient as it depends on which shader map was used for the build.  
	  */
	UPROPERTY(transient)
	int32 TextureIndex;
#endif

	/** Return whether the data is valid to be used */
	ENGINE_API bool IsValid(bool bCheckTextureIndex = false) const; 
};

struct TMicRecursionGuard
{
	inline TMicRecursionGuard() = default;
	inline TMicRecursionGuard(const TMicRecursionGuard& Parent)
		: MaterialInterface(nullptr)
		, PreviousLink(&Parent)
	{
	}

	inline void Set(class UMaterialInterface const* InMaterialInterface)
	{
		check(MaterialInterface == nullptr);
		MaterialInterface = InMaterialInterface;
	}

	inline bool Contains(class UMaterialInterface const* InMaterialInterface)
	{
		TMicRecursionGuard const* Link = this;
		do
		{
			if (Link->MaterialInterface == InMaterialInterface)
			{
				return true;
			}
			Link = Link->PreviousLink;
		} while (Link);
		return false;
	}

private:
	void const* MaterialInterface = nullptr;
	TMicRecursionGuard const* PreviousLink = nullptr;
};

UCLASS(abstract, BlueprintType, MinimalAPI, HideCategories = (Thumbnail))
class UMaterialInterface : public UObject, public IBlendableInterface, public IInterface_AssetUserData
{
	GENERATED_UCLASS_BODY()

	/** SubsurfaceProfile, for Screen Space Subsurface Scattering */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Material, meta = (DisplayName = "Subsurface Profile"))
	class USubsurfaceProfile* SubsurfaceProfile;

	/* -------------------------- */

	/** A fence to track when the primitive is no longer used as a parent */
	FRenderCommandFence ParentRefFence;

protected:
	/** The Lightmass settings for this object. */
	UPROPERTY(EditAnywhere, Category=Lightmass)
	struct FLightmassMaterialInterfaceSettings LightmassSettings;

protected:
#if WITH_EDITORONLY_DATA
	/** Because of redirector, the texture names need to be resorted at each load in case they changed. */
	UPROPERTY(transient)
	bool bTextureStreamingDataSorted;
	UPROPERTY()
	int32 TextureStreamingDataVersion;
#endif

	/** Data used by the texture streaming to know how each texture is sampled by the material. Sorted by names for quick access. */
	UPROPERTY()
	TArray<FMaterialTextureInfo> TextureStreamingData;

	/** Array of user data stored with the asset */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Instanced, Category = Material)
	TArray<UAssetUserData*> AssetUserData;

private:
	/** Feature levels to force to compile. */
	uint32 FeatureLevelsToForceCompile;

public:

	//~ Begin IInterface_AssetUserData Interface
	ENGINE_API virtual void AddAssetUserData(UAssetUserData* InUserData) override;
	ENGINE_API virtual void RemoveUserDataOfClass(TSubclassOf<UAssetUserData> InUserDataClass) override;
	ENGINE_API virtual UAssetUserData* GetAssetUserDataOfClass(TSubclassOf<UAssetUserData> InUserDataClass) override;
	//~ End IInterface_AssetUserData Interface

#if WITH_EDITORONLY_DATA
	/** List of all used but missing texture indices in TextureStreamingData. Used for visualization / debugging only. */
	UPROPERTY(transient)
	TArray<FMaterialTextureInfo> TextureStreamingDataMissingEntries;

	/** The mesh used by the material editor to preview the material.*/
	UPROPERTY(EditAnywhere, Category=Previewing, meta=(AllowedClasses="StaticMesh,SkeletalMesh", ExactClass="true"))
	FSoftObjectPath PreviewMesh;

	/** Information for thumbnail rendering */
	UPROPERTY(VisibleAnywhere, Instanced, Category = Thumbnail)
	class UThumbnailInfo* ThumbnailInfo;

	UPROPERTY()
	TMap<FString, bool> LayerParameterExpansion;

	UPROPERTY()
	TMap<FString, bool> ParameterOverviewExpansion;

	/** Importing data and options used for this material */
	UPROPERTY(EditAnywhere, Instanced, Category = ImportSettings)
	class UAssetImportData* AssetImportData;

private:
	/** Unique ID for this material, used for caching during distributed lighting */
	UPROPERTY()
	FGuid LightingGuid;

#endif // WITH_EDITORONLY_DATA

private:
	/** Feature level bitfield to compile for all materials */
	ENGINE_API static uint32 FeatureLevelsForAllMaterials;
public:
	/** Set which feature levels this material instance should compile. GMaxRHIFeatureLevel is always compiled! */
	ENGINE_API void SetFeatureLevelToCompile(ERHIFeatureLevel::Type FeatureLevel, bool bShouldCompile);

	/** Set which feature levels _all_ materials should compile to. GMaxRHIFeatureLevel is always compiled. */
	ENGINE_API static void SetGlobalRequiredFeatureLevel(ERHIFeatureLevel::Type FeatureLevel, bool bShouldCompile);

	//~ Begin UObject Interface.
	ENGINE_API virtual void BeginDestroy() override;
	ENGINE_API virtual bool IsReadyForFinishDestroy() override;
	ENGINE_API virtual void PostLoad() override;
	ENGINE_API virtual void PostDuplicate(bool bDuplicateForPIE) override;
	ENGINE_API virtual void PostCDOContruct() override;

#if WITH_EDITOR
	ENGINE_API virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	ENGINE_API virtual void GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const override;
#endif // WITH_EDITOR
	//~ End UObject Interface.

	//~ Begin Begin Interface IBlendableInterface
	ENGINE_API virtual void OverrideBlendableSettings(class FSceneView& View, float Weight) const override;
	//~ Begin End Interface IBlendableInterface

	/** Walks up parent chain and finds the base Material that this is an instance of. Just calls the virtual GetMaterial() */
	UFUNCTION(BlueprintCallable, Category="Rendering|Material")
	ENGINE_API UMaterial* GetBaseMaterial();

	/**
	 * Get the material which we are instancing.
	 * Walks up parent chain and finds the base Material that this is an instance of. 
	 */
	virtual class UMaterial* GetMaterial() PURE_VIRTUAL(UMaterialInterface::GetMaterial,return NULL;);
	/**
	 * Get the material which we are instancing.
	 * Walks up parent chain and finds the base Material that this is an instance of. 
	 */
	virtual const class UMaterial* GetMaterial() const PURE_VIRTUAL(UMaterialInterface::GetMaterial,return NULL;);

	/**
	 * Same as above, but can be called concurrently
	 */
	virtual const class UMaterial* GetMaterial_Concurrent(TMicRecursionGuard RecursionGuard = TMicRecursionGuard()) const PURE_VIRTUAL(UMaterialInterface::GetMaterial_Concurrent,return NULL;);

	/**
	* Test this material for dependency on a given material.
	* @param	TestDependency - The material to test for dependency upon.
	* @return	True if the material is dependent on TestDependency.
	*/
	virtual bool IsDependent(UMaterialInterface* TestDependency) { return TestDependency == this; }

	/**
	* Return a pointer to the FMaterialRenderProxy used for rendering.
	* @param	Selected	specify true to return an alternate material used for rendering this material when part of a selection
	*						@note: only valid in the editor!
	* @return	The resource to use for rendering this material instance.
	*/
	virtual class FMaterialRenderProxy* GetRenderProxy() const PURE_VIRTUAL(UMaterialInterface::GetRenderProxy,return NULL;);

	/**
	* Return a pointer to the physical material used by this material instance.
	* @return The physical material.
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Material")
	virtual UPhysicalMaterial* GetPhysicalMaterial() const PURE_VIRTUAL(UMaterialInterface::GetPhysicalMaterial,return NULL;);

	/**
	 * Return a pointer to the physical material mask used by this material instance.
	 * @return The physical material.
	 */
	UFUNCTION(BlueprintCallable, Category = "Physics|Material")
	virtual UPhysicalMaterialMask* GetPhysicalMaterialMask() const PURE_VIRTUAL(UMaterialInterface::GetPhysicalMaterialMask, return nullptr;);

	/**
	 * Return a pointer to the physical material from mask map at given index.
	 * @return The physical material.
	 */
	UFUNCTION(BlueprintCallable, Category = "Physics|Material")
	virtual UPhysicalMaterial* GetPhysicalMaterialFromMap(int32 Index) const PURE_VIRTUAL(UMaterialInterface::GetPhysicalMaterialFromMap, return nullptr;);

	/** Return the textures used to render this material. */
	virtual void GetUsedTextures(TArray<UTexture*>& OutTextures, EMaterialQualityLevel::Type QualityLevel, bool bAllQualityLevels, ERHIFeatureLevel::Type FeatureLevel, bool bAllFeatureLevels) const
		PURE_VIRTUAL(UMaterialInterface::GetUsedTextures,);

	/** 
	* Return the textures used to render this material and the material indices bound to each. 
	* Because material indices can change for each shader, this is limited to a single platform and quality level.
	* An empty array in OutIndices means the index is undefined.
	*/
	ENGINE_API virtual void GetUsedTexturesAndIndices(TArray<UTexture*>& OutTextures, TArray< TArray<int32> >& OutIndices, EMaterialQualityLevel::Type QualityLevel, ERHIFeatureLevel::Type FeatureLevel) const;

	/**
	 * Override a specific texture (transient)
	 *
	 * @param InTextureToOverride The texture to override
	 * @param OverrideTexture The new texture to use
	 */
	virtual void OverrideTexture(const UTexture* InTextureToOverride, UTexture* OverrideTexture, ERHIFeatureLevel::Type InFeatureLevel) PURE_VIRTUAL(UMaterialInterface::OverrideTexture, return;);

	/** 
	 * Overrides the default value of the given parameter (transient).  
	 * This is used to implement realtime previewing of parameter defaults. 
	 * Handles updating dependent MI's and cached uniform expressions.
	 */
	virtual void OverrideVectorParameterDefault(const FHashedMaterialParameterInfo& ParameterInfo, const FLinearColor& Value, bool bOverride, ERHIFeatureLevel::Type FeatureLevel) PURE_VIRTUAL(UMaterialInterface::OverrideTexture, return;);
	virtual void OverrideScalarParameterDefault(const FHashedMaterialParameterInfo& ParameterInfo, float Value, bool bOverride, ERHIFeatureLevel::Type FeatureLevel) PURE_VIRTUAL(UMaterialInterface::OverrideTexture, return;);

	/**
	 * DEPRECATED: Returns default value of the given parameter
	 */
	UE_DEPRECATED(4.19, "This function is deprecated. Use GetScalarParameterDefaultValue instead.")
	virtual float GetScalarParameterDefault(const FHashedMaterialParameterInfo& ParameterInfo, ERHIFeatureLevel::Type FeatureLevel)
	{
		float Value;
		GetScalarParameterDefaultValue(ParameterInfo, Value);
		return Value;
	};
	/**
	 * Checks if the material can be used with the given usage flag.  
	 * If the flag isn't set in the editor, it will be set and the material will be recompiled with it.
	 * @param Usage - The usage flag to check
	 * @return bool - true if the material can be used for rendering with the given type.
	 */
	virtual bool CheckMaterialUsage(const EMaterialUsage Usage) PURE_VIRTUAL(UMaterialInterface::CheckMaterialUsage,return false;);
	/**
	 * Same as above but is valid to call from any thread. In the editor, this might spin and stall for a shader compile
	 */
	virtual bool CheckMaterialUsage_Concurrent(const EMaterialUsage Usage) const PURE_VIRTUAL(UMaterialInterface::CheckMaterialUsage,return false;);

	/**
	 * Get the static permutation resource if the instance has one
	 * @return - the appropriate FMaterialResource if one exists, otherwise NULL
	 */
	virtual FMaterialResource* GetMaterialResource(ERHIFeatureLevel::Type InFeatureLevel, EMaterialQualityLevel::Type QualityLevel = EMaterialQualityLevel::Num) { return NULL; }

	/**
	 * Get the static permutation resource if the instance has one
	 * @return - the appropriate FMaterialResource if one exists, otherwise NULL
	 */
	virtual const FMaterialResource* GetMaterialResource(ERHIFeatureLevel::Type InFeatureLevel, EMaterialQualityLevel::Type QualityLevel = EMaterialQualityLevel::Num) const { return NULL; }

#if WITH_EDITORONLY_DATA
	struct FStaticParamEvaluationContext
	{
	public:
		FStaticParamEvaluationContext(int32 InParameterNum, const FHashedMaterialParameterInfo* InParameterInfos) : PendingParameterNum(InParameterNum), ParameterInfos(InParameterInfos) { PendingParameters.Add(true, PendingParameterNum); ResolvedByOverride.Add(false, PendingParameterNum); }

		const FHashedMaterialParameterInfo* GetParameterInfo(int32 ParamIndex) const { check(ParamIndex >= 0 && ParamIndex < PendingParameters.Num()); return ParameterInfos + ParamIndex; }
		bool AllResolved() const { return PendingParameterNum == 0; }
		bool IsResolved(int32 ParamIndex) const { return !PendingParameters[ParamIndex]; }
		bool IsResolvedByOverride(int32 ParamIndex) const { return ResolvedByOverride[ParamIndex]; }
		void MarkParameterResolved(int32 ParamIndex, bool bIsOverride);
		int32 GetPendingParameterNum() const { return PendingParameterNum; }
		int32 GetTotalParameterNum() const { return PendingParameters.Num(); }

		/**
		 * Perform an operation on the pending parameters until performed on all pending parameters, or until the operation asks to stop iterating by returning false.
	 	 * @param Op - The operation to perform on each pending parameter.  Accepts the index for the parameter and the parameter info associated with it.  If the Op returns true, we keep iterating if there are more pending parameters, if it returns false, we stop immediately after this Op.
		 */
		void ForEachPendingParameter(TFunctionRef<bool(int32 ParamIndex, const FHashedMaterialParameterInfo& ParamInfo)> Op);
	private:
		TBitArray<> PendingParameters;
		TBitArray<> ResolvedByOverride;
		int32 PendingParameterNum;
		const FHashedMaterialParameterInfo* ParameterInfos;
	};

	/**
	* Get the value of the given static switch parameter
	*
	* @param	ParameterName	The name of the static switch parameter
	* @param	OutValue		Will contain the value of the parameter if successful
	* @return					True if successful
	*/
	ENGINE_API bool GetStaticSwitchParameterValue(const FHashedMaterialParameterInfo& ParameterInfo,bool &OutValue,FGuid &OutExpressionGuid, bool bOveriddenOnly = false, bool bCheckParent = true) const;

	/**
	* Get the values of the given set of static switch parameters.
	*
	* @param	EvalContext			The evaluation context used while determining parameter values.
	* @param	OutValues			If successful, will contain the value of the requested parameters.  Must be pre-sized to fit at least all parameter values in the evaluation.  If unsuccessful, may still be written to with intermediate values.
	* @param	OutExpressionGuids	If successful, will contain the identifier of the owning expression of the requested parameter values.  Must be non null and pre-sized to fit at least all parameter values in the evaluation.  If unsuccessful, may still be written to with intermediate values.
	* @return						True if successfully obtained ALL parameter values requested
	*/
	virtual bool GetStaticSwitchParameterValues(FStaticParamEvaluationContext& EvalContext, TBitArray<>& OutValues, FGuid* OutExpressionGuids, bool bCheckParent = true) const
		PURE_VIRTUAL(UMaterialInterface::GetStaticSwitchParameterValues,return false;);

	/**
	* Get the value of the given static component mask parameter
	*
	* @param	ParameterName	The name of the parameter
	* @param	R, G, B, A		Will contain the values of the parameter if successful
	* @return					True if successful
	*/
	ENGINE_API bool GetStaticComponentMaskParameterValue(const FHashedMaterialParameterInfo& ParameterInfo, bool& R, bool& G, bool& B, bool& A, FGuid& OutExpressionGuid, bool bOveriddenOnly = false, bool bCheckParent = true) const;

	/**
	* Get the values of the given set of static component mask parameters
	*
	* @param	EvalContext				The evaluation context used while determining parameter values
	* @param	OutRGBAOrderedValues	If successful, will contain the value of the requested parameters.  Must be pre-sized to fit at least four bits for all parameter values in the evaluation.  If unsuccessful, may still be written to with intermediate values.
	* @param	OutExpressionGuids		If successful, will contain the identifier of the owning expression of the requested parameter values.  Must be non null and pre-sized to fit at least all parameter values in the evaluation.  If unsuccessful, may still be written to with intermediate values.
	* @return							True if successfully obtained ALL parameter values requested
	*/
	virtual bool GetStaticComponentMaskParameterValues(FStaticParamEvaluationContext& EvalContext, TBitArray<>& OutRGBAOrderedValues, FGuid* OutExpressionGuids, bool bCheckParent = true) const
		PURE_VIRTUAL(UMaterialInterface::GetStaticComponentMaskParameterValues,return false;);

	/**
	* Get the value of the given static material layers parameter
	*
	* @param	ParameterName	The name of the material layer parameter
	* @param	OutValue		Will contain the value of the parameter if successful
	* @return					True if successful
	*/
	virtual bool GetMaterialLayersParameterValue(const FHashedMaterialParameterInfo& ParameterInfo, FMaterialLayersFunctions& OutLayers, FGuid& OutExpressionGuid, bool bCheckParent = true) const
		PURE_VIRTUAL(UMaterialInterface::GetMaterialLayersParameterValue, return false;);
#endif // WITH_EDITORONLY_DATA

	/**
	* Get the weightmap index of the given terrain layer weight parameter
	*
	* @param	ParameterName	The name of the parameter
	* @param	OutWeightmapIndex	Will contain the values of the parameter if successful
	* @return					True if successful
	*/
	virtual bool GetTerrainLayerWeightParameterValue(const FHashedMaterialParameterInfo& ParameterInfo, int32& OutWeightmapIndex, FGuid &OutExpressionGuid) const
		PURE_VIRTUAL(UMaterialInterface::GetTerrainLayerWeightParameterValue,return false;);

#if WITH_EDITOR
	/**
	* Get the sort priority index of the given parameter
	*
	* @param	ParameterName	The name of the parameter
	* @param	OutSortPriority	Will contain the sort priority of the parameter if successful
	* @return					True if successful
	*/
	virtual bool GetParameterSortPriority(const FHashedMaterialParameterInfo& ParameterInfo, int32& OutSortPriority, const TArray<struct FStaticMaterialLayersParameter>* MaterialLayersParameters = nullptr) const
		PURE_VIRTUAL(UMaterialInterface::GetParameterSortPriority, return false;);
#endif

	/**
	* Get the sort priority index of the given parameter group
	*
	* @param	InGroupName	The name of the parameter group
	* @param	OutSortPriority	Will contain the sort priority of the parameter group if successful
	* @return					True if successful
	*/
	virtual bool GetGroupSortPriority(const FString& InGroupName, int32& OutSortPriority) const
		PURE_VIRTUAL(UMaterialInterface::GetGroupSortPriority, return false;);

	virtual void GetAllScalarParameterInfo(TArray<FMaterialParameterInfo>& OutParameterInfo, TArray<FGuid>& OutParameterIds) const
		PURE_VIRTUAL(UMaterialInterface::GetAllScalarParameterInfo,return;);
	virtual void GetAllVectorParameterInfo(TArray<FMaterialParameterInfo>& OutParameterInfo, TArray<FGuid>& OutParameterIds) const
		PURE_VIRTUAL(UMaterialInterface::GetAllVectorParameterInfo,return;);
	virtual void GetAllTextureParameterInfo(TArray<FMaterialParameterInfo>& OutParameterInfo, TArray<FGuid>& OutParameterIds) const
		PURE_VIRTUAL(UMaterialInterface::GetAllTextureParameterInfo,return;);
	virtual void GetAllRuntimeVirtualTextureParameterInfo(TArray<FMaterialParameterInfo>& OutParameterInfo, TArray<FGuid>& OutParameterIds) const
		PURE_VIRTUAL(UMaterialInterface::GetAllRuntimeVirtualTextureParameterInfo, return;);
	virtual void GetAllFontParameterInfo(TArray<FMaterialParameterInfo>& OutParameterInfo, TArray<FGuid>& OutParameterIds) const
		PURE_VIRTUAL(UMaterialInterface::GetAllFontParameterInfo,return;);

#if WITH_EDITORONLY_DATA
	virtual void GetAllMaterialLayersParameterInfo(TArray<FMaterialParameterInfo>& OutParameterInfo, TArray<FGuid>& OutParameterIds) const
		PURE_VIRTUAL(UMaterialInterface::GetAllMaterialLayersParameterInfo,return;);
	virtual void GetAllStaticSwitchParameterInfo(TArray<FMaterialParameterInfo>& OutParameterInfo, TArray<FGuid>& OutParameterIds) const
		PURE_VIRTUAL(UMaterialInterface::GetAllStaticSwitchParameterInfo,return;);
	virtual void GetAllStaticComponentMaskParameterInfo(TArray<FMaterialParameterInfo>& OutParameterInfo, TArray<FGuid>& OutParameterIds) const
		PURE_VIRTUAL(UMaterialInterface::GetAllStaticComponentMaskParameterInfo,return;);

	virtual bool IterateDependentFunctions(TFunctionRef<bool(UMaterialFunctionInterface*)> Predicate) const
		PURE_VIRTUAL(UMaterialInterface::IterateDependentFunctions,return false;);
	virtual void GetDependentFunctions(TArray<class UMaterialFunctionInterface*>& DependentFunctions) const
		PURE_VIRTUAL(UMaterialInterface::GetDependentFunctions,return;);
#endif // WITH_EDITORONLY_DATA

	virtual bool GetScalarParameterDefaultValue(const FHashedMaterialParameterInfo& ParameterInfo, float& OutValue, bool bOveriddenOnly = false, bool bCheckOwnedGlobalOverrides = false) const
		PURE_VIRTUAL(UMaterialInterface::GetScalarParameterDefaultValue,return false;);
	virtual bool GetVectorParameterDefaultValue(const FHashedMaterialParameterInfo& ParameterInfo, FLinearColor& OutValue, bool bOveriddenOnly = false, bool bCheckOwnedGlobalOverrides = false) const
		PURE_VIRTUAL(UMaterialInterface::GetVectorParameterDefaultValue,return false;);
	virtual bool GetTextureParameterDefaultValue(const FHashedMaterialParameterInfo& ParameterInfo, class UTexture*& OutValue, bool bCheckOwnedGlobalOverrides = false) const
		PURE_VIRTUAL(UMaterialInterface::GetTextureParameterDefaultValue,return false;);
	virtual bool GetRuntimeVirtualTextureParameterDefaultValue(const FHashedMaterialParameterInfo& ParameterInfo, class URuntimeVirtualTexture*& OutValue, bool bCheckOwnedGlobalOverrides = false) const
		PURE_VIRTUAL(UMaterialInterface::GetRuntimeVirtualTextureParameterDefaultValue, return false;);
	virtual bool GetFontParameterDefaultValue(const FHashedMaterialParameterInfo& ParameterInfo, class UFont*& OutFontValue, int32& OutFontPage, bool bCheckOwnedGlobalOverrides = false) const
		PURE_VIRTUAL(UMaterialInterface::GetFontParameterDefaultValue,return false;);
	
#if WITH_EDITOR
	virtual bool GetStaticSwitchParameterDefaultValue(const FHashedMaterialParameterInfo& ParameterInfo, bool& OutValue, FGuid& OutExpressionGuid, bool bCheckOwnedGlobalOverrides = false) const
		PURE_VIRTUAL(UMaterialInterface::GetStaticSwitchParameterDefaultValue,return false;);
	virtual bool GetStaticComponentMaskParameterDefaultValue(const FHashedMaterialParameterInfo& ParameterInfo, bool& OutR, bool& OutG, bool& OutB, bool& OutA, FGuid& OutExpressionGuid, bool bCheckOwnedGlobalOverrides = false) const
		PURE_VIRTUAL(UMaterialInterface::GetStaticComponentMaskParameterDefaultValue,return false;);
#endif // WITH_EDITOR

	virtual int32 GetLayerParameterIndex(EMaterialParameterAssociation Association, UMaterialFunctionInterface * LayerFunction) const
		PURE_VIRTUAL(UMaterialInterface::GetLayerParameterIndex, return INDEX_NONE;);

	/** Get textures referenced by expressions, including nested functions. */
	virtual TArrayView<UObject* const> GetReferencedTextures() const
		PURE_VIRTUAL(UMaterialInterface::GetReferencedTextures,return TArrayView<UObject* const>(););

	virtual void SaveShaderStableKeysInner(const class ITargetPlatform* TP, const struct FStableShaderKeyAndValue& SaveKeyVal)
		PURE_VIRTUAL(UMaterialInterface::SaveShaderStableKeysInner, );

	UFUNCTION(BlueprintCallable, Category = "Rendering|Material")
	ENGINE_API FMaterialParameterInfo GetParameterInfo(EMaterialParameterAssociation Association, FName ParameterName, UMaterialFunctionInterface* LayerFunction) const;

	/** @return The material's relevance. */
	ENGINE_API FMaterialRelevance GetRelevance(ERHIFeatureLevel::Type InFeatureLevel) const;
	/** @return The material's relevance, from concurrent render thread updates. */
	ENGINE_API FMaterialRelevance GetRelevance_Concurrent(ERHIFeatureLevel::Type InFeatureLevel) const;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	/**
	 * Output to the log which materials and textures are used by this material.
	 * @param Indent	Number of tabs to put before the log.
	 */
	ENGINE_API virtual void LogMaterialsAndTextures(FOutputDevice& Ar, int32 Indent) const {}
#endif

private:
	// might get called from game or render thread
	FMaterialRelevance GetRelevance_Internal(const UMaterial* Material, ERHIFeatureLevel::Type InFeatureLevel) const;
public:

	int32 GetWidth() const;
	int32 GetHeight() const;

	const FGuid& GetLightingGuid() const
	{
#if WITH_EDITORONLY_DATA
		return LightingGuid;
#else
		static const FGuid NullGuid( 0, 0, 0, 0 );
		return NullGuid; 
#endif // WITH_EDITORONLY_DATA
	}

	void SetLightingGuid()
	{
#if WITH_EDITORONLY_DATA
		LightingGuid = FGuid::NewGuid();
#endif // WITH_EDITORONLY_DATA
	}

	/**
	 *	Returns all the Guids related to this material. For material instances, this includes the parent hierarchy.
	 *  Used for versioning as parent changes don't update the child instance Guids.
	 *
	 *	@param	bIncludeTextures	Whether to include the referenced texture Guids.
	 *	@param	OutGuids			The list of all resource guids affecting the precomputed lighting system and texture streamer.
	 */
	ENGINE_API virtual void GetLightingGuidChain(bool bIncludeTextures, TArray<FGuid>& OutGuids) const;

	/**
	 *	Check if the textures have changed since the last time the material was
	 *	serialized for Lightmass... Update the lists while in here.
	 *	NOTE: This will mark the package dirty if they have changed.
	 *
	 *	@return	bool	true if the textures have changed.
	 *					false if they have not.
	 */
	virtual bool UpdateLightmassTextureTracking() 
	{ 
		return false; 
	}
	
	/** @return The override bOverrideCastShadowAsMasked setting of the material. */
	inline bool GetOverrideCastShadowAsMasked() const
	{
		return LightmassSettings.bOverrideCastShadowAsMasked;
	}

	/** @return The override emissive boost setting of the material. */
	inline bool GetOverrideEmissiveBoost() const
	{
		return LightmassSettings.bOverrideEmissiveBoost;
	}

	/** @return The override diffuse boost setting of the material. */
	inline bool GetOverrideDiffuseBoost() const
	{
		return LightmassSettings.bOverrideDiffuseBoost;
	}

	/** @return The override export resolution scale setting of the material. */
	inline bool GetOverrideExportResolutionScale() const
	{
		return LightmassSettings.bOverrideExportResolutionScale;
	}

	/** @return	The bCastShadowAsMasked value for this material. */
	virtual bool GetCastShadowAsMasked() const
	{
		return LightmassSettings.bCastShadowAsMasked;
	}

	/** @return	The Emissive boost value for this material. */
	virtual float GetEmissiveBoost() const
	{
		return 
		LightmassSettings.EmissiveBoost;
	}

	/** @return	The Diffuse boost value for this material. */
	virtual float GetDiffuseBoost() const
	{
		return LightmassSettings.DiffuseBoost;
	}

	/** @return	The ExportResolutionScale value for this material. */
	virtual float GetExportResolutionScale() const
	{
		return FMath::Clamp(LightmassSettings.ExportResolutionScale, .1f, 10.0f);
	}

	/** @param	bInOverrideCastShadowAsMasked	The override CastShadowAsMasked setting to set. */
	inline void SetOverrideCastShadowAsMasked(bool bInOverrideCastShadowAsMasked)
	{
		LightmassSettings.bOverrideCastShadowAsMasked = bInOverrideCastShadowAsMasked;
	}

	/** @param	bInOverrideEmissiveBoost	The override emissive boost setting to set. */
	inline void SetOverrideEmissiveBoost(bool bInOverrideEmissiveBoost)
	{
		LightmassSettings.bOverrideEmissiveBoost = bInOverrideEmissiveBoost;
	}

	/** @param bInOverrideDiffuseBoost		The override diffuse boost setting of the parent material. */
	inline void SetOverrideDiffuseBoost(bool bInOverrideDiffuseBoost)
	{
		LightmassSettings.bOverrideDiffuseBoost = bInOverrideDiffuseBoost;
	}

	/** @param bInOverrideExportResolutionScale	The override export resolution scale setting of the parent material. */
	inline void SetOverrideExportResolutionScale(bool bInOverrideExportResolutionScale)
	{
		LightmassSettings.bOverrideExportResolutionScale = bInOverrideExportResolutionScale;
	}

	/** @param	InCastShadowAsMasked	The CastShadowAsMasked value for this material. */
	inline void SetCastShadowAsMasked(bool InCastShadowAsMasked)
	{
		LightmassSettings.bCastShadowAsMasked = InCastShadowAsMasked;
	}

	/** @param	InEmissiveBoost		The Emissive boost value for this material. */
	inline void SetEmissiveBoost(float InEmissiveBoost)
	{
		LightmassSettings.EmissiveBoost = InEmissiveBoost;
	}

	/** @param	InDiffuseBoost		The Diffuse boost value for this material. */
	inline void SetDiffuseBoost(float InDiffuseBoost)
	{
		LightmassSettings.DiffuseBoost = InDiffuseBoost;
	}

	/** @param	InExportResolutionScale		The ExportResolutionScale value for this material. */
	inline void SetExportResolutionScale(float InExportResolutionScale)
	{
		LightmassSettings.ExportResolutionScale = InExportResolutionScale;
	}

#if WITH_EDITOR
	/**
	 *	Get all of the textures in the expression chain for the given property (ie fill in the given array with all textures in the chain).
	 *
	 *	@param	InProperty				The material property chain to inspect, such as MP_BaseColor.
	 *	@param	OutTextures				The array to fill in all of the textures.
	 *	@param	OutTextureParamNames	Optional array to fill in with texture parameter names.
	 *	@param	InStaticParameterSet	Optional static parameter set - if specified only follow StaticSwitches according to its settings
	 *
	 *	@return	bool			true if successful, false if not.
	 */
	virtual bool GetTexturesInPropertyChain(EMaterialProperty InProperty, TArray<UTexture*>& OutTextures,  TArray<FName>* OutTextureParamNames, struct FStaticParameterSet* InStaticParameterSet,
		ERHIFeatureLevel::Type InFeatureLevel = ERHIFeatureLevel::Num, EMaterialQualityLevel::Type InQuality = EMaterialQualityLevel::Num)
		PURE_VIRTUAL(UMaterialInterface::GetTexturesInPropertyChain,return false;);

	ENGINE_API virtual bool GetGroupName(const FHashedMaterialParameterInfo& ParameterInfo, FName& GroupName) const;
	ENGINE_API virtual bool GetParameterDesc(const FHashedMaterialParameterInfo& ParameterInfo, FString& OutDesc, const TArray<struct FStaticMaterialLayersParameter>* MaterialLayersParameters = nullptr) const;
	ENGINE_API virtual bool GetScalarParameterSliderMinMax(const FHashedMaterialParameterInfo& ParameterInfo, float& OutSliderMin, float& OutSliderMax) const;
#endif // WITH_EDITOR
	ENGINE_API virtual bool GetScalarParameterValue(const FHashedMaterialParameterInfo& ParameterInfo, float& OutValue, bool bOveriddenOnly = false) const;
#if WITH_EDITOR
	ENGINE_API virtual bool IsScalarParameterUsedAsAtlasPosition(const FHashedMaterialParameterInfo& ParameterInfo, bool& OutValue, TSoftObjectPtr<class UCurveLinearColor>& Curve, TSoftObjectPtr<class UCurveLinearColorAtlas>&  Atlas) const;
#endif // WITH_EDITOR
	ENGINE_API virtual bool GetScalarCurveParameterValue(const FHashedMaterialParameterInfo& ParameterInfo, FInterpCurveFloat& OutValue) const;
	ENGINE_API virtual bool GetVectorParameterValue(const FHashedMaterialParameterInfo& ParameterInfo, FLinearColor& OutValue, bool bOveriddenOnly = false) const;
#if WITH_EDITOR
	ENGINE_API virtual bool IsVectorParameterUsedAsChannelMask(const FHashedMaterialParameterInfo& ParameterInfo, bool& OutValue) const;
	ENGINE_API virtual bool GetVectorParameterChannelNames(const FHashedMaterialParameterInfo& ParameterInfo, FParameterChannelNames& OutValue) const;
#endif
	ENGINE_API virtual bool GetVectorCurveParameterValue(const FHashedMaterialParameterInfo& ParameterInfo, FInterpCurveVector& OutValue) const;
	ENGINE_API virtual bool GetLinearColorParameterValue(const FHashedMaterialParameterInfo& ParameterInfo, FLinearColor& OutValue) const;
	ENGINE_API virtual bool GetLinearColorCurveParameterValue(const FHashedMaterialParameterInfo& ParameterInfo, FInterpCurveLinearColor& OutValue) const;
	ENGINE_API virtual bool GetTextureParameterValue(const FHashedMaterialParameterInfo& ParameterInfo, class UTexture*& OutValue, bool bOveriddenOnly = false) const;
	ENGINE_API virtual bool GetRuntimeVirtualTextureParameterValue(const FHashedMaterialParameterInfo& ParameterInfo, class URuntimeVirtualTexture*& OutValue, bool bOveriddenOnly = false) const;
#if WITH_EDITOR
	ENGINE_API virtual bool GetTextureParameterChannelNames(const FHashedMaterialParameterInfo& ParameterInfo, FParameterChannelNames& OutValue) const;
#endif
	ENGINE_API virtual bool GetFontParameterValue(const FHashedMaterialParameterInfo& ParameterInfo,class UFont*& OutFontValue, int32& OutFontPage, bool bOveriddenOnly = false) const;
	ENGINE_API virtual bool GetRefractionSettings(float& OutBiasValue) const;

	/**
		Access to overridable properties of the base material.
	*/
	ENGINE_API virtual float GetOpacityMaskClipValue() const;
	ENGINE_API virtual bool GetCastDynamicShadowAsMasked() const;
	ENGINE_API virtual EBlendMode GetBlendMode() const;
	ENGINE_API virtual FMaterialShadingModelField GetShadingModels() const;
	ENGINE_API virtual bool IsShadingModelFromMaterialExpression() const;
	ENGINE_API virtual bool IsTwoSided() const;
	ENGINE_API virtual bool IsDitheredLODTransition() const;
	ENGINE_API virtual bool IsTranslucencyWritingCustomDepth() const;
	ENGINE_API virtual bool IsTranslucencyWritingVelocity() const;
	ENGINE_API virtual bool IsMasked() const;
	ENGINE_API virtual bool IsDeferredDecal() const;

	ENGINE_API virtual USubsurfaceProfile* GetSubsurfaceProfile_Internal() const;
	ENGINE_API virtual bool CastsRayTracedShadows() const;

	//@StarLight code - BEGIN Add rain depth pass, edit by wanghai
	ENGINE_API virtual bool IsUsedWithRainOccluder() const;
	//@StarLight code - END Add rain depth pass, edit by wanghai

	/**
	 * Force the streaming system to disregard the normal logic for the specified duration and
	 * instead always load all mip-levels for all textures used by this material.
	 *
	 * @param OverrideForceMiplevelsToBeResident	- Whether to use (true) or ignore (false) the bForceMiplevelsToBeResidentValue parameter.
	 * @param bForceMiplevelsToBeResidentValue		- true forces all mips to stream in. false lets other factors decide what to do with the mips.
	 * @param ForceDuration							- Number of seconds to keep all mip-levels in memory, disregarding the normal priority logic. Negative value turns it off.
	 * @param CinematicTextureGroups				- Bitfield indicating texture groups that should use extra high-resolution mips
	 * @param bFastResponse							- USE WITH EXTREME CAUTION! Fast response textures incur sizable GT overhead and disturb streaming metric calculation. Avoid whenever possible.
	 */
	UFUNCTION(BlueprintCallable, Category = "Rendering|Material")
	ENGINE_API virtual void SetForceMipLevelsToBeResident( bool OverrideForceMiplevelsToBeResident, bool bForceMiplevelsToBeResidentValue, float ForceDuration, int32 CinematicTextureGroups = 0, bool bFastResponse = false );

	/**
	 * Re-caches uniform expressions for all material interfaces
	 * Set bRecreateUniformBuffer to true if uniform buffer layout will change (e.g. FMaterial is being recompiled).
	 * In that case calling needs to use FMaterialUpdateContext to recreate the rendering state of primitives using this material.
	 * 
	 * @param bRecreateUniformBuffer - true forces uniform buffer recreation.
	 */
	ENGINE_API static void RecacheAllMaterialUniformExpressions(bool bRecreateUniformBuffer);

	/**
	 * Re-caches uniform expressions for this material interface                   
	 * Set bRecreateUniformBuffer to true if uniform buffer layout will change (e.g. FMaterial is being recompiled).
	 * In that case calling needs to use FMaterialUpdateContext to recreate the rendering state of primitives using this material.
	 *
	 * @param bRecreateUniformBuffer - true forces uniform buffer recreation.
	 */
	virtual void RecacheUniformExpressions(bool bRecreateUniformBuffer) const {}

#if WITH_EDITOR
	/** Clears the shader cache and recompiles the shader for rendering. */
	ENGINE_API virtual void ForceRecompileForRendering() {}
#endif // WITH_EDITOR

	/**
	 * Asserts if any default material does not exist.
	 */
	ENGINE_API static void AssertDefaultMaterialsExist();

	/**
	 * Asserts if any default material has not been post-loaded.
	 */
	ENGINE_API static void AssertDefaultMaterialsPostLoaded();

	/**
	 * Initializes all default materials.
	 */
	ENGINE_API static void InitDefaultMaterials();

	/** Checks to see if an input property should be active, based on the state of the material */
	ENGINE_API virtual bool IsPropertyActive(EMaterialProperty InProperty) const;

#if WITH_EDITOR
	/** Compiles a material property. */
	ENGINE_API int32 CompileProperty(FMaterialCompiler* Compiler, EMaterialProperty Property, uint32 ForceCastFlags = 0);

	/** Allows material properties to be compiled with the option of being overridden by the material attributes input. */
	ENGINE_API virtual int32 CompilePropertyEx( class FMaterialCompiler* Compiler, const FGuid& AttributeID );

	/** True if this Material Interface should force a plane preview */
	ENGINE_API virtual bool ShouldForcePlanePreview()
	{
		return bShouldForcePlanePreview;
	}
	
	/** Set whether or not this Material Interface should force a plane preview */
	ENGINE_API void SetShouldForcePlanePreview(const bool bInShouldForcePlanePreview)
	{
		bShouldForcePlanePreview = bInShouldForcePlanePreview;
	};
#endif // WITH_EDITOR

	/** Get bitfield indicating which feature levels should be compiled by default */
	ENGINE_API static uint32 GetFeatureLevelsToCompileForAllMaterials() { return FeatureLevelsForAllMaterials | (1 << GMaxRHIFeatureLevel); }

	/** Return number of used texture coordinates and whether or not the Vertex data is used in the shader graph */
	ENGINE_API void AnalyzeMaterialProperty(EMaterialProperty InProperty, int32& OutNumTextureCoordinates, bool& bOutRequiresVertexData);

#if WITH_EDITOR
	/** Checks to see if the given property references the texture */
	ENGINE_API bool IsTextureReferencedByProperty(EMaterialProperty InProperty, const UTexture* InTexture);
#endif // WITH_EDITOR

	/** Iterate over all feature levels currently marked as active */
	template <typename FunctionType>
	static void IterateOverActiveFeatureLevels(FunctionType InHandler) 
	{  
		uint32 FeatureLevels = GetFeatureLevelsToCompileForAllMaterials();
		while (FeatureLevels != 0)
		{
			InHandler((ERHIFeatureLevel::Type)FBitSet::GetAndClearNextBit(FeatureLevels));
		}
	}

	/** Access the cached uenum type information for material sampler type */
	static UEnum* GetSamplerTypeEnum() 
	{ 
		check(SamplerTypeEnum); 
		return SamplerTypeEnum; 
	}

	/** Return whether this material refer to any streaming textures. */
	ENGINE_API bool UseAnyStreamingTexture() const;
	/** Returns whether there is any streaming data in the component. */
	FORCEINLINE bool HasTextureStreamingData() const { return TextureStreamingData.Num() != 0; }
	/** Accessor to the data. */
	FORCEINLINE const TArray<FMaterialTextureInfo>& GetTextureStreamingData() const { return TextureStreamingData; }
	FORCEINLINE TArray<FMaterialTextureInfo>& GetTextureStreamingData() { return TextureStreamingData; }
	/** Find entries within TextureStreamingData that match the given name. */
	ENGINE_API bool FindTextureStreamingDataIndexRange(FName TextureName, int32& LowerIndex, int32& HigherIndex) const;

	/** Set new texture streaming data. */
	ENGINE_API void SetTextureStreamingData(const TArray<FMaterialTextureInfo>& InTextureStreamingData);

	/**
	* Returns the density of a texture in (LocalSpace Unit / Texture). Used for texture streaming metrics.
	*
	* @param TextureName			The name of the texture to get the data for.
	* @param UVChannelData			The mesh UV density in (LocalSpace Unit / UV Unit).
	* @return						The density, or zero if no data is available for this texture.
	*/
	ENGINE_API virtual float GetTextureDensity(FName TextureName, const struct FMeshUVChannelInfo& UVChannelData) const;

	ENGINE_API virtual void PreSave(const class ITargetPlatform* TargetPlatform) override;

	/**
	* Sort the texture streaming data by names to accelerate search. Only sorts if required.
	*
	* @param bForceSort			If true, force the operation even though the data might be already sorted.
	* @param bFinalSort			If true, the means there won't be any other sort after. This allows to remove null entries (platform dependent).
	*/
	ENGINE_API void SortTextureStreamingData(bool bForceSort, bool bFinalSort);

protected:

	/** Returns a bitfield indicating which feature levels should be compiled for rendering. GMaxRHIFeatureLevel is always present */
	ENGINE_API uint32 GetFeatureLevelsToCompileForRendering() const;

	void UpdateMaterialRenderProxy(FMaterialRenderProxy& Proxy);

private:
	/**
	 * Post loads all default materials.
	 */
	static void PostLoadDefaultMaterials();

	/**
	* Cached type information for the sampler type enumeration. 
	*/
	static UEnum* SamplerTypeEnum;

#if WITH_EDITOR
	/**
	* Whether or not this material interface should force the preview to be a plane mesh.
	*/
	bool bShouldForcePlanePreview;
#endif
};

// Used to set up some compact FName paths for the FCompactFullName
ENGINE_API void SetCompactFullNameFromObject(struct FCompactFullName &Dest, UObject* InDepObject);

/** Helper function to serialize inline shader maps for the given material resources. */
extern void SerializeInlineShaderMaps(
	const TMap<const class ITargetPlatform*, TArray<FMaterialResource*>>* PlatformMaterialResourcesToSave,
	FArchive& Ar,
	TArray<FMaterialResource>& OutLoadedResources,
	uint32* OutOffsetToFirstResource = nullptr);
/** Helper function to process (register) serialized inline shader maps for the given material resources. */
extern void ProcessSerializedInlineShaderMaps(UMaterialInterface* Owner, TArray<FMaterialResource>& LoadedResources, FMaterialResource* (&OutMaterialResourcesLoaded)[EMaterialQualityLevel::Num][ERHIFeatureLevel::Num]);

