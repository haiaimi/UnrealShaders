// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MaterialCompiler.h: Material compiler interface.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"
#include "MaterialShared.h"
#include "Materials/MaterialExpressionSpeedTree.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionTextureProperty.h"
#include "Materials/MaterialExpressionViewProperty.h"

class Error;
class UMaterialParameterCollection;
class URuntimeVirtualTexture;
class UTexture;
struct FMaterialParameterInfo;

enum EMaterialForceCastFlags
{
	MFCF_ForceCast		= 1<<0,	// Used by caller functions as a helper
	MFCF_ExactMatch		= 1<<2, // If flag set skips the cast on an exact match, else skips on a compatible match
	MFCF_ReplicateValue	= 1<<3	// Replicates a Float1 value when up-casting, else appends zero
};

enum class EVirtualTextureUnpackType
{
	None,
	BaseColorYCoCg,
	NormalBC3,
	NormalBC5,
	NormalBC3BC3,
	NormalBC5BC1,
	HeightR16,
};

/** What type of compiler is this? Used by material expressions that select input based on compile context */
enum class EMaterialCompilerType
{
	Standard, /** Standard HLSL translator */
	Lightmass, /** Lightmass proxy compiler */
	MaterialProxy, /** Flat material proxy compiler */
};

/** 
 * The interface used to translate material expressions into executable code. 
 * Note: Most member functions should be pure virtual to force a FProxyMaterialCompiler override!
 */
class FMaterialCompiler
{
public:
	virtual ~FMaterialCompiler() { }

	// sets internal state CurrentShaderFrequency 
	// @param OverrideShaderFrequency SF_NumFrequencies to not override
	virtual void SetMaterialProperty(EMaterialProperty InProperty, EShaderFrequency OverrideShaderFrequency = SF_NumFrequencies, bool bUsePreviousFrameTime = false) = 0;
	
	/** Pushes a material attributes property onto the stack. Called as we begin compiling a property through a MaterialAttributes pin. */
	virtual void PushMaterialAttribute(const FGuid& InAttributeID) = 0;
	/** Pops a MaterialAttributes property off the stack. Called as we finish compiling a property through a MaterialAttributes pin. */
	virtual FGuid PopMaterialAttribute() = 0;
	/** Gets the current top of the MaterialAttributes property stack. */
	virtual const FGuid GetMaterialAttribute() = 0;
	/** Sets the bottom MaterialAttributes property of the stack. */
	virtual void SetBaseMaterialAttribute(const FGuid& InAttributeID) = 0;

	/** Pushes a parameter owner onto the stack. Called as we begin compiling each layer function of MaterialAttributeLayers. */
	virtual void PushParameterOwner(const FMaterialParameterInfo& InOwnerInfo) = 0;
	/** Pops a parameter owner off the stack. Called as we finish compiling each layer function of MaterialAttributeLayers. */
	virtual FMaterialParameterInfo PopParameterOwner() = 0;

	// gets value stored by SetMaterialProperty()
	virtual EShaderFrequency GetCurrentShaderFrequency() const = 0;
	//
	virtual int32 Error(const TCHAR* Text) = 0;
	ENGINE_API int32 Errorf(const TCHAR* Format,...);
	virtual void AppendExpressionError(UMaterialExpression* Expression, const TCHAR* Text) = 0;

	virtual int32 CallExpression(FMaterialExpressionKey ExpressionKey,FMaterialCompiler* InCompiler) = 0;

	virtual EMaterialCompilerType GetCompilerType() const { return EMaterialCompilerType::Standard; }
	inline bool IsMaterialProxyCompiler() const { return GetCompilerType() == EMaterialCompilerType::MaterialProxy; }
	inline bool IsLightmassCompiler() const { return GetCompilerType() == EMaterialCompilerType::Lightmass; }

	inline bool IsVertexInterpolatorBypass() const
	{
		const EMaterialCompilerType Type = GetCompilerType();
		return Type == EMaterialCompilerType::MaterialProxy || Type == EMaterialCompilerType::Lightmass;
	}

	virtual EMaterialValueType GetType(int32 Code) = 0;

	virtual EMaterialQualityLevel::Type GetQualityLevel() = 0;

	virtual ERHIFeatureLevel::Type GetFeatureLevel() = 0;

	virtual EShaderPlatform GetShaderPlatform() = 0;

	virtual const ITargetPlatform* GetTargetPlatform() const = 0;

	virtual FMaterialShadingModelField GetMaterialShadingModels() const = 0;

	virtual EMaterialValueType GetParameterType(int32 Index) const = 0;

	virtual FMaterialUniformExpression* GetParameterUniformExpression(int32 Index) const = 0;

	virtual bool GetTextureForExpression(int32 Index, int32& OutTextureIndex, EMaterialSamplerType& OutSamplerType, TOptional<FName>& OutParameterName) const = 0;

	/** 
	 * Casts the passed in code to DestType, or generates a compile error if the cast is not valid. 
	 * This will truncate a type (float4 -> float3) but not add components (float2 -> float3), however a float1 can be cast to any float type by replication. 
	 */
	virtual int32 ValidCast(int32 Code,EMaterialValueType DestType) = 0;
	virtual int32 ForceCast(int32 Code,EMaterialValueType DestType,uint32 ForceCastFlags = 0) = 0;

	/** Pushes a function onto the compiler's function stack, which indicates that compilation is entering a function. */
	virtual void PushFunction(FMaterialFunctionCompileState* FunctionState) = 0;

	/** Pops a function from the compiler's function stack, which indicates that compilation is leaving a function. */
	virtual FMaterialFunctionCompileState* PopFunction() = 0;

	virtual int32 GetCurrentFunctionStackDepth() = 0;

	virtual int32 AccessCollectionParameter(UMaterialParameterCollection* ParameterCollection, int32 ParameterIndex, int32 ComponentIndex) = 0;	
	virtual int32 ScalarParameter(FName ParameterName, float DefaultValue) = 0;
	virtual int32 VectorParameter(FName ParameterName, const FLinearColor& DefaultValue) = 0;

	virtual int32 Constant(float X) = 0;
	virtual int32 Constant2(float X,float Y) = 0;
	virtual int32 Constant3(float X,float Y,float Z) = 0;
	virtual int32 Constant4(float X,float Y,float Z,float W) = 0;

	virtual	int32 ViewProperty(EMaterialExposedViewProperty Property, bool InvProperty = false) = 0;

