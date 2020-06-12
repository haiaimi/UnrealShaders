// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VertexFactory.h: Vertex factory definitions.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Containers/List.h"
#include "Misc/SecureHash.h"
#include "RHI.h"
#include "RenderResource.h"
#include "ShaderCore.h"
#include "Shader.h"
#include "Misc/EnumClassFlags.h"

class FMaterial;
class FMeshDrawSingleShaderBindings;

#if PLATFORM_SUPPORTS_PRAGMA_PACK
	#pragma pack (push,4)
#endif

struct FVertexInputStream
{
	uint32 StreamIndex : 4;
	uint32 Offset : 28;
	FRHIVertexBuffer* VertexBuffer;

	FVertexInputStream() :
		StreamIndex(0),
		Offset(0),
		VertexBuffer(nullptr)
	{}

	FVertexInputStream(uint32 InStreamIndex, uint32 InOffset, FRHIVertexBuffer* InVertexBuffer)
		: StreamIndex(InStreamIndex), Offset(InOffset), VertexBuffer(InVertexBuffer)
	{
		// Verify no overflow
		checkSlow(InStreamIndex == StreamIndex && InOffset == Offset);
	}

	inline bool operator==(const FVertexInputStream& rhs) const
	{
		if (StreamIndex != rhs.StreamIndex ||
			Offset != rhs.Offset || 
			VertexBuffer != rhs.VertexBuffer) 
		{
			return false;
		}

		return true;
	}

	inline bool operator!=(const FVertexInputStream& rhs) const
	{
		return !(*this == rhs);
	}
};

#if PLATFORM_SUPPORTS_PRAGMA_PACK
	#pragma pack (pop)
#endif

/** 
 * Number of vertex input bindings to allocate inline within a FMeshDrawCommand.
 * This is tweaked so that the bindings for FLocalVertexFactory fit into the inline storage.
 * Overflow of the inline storage will cause a heap allocation per draw (and corresponding cache miss on traversal)
 */
typedef TArray<FVertexInputStream, TInlineAllocator<4>> FVertexInputStreamArray;

enum class EVertexStreamUsage : uint8
{
	Default			= 0 << 0,
	Instancing		= 1 << 0,
	Overridden		= 1 << 1,
	ManualFetch		= 1 << 2
};


enum class EVertexInputStreamType : uint8
{
	Default = 0,
	PositionOnly,
	PositionAndNormalOnly
};

ENUM_CLASS_FLAGS(EVertexStreamUsage);
/**
 * A typed data source for a vertex factory which streams data from a vertex buffer.
 */
struct FVertexStreamComponent
{
	/** The vertex buffer to stream data from.  If null, no data can be read from this stream. */
	const FVertexBuffer* VertexBuffer = nullptr;

	/** The offset to the start of the vertex buffer fetch. */
	uint32 StreamOffset = 0;

	/** The offset of the data, relative to the beginning of each element in the vertex buffer. */
	uint8 Offset = 0;

	/** The stride of the data. */
	uint8 Stride = 0;

	/** The type of the data read from this stream. */
	TEnumAsByte<EVertexElementType> Type = VET_None;

	EVertexStreamUsage VertexStreamUsage = EVertexStreamUsage::Default;

	/**
	 * Initializes the data stream to null.
	 */
	FVertexStreamComponent()
	{}

	/**
	 * Minimal initialization constructor.
	 */
	FVertexStreamComponent(const FVertexBuffer* InVertexBuffer, uint32 InOffset, uint32 InStride, EVertexElementType InType, EVertexStreamUsage Usage = EVertexStreamUsage::Default) :
		VertexBuffer(InVertexBuffer),
		StreamOffset(0),
		Offset(InOffset),
		Stride(InStride),
		Type(InType),
		VertexStreamUsage(Usage)
	{
		check(InStride <= 0xFF);
		check(InOffset <= 0xFF);
	}