	virtual int32 GameTime(bool bPeriodic, float Period) = 0;
	virtual int32 RealTime(bool bPeriodic, float Period) = 0;
	virtual int32 DeltaTime() = 0;
	virtual int32 PeriodicHint(int32 PeriodicCode) { return PeriodicCode; }

	virtual int32 Sine(int32 X) = 0;
	virtual int32 Cosine(int32 X) = 0;
	virtual int32 Tangent(int32 X) = 0;
	virtual int32 Arcsine(int32 X) = 0;
	virtual int32 ArcsineFast(int32 X) = 0;
	virtual int32 Arccosine(int32 X) = 0;
	virtual int32 ArccosineFast(int32 X) = 0;
	virtual int32 Arctangent(int32 X) = 0;
	virtual int32 ArctangentFast(int32 X) = 0;
	virtual int32 Arctangent2(int32 Y, int32 X) = 0;
	virtual int32 Arctangent2Fast(int32 Y, int32 X) = 0;

	virtual int32 Floor(int32 X) = 0;
	virtual int32 Ceil(int32 X) = 0;
	virtual int32 Round(int32 X) = 0;
	virtual int32 Truncate(int32 X) = 0;
	virtual int32 Sign(int32 X) = 0;
	virtual int32 Frac(int32 X) = 0;
	virtual int32 Fmod(int32 A, int32 B) = 0;
	virtual int32 Abs(int32 X) = 0;

	virtual int32 ReflectionVector() = 0;
	virtual int32 ReflectionAboutCustomWorldNormal(int32 CustomWorldNormal, int32 bNormalizeCustomWorldNormal) = 0;
	virtual int32 CameraVector() = 0;
	virtual int32 LightVector() = 0;

	virtual int32 GetViewportUV() = 0;
	virtual int32 GetPixelPosition() = 0;
	virtual int32 WorldPosition(EWorldPositionIncludedOffsets WorldPositionIncludedOffsets) = 0;
	virtual int32 ObjectWorldPosition() = 0;
	virtual int32 ObjectRadius() = 0;
	virtual int32 ObjectBounds() = 0;
	virtual int32 PreSkinnedLocalBounds(int32 OutputIndex) = 0;
	virtual int32 DistanceCullFade() = 0;
	virtual int32 ActorWorldPosition() = 0;
	virtual int32 ParticleMacroUV() = 0;
	virtual int32 ParticleSubUV(int32 TextureIndex, EMaterialSamplerType SamplerType, bool bBlend) = 0;
	virtual int32 ParticleSubUVProperty(int32 PropertyIndex) = 0;
	virtual int32 ParticleColor() = 0;
	virtual int32 ParticlePosition() = 0;
	virtual int32 ParticleRadius() = 0;
	virtual int32 SphericalParticleOpacity(int32 Density) = 0;
	virtual int32 ParticleRelativeTime() = 0;
	virtual int32 ParticleMotionBlurFade() = 0;
	virtual int32 ParticleRandom() = 0;
	virtual int32 ParticleDirection() = 0;
	virtual int32 ParticleSpeed() = 0;
	virtual int32 ParticleSize() = 0;

	virtual int32 If(int32 A,int32 B,int32 AGreaterThanB,int32 AEqualsB,int32 ALessThanB,int32 Threshold) = 0;

	virtual int32 TextureCoordinate(uint32 CoordinateIndex, bool UnMirrorU, bool UnMirrorV) = 0;
	virtual int32 TextureSample(int32 Texture,int32 Coordinate,enum EMaterialSamplerType SamplerType,int32 MipValue0Index=INDEX_NONE,int32 MipValue1Index=INDEX_NONE,ETextureMipValueMode MipValueMode=TMVM_None,ESamplerSourceMode SamplerSource=SSM_FromTextureAsset,int32 TextureReferenceIndex=INDEX_NONE, bool AutomaticViewMipBias=false) = 0;
	virtual int32 TextureProperty(int32 InTexture, EMaterialExposedTextureProperty Property) = 0;

	virtual int32 TextureDecalMipmapLevel(int32 TextureSizeInput) = 0;
	virtual int32 TextureDecalDerivative(bool bDDY) = 0;
	virtual int32 DecalLifetimeOpacity() = 0;

	virtual int32 Texture(UTexture* Texture,int32& TextureReferenceIndex, EMaterialSamplerType SamplerType, ESamplerSourceMode SamplerSource=SSM_FromTextureAsset,ETextureMipValueMode MipValueMode=TMVM_None) = 0;
	virtual int32 TextureParameter(FName ParameterName,UTexture* DefaultTexture,int32& TextureReferenceIndex, EMaterialSamplerType SamplerType, ESamplerSourceMode SamplerSource=SSM_FromTextureAsset) = 0;

	virtual int32 VirtualTexture(URuntimeVirtualTexture* InTexture, int32 TextureLayerIndex, int32 PageTableLayerIndex, int32& TextureReferenceIndex, EMaterialSamplerType SamplerType) = 0;
	virtual int32 VirtualTextureParameter(FName ParameterName, URuntimeVirtualTexture* DefaultValue, int32 TextureLayerIndex, int32 PageTableLayerIndex, int32& TextureReferenceIndex, EMaterialSamplerType SamplerType) = 0;
	virtual int32 VirtualTextureUniform(int32 TextureIndex, int32 VectorIndex) = 0;
	virtual int32 VirtualTextureUniform(FName ParameterName, int32 TextureIndex, int32 VectorIndex) = 0;
	virtual int32 VirtualTextureWorldToUV(int32 WorldPositionIndex, int32 P0, int32 P1, int32 P2) = 0;
	virtual int32 VirtualTextureUnpack(int32 CodeIndex0, int32 CodeIndex1, int32 CodeIndex2, EVirtualTextureUnpackType UnpackType) = 0;

	virtual int32 ExternalTexture(const FGuid& ExternalTextureGuid) = 0;
	virtual int32 ExternalTexture(UTexture* InTexture, int32& TextureReferenceIndex) = 0;
	virtual int32 ExternalTextureParameter(FName ParameterName, UTexture* DefaultValue, int32& TextureReferenceIndex) = 0;
	virtual int32 ExternalTextureCoordinateScaleRotation(int32 TextureReferenceIndex, TOptional<FName> ParameterName) = 0;
	virtual int32 ExternalTextureCoordinateScaleRotation(const FGuid& ExternalTextureGuid) = 0;
	virtual int32 ExternalTextureCoordinateOffset(int32 TextureReferenceIndex, TOptional<FName> ParameterName) = 0;
	virtual int32 ExternalTextureCoordinateOffset(const FGuid& ExternalTextureGuid) = 0;

	virtual UObject* GetReferencedTexture(int32 Index) { return nullptr; }

	int32 Texture(UTexture* InTexture, EMaterialSamplerType SamplerType, ESamplerSourceMode SamplerSource=SSM_FromTextureAsset)
	{
		int32 TextureReferenceIndex = INDEX_NONE;
		return Texture(InTexture, TextureReferenceIndex, SamplerType, SamplerSource);
	}

	int32 VirtualTexture(URuntimeVirtualTexture* InTexture, int32 TextureLayerIndex, int32 PageTableLayerIndex, EMaterialSamplerType SamplerType)
	{
		int32 TextureReferenceIndex = INDEX_NONE;
		return VirtualTexture(InTexture, TextureLayerIndex, PageTableLayerIndex, TextureReferenceIndex, SamplerType);
	}

	int32 VirtualTextureParameter(FName ParameterName, URuntimeVirtualTexture* DefaultValue, int32 TextureLayerIndex, int32 PageTableLayerIndex, EMaterialSamplerType SamplerType)
	{
		int32 TextureReferenceIndex = INDEX_NONE;
		return VirtualTextureParameter(ParameterName, DefaultValue, TextureLayerIndex, PageTableLayerIndex, TextureReferenceIndex, SamplerType);
	}

	int32 ExternalTexture(UTexture* DefaultTexture)
	{
		int32 TextureReferenceIndex = INDEX_NONE;
		return ExternalTexture(DefaultTexture, TextureReferenceIndex);
	}

	int32 TextureParameter(FName ParameterName,UTexture* DefaultTexture, EMaterialSamplerType SamplerType, ESamplerSourceMode SamplerSource=SSM_FromTextureAsset)
	{
		int32 TextureReferenceIndex = INDEX_NONE;
		return  TextureParameter(ParameterName, DefaultTexture, TextureReferenceIndex, SamplerType, SamplerSource);
	}

	int32 ExternalTextureParameter(FName ParameterName, UTexture* DefaultTexture)
	{
		int32 TextureReferenceIndex = INDEX_NONE;
		return ExternalTextureParameter(ParameterName, DefaultTexture, TextureReferenceIndex);
	}

	virtual	int32 PixelDepth()=0;
	virtual int32 SceneDepth(int32 Offset, int32 ViewportUV, bool bUseOffset) = 0;
	virtual int32 SceneColor(int32 Offset, int32 ViewportUV, bool bUseOffset) = 0;
	// @param SceneTextureId of type ESceneTextureId e.g. PPI_SubsurfaceColor
	virtual int32 SceneTextureLookup(int32 ViewportUV, uint32 SceneTextureId, bool bFiltered) = 0;
	virtual int32 GetSceneTextureViewSize(int32 SceneTextureId, bool InvProperty) = 0;

	virtual int32 StaticBool(bool Value) = 0;
	virtual int32 StaticBoolParameter(FName ParameterName,bool bDefaultValue) = 0;
	virtual int32 StaticComponentMask(int32 Vector,FName ParameterName,bool bDefaultR,bool bDefaultG,bool bDefaultB,bool bDefaultA) = 0;
	virtual const FMaterialLayersFunctions* StaticMaterialLayersParameter(FName ParameterName) = 0;
	virtual bool GetStaticBoolValue(int32 BoolIndex, bool& bSucceeded) = 0;
	virtual int32 StaticTerrainLayerWeight(FName ParameterName,int32 Default) = 0;

	virtual int32 VertexColor() = 0;

	virtual int32 PreSkinnedPosition() = 0;
	virtual int32 PreSkinnedNormal() = 0;

	virtual int32 VertexInterpolator(uint32 InterpolatorIndex) = 0;

#if WITH_EDITOR
	virtual int32 MaterialBakingWorldPosition() = 0;
#endif

	virtual int32 Add(int32 A,int32 B) = 0;
	virtual int32 Sub(int32 A,int32 B) = 0;
	virtual int32 Mul(int32 A,int32 B) = 0;
	virtual int32 Div(int32 A,int32 B) = 0;
	virtual int32 Dot(int32 A,int32 B) = 0;
	virtual int32 Cross(int32 A,int32 B) = 0;

	virtual int32 Power(int32 Base,int32 Exponent) = 0;
	virtual int32 Logarithm2(int32 X) = 0;
	virtual int32 Logarithm10(int32 X) = 0;
	virtual int32 SquareRoot(int32 X) = 0;
	virtual int32 Length(int32 X) = 0;

	virtual int32 Lerp(int32 X,int32 Y,int32 A) = 0;
	virtual int32 Min(int32 A,int32 B) = 0;
	virtual int32 Max(int32 A,int32 B) = 0;
	virtual int32 Clamp(int32 X,int32 A,int32 B) = 0;
	virtual int32 Saturate(int32 X) = 0;

	virtual int32 ComponentMask(int32 Vector,bool R,bool G,bool B,bool A) = 0;
	virtual int32 AppendVector(int32 A,int32 B) = 0;
	virtual int32 TransformVector(EMaterialCommonBasis SourceCoordBasis, EMaterialCommonBasis DestCoordBasis, int32 A) = 0;
	virtual int32 TransformPosition(EMaterialCommonBasis SourceCoordBasis, EMaterialCommonBasis DestCoordBasis, int32 A) = 0;

	virtual int32 DynamicParameter(FLinearColor& DefaultValue, uint32 ParameterIndex = 0) = 0;
	virtual int32 LightmapUVs() = 0;
	virtual int32 PrecomputedAOMask()  = 0;

	virtual int32 GIReplace(int32 Direct, int32 StaticIndirect, int32 DynamicIndirect) = 0;
	virtual int32 ShadowReplace(int32 Default, int32 Shadow) = 0;
	virtual int32 RayTracingQualitySwitchReplace(int32 Normal, int32 RayTraced) = 0;
	virtual int32 VirtualTextureOutputReplace(int32 Default, int32 VirtualTexture) = 0;

	virtual int32 ObjectOrientation() = 0;
	virtual int32 RotateAboutAxis(int32 NormalizedRotationAxisAndAngleIndex, int32 PositionOnAxisIndex, int32 PositionIndex) = 0;
	virtual int32 TwoSidedSign() = 0;
	virtual int32 VertexNormal() = 0;
	virtual int32 PixelNormalWS() = 0;