	FVertexStreamComponent(const FVertexBuffer* InVertexBuffer, uint32 InStreamOffset, uint32 InOffset, uint32 InStride, EVertexElementType InType, EVertexStreamUsage Usage = EVertexStreamUsage::Default) :
		VertexBuffer(InVertexBuffer),
		StreamOffset(InStreamOffset),
		Offset(InOffset),
		Stride(InStride),
		Type(InType),
		VertexStreamUsage(Usage)
	{
		check(InStride <= 0xFF);
		check(InOffset <= 0xFF);
	}
};

/**
 * A macro which initializes a FVertexStreamComponent to read a member from a struct.
 */
#define STRUCTMEMBER_VERTEXSTREAMCOMPONENT(VertexBuffer,VertexType,Member,MemberType) \
	FVertexStreamComponent(VertexBuffer,STRUCT_OFFSET(VertexType,Member),sizeof(VertexType),MemberType)

/**
 * An interface to the parameter bindings for the vertex factory used by a shader.
 */
class RENDERCORE_API FVertexFactoryShaderParameters
{
public:
	virtual ~FVertexFactoryShaderParameters() {}
	virtual void Bind(const class FShaderParameterMap& ParameterMap) = 0;
	virtual void Serialize(FArchive& Ar) = 0;

	/** 
	 * Gets the vertex factory's shader bindings and vertex streams.
	 * View can be null when caching mesh draw commands (only for supported vertex factories)
	 */
	virtual void GetElementShaderBindings(
		const class FSceneInterface* Scene,
		const class FSceneView* View,
		const class FMeshMaterialShader* Shader,
		const EVertexInputStreamType InputStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const class FVertexFactory* VertexFactory,
		const struct FMeshBatchElement& BatchElement,
		class FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams) const = 0;

	virtual uint32 GetSize() const { return sizeof(*this); }
};

/**
 * An object used to represent the type of a vertex factory.
 */
class FVertexFactoryType
{
public:

	typedef FVertexFactoryShaderParameters* (*ConstructParametersType)(EShaderFrequency ShaderFrequency);
	typedef bool (*ShouldCacheType)(EShaderPlatform, const class FMaterial*, const class FShaderType*);
	typedef void (*ModifyCompilationEnvironmentType)(const FVertexFactoryType*, EShaderPlatform, const class FMaterial*, FShaderCompilerEnvironment&);
	typedef void (*ValidateCompiledResultType)(const FVertexFactoryType*, EShaderPlatform, const FShaderParameterMap& ParameterMap, TArray<FString>& OutErrors);
	typedef bool (*SupportsTessellationShadersType)();

	/**
	 * @return The global shader factory list.
	 */
	static RENDERCORE_API TLinkedList<FVertexFactoryType*>*& GetTypeList();

	/**
	 * Finds a FVertexFactoryType by name.
	 */
	static RENDERCORE_API FVertexFactoryType* GetVFByName(const FString& VFName);

	/** Initialize FVertexFactoryType static members, this must be called before any VF types are created. */
	static void Initialize(const TMap<FString, TArray<const TCHAR*> >& ShaderFileToUniformBufferVariables);

	/** Uninitializes FVertexFactoryType cached data. */
	static void Uninitialize();

	/**
	 * Minimal initialization constructor.
	 * @param bInUsedWithMaterials - True if the vertex factory will be rendered in combination with a material.
	 * @param bInSupportsStaticLighting - True if the vertex factory will be rendered with static lighting.
	 */
	RENDERCORE_API FVertexFactoryType(
		const TCHAR* InName,
		const TCHAR* InShaderFilename,
		bool bInUsedWithMaterials,
		bool bInSupportsStaticLighting,
		bool bInSupportsDynamicLighting,
		bool bInSupportsPrecisePrevWorldPos,
		bool bInSupportsPositionOnly,
		bool bInSupportsCachingMeshDrawCommands,
		bool bInSupportsPrimitiveIdStream,
		ConstructParametersType InConstructParameters,
		ShouldCacheType InShouldCache,
		ModifyCompilationEnvironmentType InModifyCompilationEnvironment,
		ValidateCompiledResultType InValidateCompiledResult,
		SupportsTessellationShadersType InSupportsTessellationShaders
		);