	virtual int32 CustomExpression(class UMaterialExpressionCustom* Custom, TArray<int32>& CompiledInputs) = 0;
	virtual int32 CustomOutput(class UMaterialExpressionCustomOutput* Custom, int32 OutputIndex, int32 OutputCode) = 0;
	virtual int32 VirtualTextureOutput() = 0;

	virtual int32 DDX(int32 X) = 0;
	virtual int32 DDY(int32 X) = 0;

	virtual int32 PerInstanceRandom() = 0;
	//#Change by wh, 2019/6/10 
	virtual int32 PerInstanceShadowFakery() = 0;
	//end
	virtual int32 PerInstanceFadeAmount() = 0;
	virtual int32 AntialiasedTextureMask(int32 Tex, int32 UV, float Threshold, uint8 Channel) = 0;
	virtual int32 Sobol(int32 Cell, int32 Index, int32 Seed) = 0;
	virtual int32 TemporalSobol(int32 Index, int32 Seed) = 0;
	virtual int32 Noise(int32 Position, float Scale, int32 Quality, uint8 NoiseFunction, bool bTurbulence, int32 Levels, float OutputMin, float OutputMax, float LevelScale, int32 FilterWidth, bool bTiling, uint32 RepeatSize) = 0;
	virtual int32 VectorNoise(int32 Position, int32 Quality, uint8 NoiseFunction, bool bTiling, uint32 RepeatSize) = 0;
	virtual int32 BlackBody( int32 Temp ) = 0;
	virtual int32 DistanceToNearestSurface(int32 PositionArg) = 0;
	virtual int32 DistanceFieldGradient(int32 PositionArg) = 0;
	virtual int32 DepthOfFieldFunction(int32 Depth, int32 FunctionValueIndex) = 0;
	virtual int32 AtmosphericFogColor(int32 WorldPosition) = 0;
	virtual int32 RotateScaleOffsetTexCoords(int32 TexCoordCodeIndex, int32 RotationScale, int32 Offset) = 0;
	virtual int32 SpeedTree(int32 GeometryArg, int32 WindArg, int32 LODArg, float BillboardThreshold, bool bAccurateWindVelocities, bool bExtraBend, int32 ExtraBendArg) = 0;
	virtual int32 TextureCoordinateOffset() = 0;
	virtual int32 EyeAdaptation() = 0;
	virtual int32 AtmosphericLightVector() = 0;
	virtual int32 AtmosphericLightColor() = 0;
	virtual int32 SkyAtmosphereLightIlluminance(int32 WorldPosition, int32 LightIndex) = 0;
	virtual int32 SkyAtmosphereLightDirection(int32 LightIndex) = 0;
	virtual int32 SkyAtmosphereLightDiskLuminance(int32 LightIndex) = 0;
	virtual int32 SkyAtmosphereViewLuminance() = 0;
	virtual int32 SkyAtmosphereAerialPerspective(int32 WorldPosition) = 0;
	virtual int32 SkyAtmosphereDistantLightScatteredLuminance() = 0;
	virtual int32 GetHairUV() = 0;
	virtual int32 GetHairDimensions() = 0;
	virtual int32 GetHairSeed() = 0;
	virtual int32 GetHairTangent() = 0;
	virtual int32 GetHairRootUV() = 0;
	virtual int32 GetHairBaseColor() = 0;
	virtual int32 GetHairRoughness() = 0;
	virtual int32 CustomPrimitiveData(int32 OutputIndex, EMaterialValueType Type) = 0;
	virtual int32 ShadingModel(EMaterialShadingModel InSelectedShadingModel) = 0;


	virtual int32 MapARPassthroughCameraUV(int32 UV) = 0;
	// The compiler can run in a different state and this affects caching of sub expression, Expressions are different (e.g. View.PrevWorldViewOrigin) when using previous frame's values
	// If possible we should re-factor this to avoid having to deal with compiler state
	virtual bool IsCurrentlyCompilingForPreviousFrame() const { return false; }
	virtual bool IsDevelopmentFeatureEnabled(const FName& FeatureName) const { return true; }
};

/** 
 * A proxy for the material compiler interface which by default passes all function calls unmodified. 
 * Note: Any functions of FMaterialCompiler that change the internal compiler state must be routed!
 */
class FProxyMaterialCompiler : public FMaterialCompiler
{
public:

	// Constructor.
	FProxyMaterialCompiler(FMaterialCompiler* InCompiler):
		Compiler(InCompiler)
	{}

	// Simple pass through all other material operations unmodified.

	virtual FMaterialShadingModelField GetMaterialShadingModels() const { return Compiler->GetMaterialShadingModels(); }
	virtual EMaterialValueType GetParameterType(int32 Index) const { return Compiler->GetParameterType(Index); }
	virtual FMaterialUniformExpression* GetParameterUniformExpression(int32 Index) const { return Compiler->GetParameterUniformExpression(Index); }
	virtual bool GetTextureForExpression(int32 Index, int32& OutTextureIndex, EMaterialSamplerType& OutSamplerType, TOptional<FName>& OutParameterName) const override { return Compiler->GetTextureForExpression(Index, OutTextureIndex, OutSamplerType, OutParameterName); }
	virtual void SetMaterialProperty(EMaterialProperty InProperty, EShaderFrequency OverrideShaderFrequency, bool bUsePreviousFrameTime) override { Compiler->SetMaterialProperty(InProperty, OverrideShaderFrequency, bUsePreviousFrameTime); }
	virtual void PushMaterialAttribute(const FGuid& InAttributeID) override { Compiler->PushMaterialAttribute(InAttributeID); }
	virtual FGuid PopMaterialAttribute() override { return Compiler->PopMaterialAttribute(); }
	virtual const FGuid GetMaterialAttribute() override { return Compiler->GetMaterialAttribute(); }
	virtual void SetBaseMaterialAttribute(const FGuid& InAttributeID) override { Compiler->SetBaseMaterialAttribute(InAttributeID); }

	virtual void PushParameterOwner(const FMaterialParameterInfo& InOwnerInfo) override { Compiler->PushParameterOwner(InOwnerInfo); }
	virtual FMaterialParameterInfo PopParameterOwner() override { return Compiler->PopParameterOwner(); }

	virtual EShaderFrequency GetCurrentShaderFrequency() const override { return Compiler->GetCurrentShaderFrequency(); }
	virtual int32 Error(const TCHAR* Text) override { return Compiler->Error(Text); }
	virtual void AppendExpressionError(UMaterialExpression* Expression, const TCHAR* Text) override { return Compiler->AppendExpressionError(Expression, Text); }

	virtual int32 CallExpression(FMaterialExpressionKey ExpressionKey,FMaterialCompiler* InCompiler) override { return Compiler->CallExpression(ExpressionKey,InCompiler); }

	virtual void PushFunction(FMaterialFunctionCompileState* FunctionState) override { Compiler->PushFunction(FunctionState); }
	virtual FMaterialFunctionCompileState* PopFunction() override { return Compiler->PopFunction(); }
	virtual int32 GetCurrentFunctionStackDepth() override { return Compiler->GetCurrentFunctionStackDepth(); }

	virtual EMaterialValueType GetType(int32 Code) override { return Compiler->GetType(Code); }
	virtual EMaterialQualityLevel::Type GetQualityLevel() override { return Compiler->GetQualityLevel(); }
	virtual ERHIFeatureLevel::Type GetFeatureLevel() override { return Compiler->GetFeatureLevel(); }
	virtual EShaderPlatform GetShaderPlatform() override { return Compiler->GetShaderPlatform(); }
	virtual const ITargetPlatform* GetTargetPlatform() const override { return Compiler->GetTargetPlatform(); }
	virtual int32 ValidCast(int32 Code,EMaterialValueType DestType) override { return Compiler->ValidCast(Code, DestType); }
	virtual int32 ForceCast(int32 Code,EMaterialValueType DestType,uint32 ForceCastFlags = 0) override
	{ return Compiler->ForceCast(Code,DestType,ForceCastFlags); }

	virtual int32 AccessCollectionParameter(UMaterialParameterCollection* ParameterCollection, int32 ParameterIndex, int32 ComponentIndex) override { return Compiler->AccessCollectionParameter(ParameterCollection, ParameterIndex, ComponentIndex); }
	virtual int32 ScalarParameter(FName ParameterName, float DefaultValue) override { return Compiler->ScalarParameter(ParameterName,DefaultValue); }
	virtual int32 VectorParameter(FName ParameterName, const FLinearColor& DefaultValue) override { return Compiler->VectorParameter(ParameterName,DefaultValue); }

	virtual int32 Constant(float X) override { return Compiler->Constant(X); }
	virtual int32 Constant2(float X,float Y) override { return Compiler->Constant2(X,Y); }
	virtual int32 Constant3(float X,float Y,float Z) override { return Compiler->Constant3(X,Y,Z); }
	virtual int32 Constant4(float X,float Y,float Z,float W) override { return Compiler->Constant4(X,Y,Z,W); }
	
	virtual	int32 ViewProperty(EMaterialExposedViewProperty Property, bool InvProperty) override { return Compiler->ViewProperty(Property, InvProperty); }

	virtual int32 GameTime(bool bPeriodic, float Period) override { return Compiler->GameTime(bPeriodic, Period); }
	virtual int32 RealTime(bool bPeriodic, float Period) override { return Compiler->RealTime(bPeriodic, Period); }
	virtual int32 DeltaTime() override { return Compiler->DeltaTime(); }

	virtual int32 PeriodicHint(int32 PeriodicCode) override { return Compiler->PeriodicHint(PeriodicCode); }

	virtual int32 Sine(int32 X) override { return Compiler->Sine(X); }
	virtual int32 Cosine(int32 X) override { return Compiler->Cosine(X); }
	virtual int32 Tangent(int32 X) override { return Compiler->Tangent(X); }
	virtual int32 Arcsine(int32 X) override { return Compiler->Arcsine(X); }
	virtual int32 ArcsineFast(int32 X) override { return Compiler->ArcsineFast(X); }
	virtual int32 Arccosine(int32 X) override { return Compiler->Arccosine(X); }
	virtual int32 ArccosineFast(int32 X) override { return Compiler->ArccosineFast(X); }
	virtual int32 Arctangent(int32 X) override { return Compiler->Arctangent(X); }
	virtual int32 ArctangentFast(int32 X) override { return Compiler->ArctangentFast(X); }
	virtual int32 Arctangent2(int32 Y, int32 X) override { return Compiler->Arctangent2(Y, X); }
	virtual int32 Arctangent2Fast(int32 Y, int32 X) override { return Compiler->Arctangent2Fast(Y, X); }

	virtual int32 Floor(int32 X) override { return Compiler->Floor(X); }
	virtual int32 Ceil(int32 X) override { return Compiler->Ceil(X); }
	virtual int32 Round(int32 X) override { return Compiler->Round(X); }
	virtual int32 Truncate(int32 X) override { return Compiler->Truncate(X); }
	virtual int32 Sign(int32 X) override { return Compiler->Sign(X); }
	virtual int32 Frac(int32 X) override { return Compiler->Frac(X); }
	virtual int32 Fmod(int32 A, int32 B) override { return Compiler->Fmod(A,B); }
	virtual int32 Abs(int32 X) override { return Compiler->Abs(X); }

	virtual int32 ReflectionVector() override { return Compiler->ReflectionVector(); }
	virtual int32 CameraVector() override { return Compiler->CameraVector(); }
	virtual int32 LightVector() override { return Compiler->LightVector(); }

	virtual int32 GetViewportUV() override { return Compiler->GetViewportUV(); }
	virtual int32 GetPixelPosition() override { return Compiler->GetPixelPosition(); }
	virtual int32 WorldPosition(EWorldPositionIncludedOffsets WorldPositionIncludedOffsets) override { return Compiler->WorldPosition(WorldPositionIncludedOffsets); }
	virtual int32 ObjectWorldPosition() override { return Compiler->ObjectWorldPosition(); }
	virtual int32 ObjectRadius() override { return Compiler->ObjectRadius(); }
	virtual int32 ObjectBounds() override { return Compiler->ObjectBounds(); }
	virtual int32 PreSkinnedLocalBounds(int32 OutputIndex) override { return Compiler->PreSkinnedLocalBounds(OutputIndex); }
	virtual int32 DistanceCullFade() override { return Compiler->DistanceCullFade(); }
	virtual int32 ActorWorldPosition() override { return Compiler->ActorWorldPosition(); }
	virtual int32 ParticleMacroUV() override { return Compiler->ParticleMacroUV(); }
	virtual int32 ParticleSubUV(int32 TextureIndex, EMaterialSamplerType SamplerType, bool bBlend) override { return Compiler->ParticleSubUV(TextureIndex, SamplerType, bBlend); }
	virtual int32 ParticleSubUVProperty(int32 PropertyIndex) override { return Compiler->ParticleSubUVProperty(PropertyIndex); }
	virtual int32 ParticleColor() override { return Compiler->ParticleColor(); }
	virtual int32 ParticlePosition() override { return Compiler->ParticlePosition(); }
	virtual int32 ParticleRadius() override { return Compiler->ParticleRadius(); }
	virtual int32 SphericalParticleOpacity(int32 Density) override { return Compiler->SphericalParticleOpacity(Density); }