	RENDERCORE_API virtual ~FVertexFactoryType();

	// Accessors.
	const TCHAR* GetName() const { return Name; }
	FName GetFName() const { return TypeName; }
	const TCHAR* GetShaderFilename() const { return ShaderFilename; }
	FVertexFactoryShaderParameters* CreateShaderParameters(EShaderFrequency ShaderFrequency) const { return (*ConstructParameters)(ShaderFrequency); }
	bool IsUsedWithMaterials() const { return bUsedWithMaterials; }
	bool SupportsStaticLighting() const { return bSupportsStaticLighting; }
	bool SupportsDynamicLighting() const { return bSupportsDynamicLighting; }
	bool SupportsPrecisePrevWorldPos() const { return bSupportsPrecisePrevWorldPos; }
	bool SupportsPositionOnly() const { return bSupportsPositionOnly; }
	bool SupportsCachingMeshDrawCommands() const { return bSupportsCachingMeshDrawCommands; }
	bool SupportsPrimitiveIdStream() const { return bSupportsPrimitiveIdStream; }
	/** Returns an int32 specific to this vertex factory type. */
	inline int32 GetId() const { return HashIndex; }
	static int32 GetNumVertexFactoryTypes() { return NextHashIndex; }

	const FSerializationHistory* GetSerializationHistory(EShaderFrequency Frequency) const
	{
		return &SerializationHistory[Frequency];
	}

	// Hash function.
	friend uint32 GetTypeHash(FVertexFactoryType* Type)
	{ 
		return Type ? Type->HashIndex : 0;
	}

	/** Calculates a Hash based on this vertex factory type's source code and includes */
	RENDERCORE_API const FSHAHash& GetSourceHash(EShaderPlatform ShaderPlatform) const;

	/**
	 * Should we cache the material's shadertype on this platform with this vertex factory? 
	 */
	bool ShouldCache(EShaderPlatform Platform, const class FMaterial* Material, const class FShaderType* ShaderType)
	{
		return (*ShouldCacheRef)(Platform, Material, ShaderType); 
	}

	/**
	* Calls the function ptr for the shader type on the given environment
	* @param Environment - shader compile environment to modify
	*/
	void ModifyCompilationEnvironment(EShaderPlatform Platform, const FMaterial* Material, FShaderCompilerEnvironment& OutEnvironment){
		// Set up the mapping from VertexFactory.usf to the vertex factory type's source code.
		FString VertexFactoryIncludeString = FString::Printf( TEXT("#include \"%s\""), GetShaderFilename() );
		OutEnvironment.IncludeVirtualPathToContentsMap.Add(TEXT("/Engine/Generated/VertexFactory.ush"), VertexFactoryIncludeString);

		OutEnvironment.SetDefine(TEXT("HAS_PRIMITIVE_UNIFORM_BUFFER"), 1);

		(*ModifyCompilationEnvironmentRef)(this, Platform, Material, OutEnvironment);
	}

	void ValidateCompiledResult(EShaderPlatform Platform, const FShaderParameterMap& ParameterMap, TArray<FString>& OutErrors)
	{
		(*ValidateCompiledResultRef)(this, Platform, ParameterMap, OutErrors);
	}

	/**
	 * Does this vertex factory support tessellation shaders?
	 */
	bool SupportsTessellationShaders() const
	{
		return (*SupportsTessellationShadersRef)(); 
	}

	/** Adds include statements for uniform buffers that this shader type references, and builds a prefix for the shader file with the include statements. */
	RENDERCORE_API void AddReferencedUniformBufferIncludes(FShaderCompilerEnvironment& OutEnvironment, FString& OutSourceFilePrefix, EShaderPlatform Platform);