	virtual int32 If(int32 A,int32 B,int32 AGreaterThanB,int32 AEqualsB,int32 ALessThanB,int32 Threshold) override { return Compiler->If(A,B,AGreaterThanB,AEqualsB,ALessThanB,Threshold); }

	virtual int32 TextureSample(int32 InTexture,int32 Coordinate,enum EMaterialSamplerType SamplerType,int32 MipValue0Index,int32 MipValue1Index,ETextureMipValueMode MipValueMode,ESamplerSourceMode SamplerSource,int32 TextureReferenceIndex, bool AutomaticViewMipBias) override
		{ return Compiler->TextureSample(InTexture,Coordinate,SamplerType,MipValue0Index,MipValue1Index,MipValueMode,SamplerSource,TextureReferenceIndex, AutomaticViewMipBias); }
	virtual int32 TextureProperty(int32 InTexture, EMaterialExposedTextureProperty Property) override 
		{ return Compiler->TextureProperty(InTexture, Property); }

	virtual int32 TextureCoordinate(uint32 CoordinateIndex, bool UnMirrorU, bool UnMirrorV) override { return Compiler->TextureCoordinate(CoordinateIndex, UnMirrorU, UnMirrorV); }

	virtual int32 TextureDecalMipmapLevel(int32 TextureSizeInput) override { return Compiler->TextureDecalMipmapLevel(TextureSizeInput); }
	virtual int32 TextureDecalDerivative(bool bDDY) override { return Compiler->TextureDecalDerivative(bDDY); }
	virtual int32 DecalLifetimeOpacity() override { return Compiler->DecalLifetimeOpacity(); }

	virtual int32 Texture(UTexture* InTexture,int32& TextureReferenceIndex,EMaterialSamplerType SamplerType,ESamplerSourceMode SamplerSource=SSM_FromTextureAsset,ETextureMipValueMode MipValueMode=TMVM_None) override { return Compiler->Texture(InTexture,TextureReferenceIndex, SamplerType, SamplerSource,MipValueMode); }
	virtual int32 TextureParameter(FName ParameterName,UTexture* DefaultValue,int32& TextureReferenceIndex, EMaterialSamplerType SamplerType, ESamplerSourceMode SamplerSource=SSM_FromTextureAsset) override { return Compiler->TextureParameter(ParameterName,DefaultValue,TextureReferenceIndex, SamplerType, SamplerSource); }

	virtual int32 VirtualTexture(URuntimeVirtualTexture* InTexture, int32 TextureLayerIndex, int32 PageTableLayerIndex, int32& TextureReferenceIndex, EMaterialSamplerType SamplerType) override
	{
		return Compiler->VirtualTexture(InTexture, TextureLayerIndex, PageTableLayerIndex, TextureReferenceIndex, SamplerType); 
	}
	virtual int32 VirtualTextureParameter(FName ParameterName, URuntimeVirtualTexture* DefaultValue, int32 TextureLayerIndex, int32 PageTableLayerIndex, int32& TextureReferenceIndex, EMaterialSamplerType SamplerType) override
	{
		return Compiler->VirtualTextureParameter(ParameterName, DefaultValue, TextureLayerIndex, PageTableLayerIndex, TextureReferenceIndex, SamplerType); 
	}
	virtual int32 VirtualTextureUniform(int32 TextureIndex, int32 VectorIndex) override { return Compiler->VirtualTextureUniform(TextureIndex, VectorIndex); }
	virtual int32 VirtualTextureUniform(FName ParameterName, int32 TextureIndex, int32 VectorIndex) override { return Compiler->VirtualTextureUniform(ParameterName, TextureIndex, VectorIndex); }
	virtual int32 VirtualTextureWorldToUV(int32 WorldPositionIndex, int32 P0, int32 P1, int32 P2) override { return Compiler->VirtualTextureWorldToUV(WorldPositionIndex, P0, P1, P2); }
	virtual int32 VirtualTextureUnpack(int32 CodeIndex0, int32 CodeIndex1, int32 CodeIndex2, EVirtualTextureUnpackType UnpackType) override { return Compiler->VirtualTextureUnpack(CodeIndex0, CodeIndex1, CodeIndex2, UnpackType); }

	virtual int32 ExternalTexture(const FGuid& ExternalTextureGuid) override { return Compiler->ExternalTexture(ExternalTextureGuid); }
	virtual int32 ExternalTexture(UTexture* InTexture, int32& TextureReferenceIndex) override { return Compiler->ExternalTexture(InTexture, TextureReferenceIndex); }
	virtual int32 ExternalTextureParameter(FName ParameterName, UTexture* DefaultValue, int32& TextureReferenceIndex) override { return Compiler->ExternalTextureParameter(ParameterName, DefaultValue, TextureReferenceIndex); }
	virtual int32 ExternalTextureCoordinateScaleRotation(int32 TextureReferenceIndex, TOptional<FName> ParameterName) override { return Compiler->ExternalTextureCoordinateScaleRotation(TextureReferenceIndex, ParameterName); }
	virtual int32 ExternalTextureCoordinateScaleRotation(const FGuid& ExternalTextureGuid) override { return Compiler->ExternalTextureCoordinateScaleRotation(ExternalTextureGuid); }
	virtual int32 ExternalTextureCoordinateOffset(int32 TextureReferenceIndex, TOptional<FName> ParameterName) override { return Compiler->ExternalTextureCoordinateOffset(TextureReferenceIndex, ParameterName); }
	virtual int32 ExternalTextureCoordinateOffset(const FGuid& ExternalTextureGuid) override { return Compiler->ExternalTextureCoordinateOffset(ExternalTextureGuid); }

	virtual UObject* GetReferencedTexture(int32 Index) override { return Compiler->GetReferencedTexture(Index); }

	virtual	int32 PixelDepth() override { return Compiler->PixelDepth();	}
	virtual int32 SceneDepth(int32 Offset, int32 ViewportUV, bool bUseOffset) override { return Compiler->SceneDepth(Offset, ViewportUV, bUseOffset); }
	virtual int32 SceneColor(int32 Offset, int32 ViewportUV, bool bUseOffset) override { return Compiler->SceneColor(Offset, ViewportUV, bUseOffset); }
	virtual int32 SceneTextureLookup(int32 ViewportUV, uint32 InSceneTextureId, bool bFiltered) override { return Compiler->SceneTextureLookup(ViewportUV, InSceneTextureId, bFiltered); }
	virtual int32 GetSceneTextureViewSize(int32 SceneTextureId, bool InvProperty) override { return Compiler->GetSceneTextureViewSize(SceneTextureId, InvProperty); }