	void FlushShaderFileCache(const TMap<FString, TArray<const TCHAR*> >& ShaderFileToUniformBufferVariables)
	{
		ReferencedUniformBufferStructsCache.Empty();
		GenerateReferencedUniformBuffers(ShaderFilename, Name, ShaderFileToUniformBufferVariables, ReferencedUniformBufferStructsCache);
		bCachedUniformBufferStructDeclarations = false;
	}

	const TMap<const TCHAR*, FCachedUniformBufferDeclaration>& GetReferencedUniformBufferStructsCache() const
	{
		return ReferencedUniformBufferStructsCache;
	}

private:
	static RENDERCORE_API uint32 NextHashIndex;

	/** Tracks whether serialization history for all shader types has been initialized. */
	static bool bInitializedSerializationHistory;

	uint32 HashIndex;
	const TCHAR* Name;
	const TCHAR* ShaderFilename;
	FName TypeName;
	uint32 bUsedWithMaterials : 1;
	uint32 bSupportsStaticLighting : 1;
	uint32 bSupportsDynamicLighting : 1;
	uint32 bSupportsPrecisePrevWorldPos : 1;
	uint32 bSupportsPositionOnly : 1;
	uint32 bSupportsCachingMeshDrawCommands : 1;
	uint32 bSupportsPrimitiveIdStream : 1;
	ConstructParametersType ConstructParameters;
	ShouldCacheType ShouldCacheRef;
	ModifyCompilationEnvironmentType ModifyCompilationEnvironmentRef;
	ValidateCompiledResultType ValidateCompiledResultRef;
	SupportsTessellationShadersType SupportsTessellationShadersRef;

	TLinkedList<FVertexFactoryType*> GlobalListLink;

	/** 
	 * Cache of referenced uniform buffer includes.  
	 * These are derived from source files so they need to be flushed when editing and recompiling shaders on the fly. 
	 * FVertexFactoryType::Initialize will add an entry for each referenced uniform buffer, but the declarations are added on demand as shaders are compiled.
	 */
	TMap<const TCHAR*, FCachedUniformBufferDeclaration> ReferencedUniformBufferStructsCache;

	/** Tracks what platforms ReferencedUniformBufferStructsCache has had declarations cached for. */
	bool bCachedUniformBufferStructDeclarations;

	/** 
	 * Stores a history of serialization sizes for this vertex factory's shader parameter class. 
	 * This is used to invalidate shaders when serialization changes.
	 */
	FSerializationHistory SerializationHistory[SF_NumFrequencies];
};

/**
 * Serializes a reference to a vertex factory type.
 */
extern RENDERCORE_API FArchive& operator<<(FArchive& Ar,FVertexFactoryType*& TypeRef);

/**
 * Find the vertex factory type with the given name.
 * @return NULL if no vertex factory type matched, otherwise a vertex factory type with a matching name.
 */
extern RENDERCORE_API FVertexFactoryType* FindVertexFactoryType(FName TypeName);

/**
 * A macro for declaring a new vertex factory type, for use in the vertex factory class's definition body.
 */
#define DECLARE_VERTEX_FACTORY_TYPE(FactoryClass) \
	public: \
	static FVertexFactoryType StaticType; \
	virtual FVertexFactoryType* GetType() const override;

/**
 * A macro for implementing the static vertex factory type object, and specifying parameters used by the type.
 * @param bUsedWithMaterials - True if the vertex factory will be rendered in combination with a material.
 * @param bSupportsStaticLighting - True if the vertex factory will be rendered with static lighting.
 */