	virtual int32 StaticBool(bool Value) override { return Compiler->StaticBool(Value); }
	virtual int32 StaticBoolParameter(FName ParameterName,bool bDefaultValue) override { return Compiler->StaticBoolParameter(ParameterName,bDefaultValue); }
	virtual int32 StaticComponentMask(int32 Vector,FName ParameterName,bool bDefaultR,bool bDefaultG,bool bDefaultB,bool bDefaultA) override { return Compiler->StaticComponentMask(Vector,ParameterName,bDefaultR,bDefaultG,bDefaultB,bDefaultA); }
	virtual const FMaterialLayersFunctions* StaticMaterialLayersParameter(FName ParameterName) override { return Compiler->StaticMaterialLayersParameter(ParameterName); }
	virtual bool GetStaticBoolValue(int32 BoolIndex, bool& bSucceeded) override { return Compiler->GetStaticBoolValue(BoolIndex, bSucceeded); }
	virtual int32 StaticTerrainLayerWeight(FName ParameterName,int32 Default) override { return Compiler->StaticTerrainLayerWeight(ParameterName,Default); }

	virtual int32 VertexColor() override { return Compiler->VertexColor(); }
	
	virtual int32 PreSkinnedPosition() override { return Compiler->PreSkinnedPosition(); }
	virtual int32 PreSkinnedNormal() override { return Compiler->PreSkinnedNormal(); }

	virtual int32 Add(int32 A,int32 B) override { return Compiler->Add(A,B); }
	virtual int32 Sub(int32 A,int32 B) override { return Compiler->Sub(A,B); }
	virtual int32 Mul(int32 A,int32 B) override { return Compiler->Mul(A,B); }
	virtual int32 Div(int32 A,int32 B) override { return Compiler->Div(A,B); }
	virtual int32 Dot(int32 A,int32 B) override { return Compiler->Dot(A,B); }
	virtual int32 Cross(int32 A,int32 B) override { return Compiler->Cross(A,B); }

	virtual int32 Power(int32 Base,int32 Exponent) override { return Compiler->Power(Base,Exponent); }
	virtual int32 Logarithm2(int32 X) override { return Compiler->Logarithm2(X); }
	virtual int32 Logarithm10(int32 X) override { return Compiler->Logarithm10(X); }
	virtual int32 SquareRoot(int32 X) override { return Compiler->SquareRoot(X); }
	virtual int32 Length(int32 X) override { return Compiler->Length(X); }

	virtual int32 Lerp(int32 X,int32 Y,int32 A) override { return Compiler->Lerp(X,Y,A); }
	virtual int32 Min(int32 A,int32 B) override { return Compiler->Min(A,B); }
	virtual int32 Max(int32 A,int32 B) override { return Compiler->Max(A,B); }
	virtual int32 Clamp(int32 X,int32 A,int32 B) override { return Compiler->Clamp(X,A,B); }
	virtual int32 Saturate(int32 X) override { return Compiler->Saturate(X); }

	virtual int32 ComponentMask(int32 Vector,bool R,bool G,bool B,bool A) override { return Compiler->ComponentMask(Vector,R,G,B,A); }
	virtual int32 AppendVector(int32 A,int32 B) override { return Compiler->AppendVector(A,B); }
	virtual int32 TransformVector(EMaterialCommonBasis SourceCoordBasis, EMaterialCommonBasis DestCoordBasis, int32 A) override
	{
		return Compiler->TransformVector(SourceCoordBasis, DestCoordBasis, A);
	}
	virtual int32 TransformPosition(EMaterialCommonBasis SourceCoordBasis, EMaterialCommonBasis DestCoordBasis, int32 A) override
	{
		return Compiler->TransformPosition(SourceCoordBasis, DestCoordBasis, A);
	}

	virtual int32 DynamicParameter(FLinearColor& DefaultValue, uint32 ParameterIndex = 0) override { return Compiler->DynamicParameter(DefaultValue, ParameterIndex); }
	virtual int32 LightmapUVs() override { return Compiler->LightmapUVs(); }
	virtual int32 PrecomputedAOMask() override { return Compiler->PrecomputedAOMask(); }

	virtual int32 GIReplace(int32 Direct, int32 StaticIndirect, int32 DynamicIndirect) override { return Compiler->GIReplace(Direct, StaticIndirect, DynamicIndirect); }
	virtual int32 ShadowReplace(int32 Default, int32 Shadow) override { return Compiler->ShadowReplace(Default, Shadow); }
	virtual int32 RayTracingQualitySwitchReplace(int32 Normal, int32 RayTraced) override { return Compiler->RayTracingQualitySwitchReplace(Normal, RayTraced); }
	virtual int32 VirtualTextureOutputReplace(int32 Default, int32 VirtualTexture) override { return Compiler->VirtualTextureOutputReplace(Default, VirtualTexture); }
	
	virtual int32 ObjectOrientation() override { return Compiler->ObjectOrientation(); }
	virtual int32 RotateAboutAxis(int32 NormalizedRotationAxisAndAngleIndex, int32 PositionOnAxisIndex, int32 PositionIndex) override
	{
		return Compiler->RotateAboutAxis(NormalizedRotationAxisAndAngleIndex, PositionOnAxisIndex, PositionIndex);
	}
	virtual int32 TwoSidedSign() override { return Compiler->TwoSidedSign(); }
	virtual int32 VertexNormal() override { return Compiler->VertexNormal(); }
	virtual int32 PixelNormalWS() override { return Compiler->PixelNormalWS(); }

	virtual int32 CustomExpression(class UMaterialExpressionCustom* Custom, TArray<int32>& CompiledInputs) override { return Compiler->CustomExpression(Custom,CompiledInputs); }
	virtual int32 CustomOutput(class UMaterialExpressionCustomOutput* Custom, int32 OutputIndex, int32 OutputCode) override{ return Compiler->CustomOutput(Custom, OutputIndex, OutputCode); }
	virtual int32 VirtualTextureOutput() override { return Compiler->VirtualTextureOutput(); }

	virtual int32 DDX(int32 X) override { return Compiler->DDX(X); }
	virtual int32 DDY(int32 X) override { return Compiler->DDY(X); }