#define IMPLEMENT_VERTEX_FACTORY_TYPE(FactoryClass,ShaderFilename,bUsedWithMaterials,bSupportsStaticLighting,bSupportsDynamicLighting,bPrecisePrevWorldPos,bSupportsPositionOnly) \
	FVertexFactoryShaderParameters* Construct##FactoryClass##ShaderParameters(EShaderFrequency ShaderFrequency) { return FactoryClass::ConstructShaderParameters(ShaderFrequency); } \
	FVertexFactoryType FactoryClass::StaticType( \
		TEXT(#FactoryClass), \
		TEXT(ShaderFilename), \
		bUsedWithMaterials, \
		bSupportsStaticLighting, \
		bSupportsDynamicLighting, \
		bPrecisePrevWorldPos, \
		bSupportsPositionOnly, \
		false, \
		false, \
		Construct##FactoryClass##ShaderParameters, \
		FactoryClass::ShouldCompilePermutation, \
		FactoryClass::ModifyCompilationEnvironment, \
		FactoryClass::ValidateCompiledResult, \
		FactoryClass::SupportsTessellationShaders \
		); \
		FVertexFactoryType* FactoryClass::GetType() const { return &StaticType; }

// @todo - need more extensible type properties - shouldn't have to change all IMPLEMENT_VERTEX_FACTORY_TYPE's when you add one new parameter
#define IMPLEMENT_VERTEX_FACTORY_TYPE_EX(FactoryClass,ShaderFilename,bUsedWithMaterials,bSupportsStaticLighting,bSupportsDynamicLighting,bPrecisePrevWorldPos,bSupportsPositionOnly,bSupportsCachingMeshDrawCommands,bSupportsPrimitiveIdStream) \
	FVertexFactoryShaderParameters* Construct##FactoryClass##ShaderParameters(EShaderFrequency ShaderFrequency) { return FactoryClass::ConstructShaderParameters(ShaderFrequency); } \
	FVertexFactoryType FactoryClass::StaticType( \
		TEXT(#FactoryClass), \
		TEXT(ShaderFilename), \
		bUsedWithMaterials, \
		bSupportsStaticLighting, \
		bSupportsDynamicLighting, \
		bPrecisePrevWorldPos, \
		bSupportsPositionOnly, \
		bSupportsCachingMeshDrawCommands, \
		bSupportsPrimitiveIdStream, \
		Construct##FactoryClass##ShaderParameters, \
		FactoryClass::ShouldCompilePermutation, \
		FactoryClass::ModifyCompilationEnvironment, \
		FactoryClass::ValidateCompiledResult, \
		FactoryClass::SupportsTessellationShaders \
		); \
		FVertexFactoryType* FactoryClass::GetType() const { return &StaticType; }

//#Change by wh, 2020/6/12 
// @todo - need more extensible type properties - shouldn't have to change all IMPLEMENT_VERTEX_FACTORY_TYPE's when you add one new parameter
#define CANCAT(LH, RH) (LH##RH)
#define IMPLEMENT_VERTEX_FACTORY_TYPE_EX_TEMPLATE(FactoryClass, TemplateType, ShaderFilename,bUsedWithMaterials,bSupportsStaticLighting,bSupportsDynamicLighting,bPrecisePrevWorldPos,bSupportsPositionOnly,bSupportsCachingMeshDrawCommands,bSupportsPrimitiveIdStream) \
	FVertexFactoryShaderParameters* Construct##FactoryClass##ShaderParameters##TemplateType(EShaderFrequency ShaderFrequency) { return FactoryClass<TemplateType>::ConstructShaderParameters(ShaderFrequency); } \
	FVertexFactoryType FactoryClass<TemplateType>::StaticType( \
		*(FString(#FactoryClass) + FString(#TemplateType)), \
		TEXT(ShaderFilename), \
		bUsedWithMaterials, \
		bSupportsStaticLighting, \
		bSupportsDynamicLighting, \
		bPrecisePrevWorldPos, \
		bSupportsPositionOnly, \
		bSupportsCachingMeshDrawCommands, \
		bSupportsPrimitiveIdStream, \
		Construct##FactoryClass##ShaderParameters##TemplateType, \
		FactoryClass<TemplateType>::ShouldCompilePermutation, \
		FactoryClass<TemplateType>::ModifyCompilationEnvironment, \
		FactoryClass<TemplateType>::ValidateCompiledResult, \
		FactoryClass<TemplateType>::SupportsTessellationShaders \
		); \
		FVertexFactoryType* FactoryClass<TemplateType>::GetType() const { return &StaticType; }
//end

/** Encapsulates a dependency on a vertex factory type and saved state from that vertex factory type. */
class FVertexFactoryTypeDependency
{
public:

	FVertexFactoryTypeDependency() :
		VertexFactoryType(NULL)
	{}

	FVertexFactoryType* VertexFactoryType;

#if KEEP_SHADER_SOURCE_HASHES
	/** Used to detect changes to the vertex factory source files. */
	FSHAHash VFSourceHash;
#endif

	friend FArchive& operator<<(FArchive& Ar,class FVertexFactoryTypeDependency& Ref)
	{
#if KEEP_SHADER_SOURCE_HASHES
		FSHAHash& Hash = Ref.VFSourceHash;
#else
		FSHAHash Hash;
#endif
		Ar << Ref.VertexFactoryType << FShaderResource::FilterShaderSourceHashForSerialization(Ar, Hash);
		return Ar;
	}

	bool operator==(const FVertexFactoryTypeDependency& Reference) const
	{
#if KEEP_SHADER_SOURCE_HASHES
		return VertexFactoryType == Reference.VertexFactoryType && VFSourceHash == Reference.VFSourceHash;
#else
		return VertexFactoryType == Reference.VertexFactoryType;
#endif
	}

	bool operator!=(const FVertexFactoryTypeDependency& Reference) const
	{
		return !(*this == Reference);
	}
};

/** Used to compare two Vertex Factory types by name. */
class FCompareVertexFactoryTypes											
{																				
public:		
	FORCEINLINE bool operator()(const FVertexFactoryType& A, const FVertexFactoryType& B ) const
	{
		int32 AL = FCString::Strlen(A.GetName());
		int32 BL = FCString::Strlen(B.GetName());
		if ( AL == BL )
		{
			return FCString::Strncmp(A.GetName(), B.GetName(), AL) > 0;
		}
		return AL > BL;
	}
};

/**
 * Encapsulates a vertex data source which can be linked into a vertex shader.
 */
class RENDERCORE_API FVertexFactory : public FRenderResource
{
public:
	FVertexFactory(ERHIFeatureLevel::Type InFeatureLevel) 
		: FRenderResource(InFeatureLevel)
	{
	}

	virtual FVertexFactoryType* GetType() const { return NULL; }

	void GetStreams(ERHIFeatureLevel::Type InFeatureLevel, EVertexInputStreamType VertexStreamType, FVertexInputStreamArray& OutVertexStreams) const;

	void OffsetInstanceStreams(uint32 InstanceOffset, EVertexInputStreamType VertexStreamType, FVertexInputStreamArray& VertexStreams) const;

	/**
	* Can be overridden by FVertexFactory subclasses to modify their compile environment just before compilation occurs.
	*/
	static void ModifyCompilationEnvironment(const FVertexFactoryType* Type, EShaderPlatform Platform, const FMaterial* Material, FShaderCompilerEnvironment& OutEnvironment ) {}

	/**
	* Can be overridden by FVertexFactory subclasses to fail a compile based on compilation output.
	*/
	static void ValidateCompiledResult(const FVertexFactoryType* Type, EShaderPlatform Platform, const FShaderParameterMap& ParameterMap, TArray<FString>& OutErrors) {}

	/**
	* Can be overridden by FVertexFactory subclasses to enable HS/DS in D3D11
	*/
	static bool SupportsTessellationShaders() { return false; }

	// FRenderResource interface.
	virtual void ReleaseRHI();

	// Accessors.
	FVertexDeclarationRHIRef& GetDeclaration() { return Declaration; }
	void SetDeclaration(FVertexDeclarationRHIRef& NewDeclaration) { Declaration = NewDeclaration; }

	const FVertexDeclarationRHIRef& GetDeclaration(EVertexInputStreamType InputStreamType/* = FVertexInputStreamType::Default*/) const 
	{
		switch (InputStreamType)
		{
		case EVertexInputStreamType::Default:				return Declaration;
		case EVertexInputStreamType::PositionOnly:			return PositionDeclaration;
		case EVertexInputStreamType::PositionAndNormalOnly:	return PositionAndNormalDeclaration;
		}
		return Declaration;
	}

	virtual bool IsGPUSkinned() const { return false; }

	/** Indicates whether the vertex factory supports a position-only stream. */
	virtual bool SupportsPositionOnlyStream() const { return !!PositionStream.Num(); }

	/** Indicates whether the vertex factory supports a position-and-normal-only stream. */
	virtual bool SupportsPositionAndNormalOnlyStream() const { return !!PositionAndNormalStream.Num(); }

	/** Indicates whether the vertex factory supports a null pixel shader. */
	virtual bool SupportsNullPixelShader() const { return true; }

	virtual bool RendersPrimitivesAsCameraFacingSprites() const { return false; }

	/**
	 * Get a bitmask representing the visibility of each FMeshBatch element.
	 * FMeshBatch.bRequiresPerElementVisibility must be set for this to be called.
	 */
	virtual uint64 GetStaticBatchElementVisibility(const class FSceneView& View, const struct FMeshBatch* Batch, const void* InViewCustomData = nullptr) const { return 1; }

	bool NeedsDeclaration() const { return bNeedsDeclaration; }
	bool SupportsManualVertexFetch(ERHIFeatureLevel::Type InFeatureLevel) const 
	{ 
		check(InFeatureLevel != ERHIFeatureLevel::Num);
		return bSupportsManualVertexFetch && (InFeatureLevel > ERHIFeatureLevel::ES3_1) && RHISupportsManualVertexFetch(GMaxRHIShaderPlatform);
	}

	inline int32 GetPrimitiveIdStreamIndex(EVertexInputStreamType InputStreamType) const
	{
		return PrimitiveIdStreamIndex[static_cast<uint8>(InputStreamType)];
	}


protected:

	inline void SetPrimitiveIdStreamIndex(EVertexInputStreamType InputStreamType, int32 StreamIndex)
	{
		PrimitiveIdStreamIndex[static_cast<uint8>(InputStreamType)] = StreamIndex;
	}

	/**
	 * Creates a vertex element for a vertex stream components.  Adds a unique stream index for the vertex buffer used by the component.
	 * @param Component - The vertex stream component.
	 * @param AttributeIndex - The attribute index to which the stream component is bound.
	 * @return The vertex element which corresponds to Component.
	 */
	FVertexElement AccessStreamComponent(const FVertexStreamComponent& Component,uint8 AttributeIndex);

	/**
	 * Creates a vertex element for a vertex stream component.  Adds a unique position stream index for the vertex buffer used by the component.
	 * @param Component - The vertex stream component.
	 * @param Usage - The vertex element usage semantics.
	 * @param AttributeIndex - The attribute index to which the stream component is bound.
	 * @return The vertex element which corresponds to Component.
	 */
	FVertexElement AccessStreamComponent(const FVertexStreamComponent& Component, uint8 AttributeIndex, EVertexInputStreamType InputStreamType);

	/**
	 * Initializes the vertex declaration.
	 * @param Elements - The elements of the vertex declaration.
	 */
	void InitDeclaration(const FVertexDeclarationElementList& Elements, EVertexInputStreamType StreamType = EVertexInputStreamType::Default);

	/**
	 * Information needed to set a vertex stream.
	 */
	struct FVertexStream
	{
		const FVertexBuffer* VertexBuffer = nullptr;
		uint32 Offset = 0;
		uint16 Stride = 0;
		EVertexStreamUsage VertexStreamUsage = EVertexStreamUsage::Default;
		uint8 Padding = 0;

		friend bool operator==(const FVertexStream& A,const FVertexStream& B)
		{
			return A.VertexBuffer == B.VertexBuffer && A.Stride == B.Stride && A.Offset == B.Offset && A.VertexStreamUsage == B.VertexStreamUsage;
		}

		FVertexStream()
		{
		}
	};

	/** The vertex streams used to render the factory. */
	TArray<FVertexStream,TInlineAllocator<8> > Streams;

	/* VF can explicitly set this to false to avoid errors without decls; this is for VFs that fetch from buffers directly (e.g. Niagara) */
	bool bNeedsDeclaration = true;
	
	bool bSupportsManualVertexFetch = false;

	int8 PrimitiveIdStreamIndex[3] = { -1, -1, -1 }; // Need to match entry count of EVertexInputStreamType

private:

	/** The position only vertex stream used to render the factory during depth only passes. */
	TArray<FVertexStream,TInlineAllocator<2> > PositionStream;
	TArray<FVertexStream, TInlineAllocator<3> > PositionAndNormalStream;

	/** The RHI vertex declaration used to render the factory normally. */
	FVertexDeclarationRHIRef Declaration;

	/** The RHI vertex declaration used to render the factory during depth only passes. */
	FVertexDeclarationRHIRef PositionDeclaration;
	FVertexDeclarationRHIRef PositionAndNormalDeclaration;
};

/**
 * An encapsulation of the vertex factory parameters for a shader.
 * Handles serialization and binding of the vertex factory parameters for the shader's vertex factory type.
 */
class RENDERCORE_API FVertexFactoryParameterRef
{
public:
	FVertexFactoryParameterRef(FVertexFactoryType* InVertexFactoryType,const FShaderParameterMap& ParameterMap, EShaderFrequency InShaderFrequency, EShaderPlatform InShaderPlatform);

	FVertexFactoryParameterRef():
		Parameters(NULL),
		VertexFactoryType(NULL)
	{}

	~FVertexFactoryParameterRef()
	{
		delete Parameters;
	}

	void GetElementShaderBindings(
		const class FSceneInterface* Scene,
		const FSceneView* View,
		const class FMeshMaterialShader* Shader,
		const EVertexInputStreamType InputStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const FVertexFactory* VertexFactory,
		const struct FMeshBatchElement& BatchElement,
		class FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams
		) const
	{
		if (Parameters)
		{
			checkSlow(VertexFactory->GetType() == VertexFactoryType);
			checkSlow(View || VertexFactoryType->SupportsCachingMeshDrawCommands());
			Parameters->GetElementShaderBindings(Scene, View, Shader, InputStreamType, FeatureLevel, VertexFactory, BatchElement, ShaderBindings, VertexStreams);
		}
	}

	const FVertexFactoryType* GetVertexFactoryType() const { return VertexFactoryType; }

#if KEEP_SHADER_SOURCE_HASHES
	/** Returns the hash of the vertex factory shader file that this shader was compiled with. */
	const FSHAHash& GetHash() const;
#endif

	/** Returns the shader platform that this shader was compiled with. */
	EShaderPlatform GetShaderPlatform() const;

	friend RENDERCORE_API bool operator<<(FArchive& Ar,FVertexFactoryParameterRef& Ref);

	uint32 GetAllocatedSize() const
	{
		return Parameters ? Parameters->GetSize() : 0;
	}

private:
	FVertexFactoryShaderParameters* Parameters;
	FVertexFactoryType* VertexFactoryType;
	EShaderFrequency ShaderFrequency;
	EShaderPlatform ShaderPlatform;

#if KEEP_SHADER_SOURCE_HASHES
	// Hash of the vertex factory's source file at shader compile time, used by the automatic versioning system to detect changes
	FSHAHash VFHash;
#endif
};

/**
* Default PrimitiveId vertex buffer.  Contains a single index of 0.
* This is used when the VF is used for rendering outside normal mesh passes, where there is no valid scene.
*/
class FPrimitiveIdDummyBuffer : public FVertexBuffer
{
public:

	virtual void InitRHI() override;

	virtual void ReleaseRHI() override
	{
		VertexBufferSRV.SafeRelease();
		FVertexBuffer::ReleaseRHI();
	}

	FShaderResourceViewRHIRef VertexBufferSRV;
};

extern RENDERCORE_API TGlobalResource<FPrimitiveIdDummyBuffer> GPrimitiveIdDummy;