	virtual int32 AntialiasedTextureMask(int32 Tex, int32 UV, float Threshold, uint8 Channel) override
	{
		return Compiler->AntialiasedTextureMask(Tex, UV, Threshold, Channel);
	}
	virtual int32 Sobol(int32 Cell, int32 Index, int32 Seed) override {	return Compiler->Sobol(Cell, Index, Seed); }
	virtual int32 TemporalSobol(int32 Index, int32 Seed) override { return Compiler->TemporalSobol(Index, Seed); }
	virtual int32 Noise(int32 Position, float Scale, int32 Quality, uint8 NoiseFunction, bool bTurbulence, int32 Levels, float OutputMin, float OutputMax, float LevelScale, int32 FilterWidth, bool bTiling, uint32 TileSize) override
	{
		return Compiler->Noise(Position, Scale, Quality, NoiseFunction, bTurbulence, Levels, OutputMin, OutputMax, LevelScale, FilterWidth, bTiling, TileSize);
	}
	virtual int32 VectorNoise(int32 Position, int32 Quality, uint8 NoiseFunction, bool bTiling, uint32 TileSize) override
	{
		return Compiler->VectorNoise(Position, Quality, NoiseFunction, bTiling, TileSize);
	}
	virtual int32 BlackBody( int32 Temp ) override { return Compiler->BlackBody(Temp); }
	virtual int32 DistanceToNearestSurface(int32 PositionArg) override { return Compiler->DistanceToNearestSurface(PositionArg); }
	virtual int32 DistanceFieldGradient(int32 PositionArg) override { return Compiler->DistanceFieldGradient(PositionArg); }
	virtual int32 PerInstanceRandom() override { return Compiler->PerInstanceRandom(); }
	//#Change by wh, 2019/6/10 
	virtual int32 PerInstanceShadowFakery() { return Compiler->PerInstanceShadowFakery(); };
	//end
	virtual int32 PerInstanceFadeAmount() override { return Compiler->PerInstanceFadeAmount(); }
	virtual int32 DepthOfFieldFunction(int32 Depth, int32 FunctionValueIndex) override
	{
		return Compiler->DepthOfFieldFunction(Depth, FunctionValueIndex);
	}

	virtual int32 GetHairUV() override { return Compiler->GetHairUV(); }
	virtual int32 GetHairDimensions() override { return Compiler->GetHairDimensions(); }
	virtual int32 GetHairSeed() override { return Compiler->GetHairSeed(); }
	virtual int32 GetHairTangent() override { return Compiler->GetHairTangent(); }
	virtual int32 GetHairRootUV() override { return Compiler->GetHairRootUV(); }
	virtual int32 GetHairBaseColor() override { return Compiler->GetHairBaseColor(); }
	virtual int32 GetHairRoughness() override { return Compiler->GetHairRoughness(); }

	virtual int32 RotateScaleOffsetTexCoords(int32 TexCoordCodeIndex, int32 RotationScale, int32 Offset) override
	{
		return Compiler->RotateScaleOffsetTexCoords(TexCoordCodeIndex, RotationScale, Offset);
	}

	virtual int32 SpeedTree(int32 GeometryArg, int32 WindArg, int32 LODArg, float BillboardThreshold, bool bAccurateWindVelocities, bool bExtraBend, int32 ExtraBendArg) override
	{ 
		return Compiler->SpeedTree(GeometryArg, WindArg, LODArg, BillboardThreshold, bAccurateWindVelocities, bExtraBend, ExtraBendArg);
	}

	virtual int32 AtmosphericFogColor(int32 WorldPosition) override
	{
		return Compiler->AtmosphericFogColor(WorldPosition);
	}

	virtual int32 AtmosphericLightVector() override
	{
		return Compiler->AtmosphericLightVector();
	}

	virtual int32 AtmosphericLightColor() override
	{
		return Compiler->AtmosphericLightColor();
	}

	virtual int32 SkyAtmosphereLightIlluminance(int32 WorldPosition, int32 LightIndex) override
	{
		return Compiler->SkyAtmosphereLightIlluminance(WorldPosition, LightIndex);
	}

	virtual int32 SkyAtmosphereLightDirection(int32 LightIndex) override
	{
		return Compiler->SkyAtmosphereLightDirection(LightIndex);
	}

	virtual int32 SkyAtmosphereLightDiskLuminance(int32 LightIndex) override
	{
		return Compiler->SkyAtmosphereLightDiskLuminance(LightIndex);
	}

	virtual int32 SkyAtmosphereViewLuminance() override
	{
		return Compiler->SkyAtmosphereViewLuminance();
	}

	virtual int32 SkyAtmosphereAerialPerspective(int32 WorldPosition) override
	{
		return Compiler->SkyAtmosphereAerialPerspective(WorldPosition);
	}

	virtual int32 SkyAtmosphereDistantLightScatteredLuminance() override
	{
		return Compiler->SkyAtmosphereDistantLightScatteredLuminance();
	}
	
	virtual int32 CustomPrimitiveData(int32 OutputIndex, EMaterialValueType Type) override
	{
		return Compiler->CustomPrimitiveData(OutputIndex, Type);
	}

	virtual int32 ShadingModel(EMaterialShadingModel InSelectedShadingModel) override
	{
		return Compiler->ShadingModel(InSelectedShadingModel);
	}

	virtual int32 MapARPassthroughCameraUV(int32 UV) override
	{
		return Compiler->MapARPassthroughCameraUV(UV);
	}

	virtual int32 TextureCoordinateOffset() override
	{
		return Compiler->TextureCoordinateOffset();
	}

	virtual int32 EyeAdaptation() override
	{
		return Compiler->EyeAdaptation();
	}

	virtual bool IsDevelopmentFeatureEnabled(const FName& FeatureName) const override
	{
		return Compiler->IsDevelopmentFeatureEnabled(FeatureName);
	}

protected:
		
	FMaterialCompiler* Compiler;
};

// Helper class to handle MaterialAttribute changes on the compiler stack
class FScopedMaterialCompilerAttribute
{
public:
	FScopedMaterialCompilerAttribute(FMaterialCompiler* InCompiler, const FGuid& InAttributeID)
	: Compiler(InCompiler)
	, AttributeID(InAttributeID)
	{
		check(Compiler);
		Compiler->PushMaterialAttribute(AttributeID);
	}

	~FScopedMaterialCompilerAttribute()
	{
		verify(AttributeID == Compiler->PopMaterialAttribute());
	}

private:
	FMaterialCompiler*	Compiler;
	FGuid				AttributeID;
};
