## HLSLMaterialTranslator
继承于FMaterialCompiler ，FMaterialCompiler是用来将材质编辑器中的蓝图转变成可执行Shader代码，其中定义了许多Shader中的运算法则，如下：
```cpp

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
```
那么HLSLMaterialTranslator就是具体翻译的实现，因为UE4中的shader使用的是HLSL语法。材质编辑器中的每一个节点就是MaterialExpression，如Abs运算，其中对应的Expression就是UMaterialExpressionAbs，其声明与实现如下：
```cpp
.h
UCLASS(MinimalAPI, collapsecategories, hidecategories=Object)
class UMaterialExpressionAbs : public UMaterialExpression
{
	GENERATED_UCLASS_BODY()

	/** Link to the input expression to be evaluated */
	UPROPERTY()
	FExpressionInput Input;


	//~ Begin UMaterialExpression Interface
#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
#endif
	//~ End UMaterialExpression Interface

};

.cpp
//会调用HLSLMaterialTranslator来生成对应的代码
int32 UMaterialExpressionAbs::Compile( FMaterialCompiler* Compiler, int32 OutputIndex)
{
	int32 Result=INDEX_NONE;

	if( !Input.GetTracedInput().Expression )
	{
		// an input expression must exist
		Result = Compiler->Errorf( TEXT("Missing Abs input") );
	}
	else
	{
		// evaluate the input expression first and use that as
		// the parameter for the Abs expression
		Result = Compiler->Abs( Input.Compile(Compiler) );
	}

	return Result;
}

//用于获取hlsl中的对应方法text，以便写入到文件中
void UMaterialExpressionAbs::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Abs"));
}
```



## ShaderCompile
无论是GlobalShader，MaterialShader还是MeshMaterialShader都会进行编译。
首先要了解一下FShaderCompilingManager类型，这用于管理Shader编译，使用多线程的方式编译。UE4中有一个全局的CompilingManager，如下：
```cpp
/** The global shader compiling thread manager. */
extern ENGINE_API FShaderCompilingManager* GShaderCompilingManager;
```
它用于管理所有Shader的编译，其中的 FShaderCompileThreadRunnable 会以多线程的方式进行编译Shader，它会调用对应图形接口编译Shader的接口，按照Manager中的Compilejobs进行编译，FShaderCompilingManager有一个比较重要的方法AddJobs是用来向Manager中添加CompileJobs：
```cpp

	/** 
	 * Adds shader jobs to be asynchronously compiled. 
	 * FinishCompilation or ProcessAsyncResults must be used to get the results.
	 */
	ENGINE_API void AddJobs(TArray<FShaderCommonCompileJob*>& NewJobs, bool bApplyCompletedShaderMapForRendering, bool bOptimizeForLowLatency, bool bRecreateComponentRenderStateOnCompletion);

```

需要注意的是所有的MaterialShader和MeshMaterialShader都是通过void FMaterialShaderMap::Compile() 里面的流程来编译，如下函数
```cpp
/**
* Compiles the shaders for a material and caches them in this shader map.
* @param Material - The material to compile shaders for.
* @param InShaderMapId - the set of static parameters to compile for
* @param Platform - The platform to compile to
*/
void FMaterialShaderMap::Compile(
	FMaterial* Material,
	const FMaterialShaderMapId& InShaderMapId, 
	TRefCountPtr<FShaderCompilerEnvironment> MaterialEnvironment,
	const FMaterialCompilationOutput& InMaterialCompilationOutput,
	EShaderPlatform InPlatform,
	bool bSynchronousCompile,
	bool bApplyCompletedShaderMapForRendering)
{}
```
而GlobalShader有些不同，它不与材质或者顶点连接，这是一个比较简单的ShaderType实例。

首先整理一下MaterialShader、MeshMaterialShader的编译流程：

1. 在需要编译Shader的情况下调用FMaterial::CacheShaders，如在材质编辑器的状态下预览的时候就会调用，如下情况：
```cpp
//很明显这是材质编辑器中预览材质效果时调用的方法
FMatExpressionPreview* FMaterialEditor::GetExpressionPreview(UMaterialExpression* MaterialExpression, bool& bNewlyCreated)
{
	if( !Preview )
		{
			bNewlyCreated = true;
			Preview = new FMatExpressionPreview(MaterialExpression);
			ExpressionPreviews.Add(Preview);
			Preview->CacheShaders(GMaxRHIShaderPlatform, true);
		}
}
```
2. 在FMaterial::CacheShaders中会调用 FMaterial::BeginCompileShaderMap ，根据传入的id选择对应的ShaderMap编译，该方法在上面有所展示，首先会把材质编辑器的内容转换成Shader文件，然后再开始编译
```cpp
/**
* Compiles this material for Platform, storing the result in OutShaderMap
*
* @param ShaderMapId - the set of static parameters to compile
* @param Platform - the platform to compile for
* @param OutShaderMap - the shader map to compile
* @return - true if compile succeeded or was not necessary (shader map for ShaderMapId was found and was complete)
*/
bool FMaterial::BeginCompileShaderMap(
	const FMaterialShaderMapId& ShaderMapId, 
	EShaderPlatform Platform, 
	TRefCountPtr<FMaterialShaderMap>& OutShaderMap, 
	bool bApplyCompletedShaderMapForRendering)
```
3. 在翻译完后就会进行编译，会动态创建一个FMaterialShaderMap，然后调用其中的FMaterialShaderMap::Compile方法进行编译，其中也分成几个编译部分，分别如下：
	+ 迭代所有的FVertexFactoryType，编译ShaderMap对应的MeshMaterialShader
    	+ 编译对应FMeshMaterialShaderType
    	+ 编译对应FShaderPipelineType
	+ 编译ShaderMap对应的所有FMaterialShaderType，得到对应的jobs
	+ 编译ShaderMap中所有FShaderPipelineType类型（存在多个Shader，PS->GS->DS->HS->VS）的Shader
	+ 上面的步骤会创建多个FShaderCommonCompileJob，会添加到GShaderCompilingManager
```cpp
/**
* Compiles the shaders for a material and caches them in this shader map.
* @param Material - The material to compile shaders for.
* @param InShaderMapId - the set of static parameters to compile for
* @param Platform - The platform to compile to
*/
void FMaterialShaderMap::Compile(
	FMaterial* Material,
	const FMaterialShaderMapId& InShaderMapId, 
	TRefCountPtr<FShaderCompilerEnvironment> MaterialEnvironment,
	const FMaterialCompilationOutput& InMaterialCompilationOutput,
	EShaderPlatform InPlatform,
	bool bSynchronousCompile,
	bool bApplyCompletedShaderMapForRendering)
	{
		//上述步骤都在该方法中
		...
		// Note: using Material->IsPersistent() to detect whether this is a preview material which should have higher priority over background compiling 添加到GShaderCompilingManager
			GShaderCompilingManager->AddJobs(NewJobs, bApplyCompletedShaderMapForRendering && !bSynchronousCompile, bSynchronousCompile || !Material->IsPersistent(), bRecreateComponentRenderStateOnCompletion);
	}
```

GlobalShader编译是独立与MaterialShader与MeshMaterialShader，大致步骤如下：
1. 在某些情况下触发编译GlobalShader时会调用CompileGlobalShaderMap()全局函数，如在切换材质FeatureLevel时：
```cpp
extern ENGINE_API void CompileGlobalShaderMap(EShaderPlatform Platform, bool bRefreshShaderMap = false);
void UEditorEngine::SetMaterialsFeatureLevel(const ERHIFeatureLevel::Type InFeatureLevel)
{
	CompileGlobalShaderMap(...)
}
```
2. 在CompileGlobalShaderMap()中首先会加载GlobalShader文件，序列化GlobalShader，然后调用VerifyGlobalShaders编译：
   + 判断是否已经编译
   + 迭代所有的GlobalShader，调用FGlobalShaderTypeCompiler::BeginCompileShader()编译，可见GlobalShader有专门的ShaderTypeCompiler，当然其本质上也是调用了GlobalBeginCompileShader()
   + 迭代所有GlobalPipelineType，FGlobalShaderTypeCompiler::BeginCompileShaderPipeline()编译

3. 同样调用GShaderCompilingManager->AddJobs();来添加所有的jobs


## Material与MeshPassProcessor
前面已经研究过UE4中渲染Mesh的流程与Material编译的流程，但是当渲染带有材质的模型是什么样的流程呢？

在HLSLTranslator翻译对应Material的Shader后，这些文件会被临时放在"/Engine/Generated/Material.ush"、顶点定义放在#include "/Engine/Generated/VertexFactory.ush"，而UniformBuffer则是放在#include "/Engine/Generated/GeneratedUniformBuffers.ush" 

1. 首先可以了解一些BasePassRendering，使用FBasePassMeshProcessor，用于渲染基本的mesh，其对应的shader文件中就有包含对应生成的hlsl文件，如下：
```hlsl
#include "/Engine/Generated/Material.ush"
#include "BasePassCommon.ush"
#include "/Engine/Generated/VertexFactory.ush"
```

而Common.ush中则包含了生成的Buffer定义文件，如下:
```hlsl
// Generated file that contains uniform buffer declarations needed by the shader being compiled 
#include "/Engine/Generated/GeneratedUniformBuffers.ush" 
```

而普通模型渲染时候所使用的Shader类就如下：
```cpp
template<typename LightMapPolicyType>
void FBasePassMeshProcessor::Process(
	const FMeshBatch& RESTRICT MeshBatch,
	uint64 BatchElementMask,
	int32 StaticMeshId,
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
	const FMaterial& RESTRICT MaterialResource,
	EBlendMode BlendMode,
	EMaterialShadingModel ShadingModel,
	const LightMapPolicyType& RESTRICT LightMapPolicy,
	const typename LightMapPolicyType::ElementDataType& RESTRICT LightMapElementData,
	ERasterizerFillMode MeshFillMode,
	ERasterizerCullMode MeshCullMode)
{
	const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;

	const bool bRenderSkylight = Scene && Scene->ShouldRenderSkylightInBasePass(BlendMode) && ShadingModel != MSM_Unlit;
	const bool bRenderAtmosphericFog = IsTranslucentBlendMode(BlendMode) && (Scene && Scene->HasAtmosphericFog() && Scene->ReadOnlyCVARCache.bEnableAtmosphericFog);

	//下面就是渲染时使用的Shader类型
	TMeshProcessorShaders<
		TBasePassVertexShaderPolicyParamType<LightMapPolicyType>,
		FBaseHS,
		FBaseDS,
		TBasePassPixelShaderPolicyParamType<LightMapPolicyType>> BasePassShaders;
}
```
从这也可以看出我们平常使用的材质编辑器，并不是直接编写一个渲染管线，而是编写其中对应的函数，然后提供给渲染管线中的shader方法进行调用，我们可以在MaterialTemplate.ush中找到对应的方法：
```hlsl
half GetMaterialTranslucencyDirectionalLightingIntensity()
{
%s;
}

half GetMaterialTranslucentShadowDensityScale()
{
%s;
}

half GetMaterialTranslucentSelfShadowDensityScale()
{
%s;
}

half GetMaterialTranslucentSelfShadowSecondDensityScale()
{
%s;
}

half3 GetMaterialWorldDisplacement(FMaterialTessellationParameters Parameters)
{
%s;
}

half GetMaterialMaxDisplacement()
{
%s;
}

half GetMaterialTessellationMultiplier(FMaterialTessellationParameters Parameters)
{
%s;
}

half GetMaterialCustomData0(FMaterialPixelParameters Parameters)
{
%s;
}

half GetMaterialCustomData1(FMaterialPixelParameters Parameters)
{
%s;
}
```
是不是很熟悉，他们就是对应于材质编辑器中的那些节点引脚和相关编辑器里的设置，如下图：

![image](https://github.com/haiaimi/PictureRepository/blob/master/PictureRepository/Rendering%20Learning/UE4_MaterialEd_1.png)
![image](https://github.com/haiaimi/PictureRepository/blob/master/PictureRepository/Rendering%20Learning/UE4_MaterialEd_2.png)

2. 上面这些Shader如TBasePassVS,TBasePassHS,TBassPassDS,TBasePassPS，都会静态生成对应的实例也就是使用 IMPLEMENT_SHADER_TYPE 来生成，如下代码，其中也包含了对应Shader文件的路径：
```cpp
// Typedef is necessary because the C preprocessor thinks the comma in the template parameter list is a comma in the macro parameter list.
// BasePass Vertex Shader needs to include hull and domain shaders for tessellation, these only compile for D3D11
#define IMPLEMENT_BASEPASS_VERTEXSHADER_TYPE(LightMapPolicyType,LightMapPolicyName) \
	typedef TBasePassVS< LightMapPolicyType, false > TBasePassVS##LightMapPolicyName ; \
	IMPLEMENT_MATERIAL_SHADER_TYPE(template<>,TBasePassVS##LightMapPolicyName,TEXT("/Engine/Private/BasePassVertexShader.usf"),TEXT("Main"),SF_Vertex); \
	typedef TBasePassHS< LightMapPolicyType, false > TBasePassHS##LightMapPolicyName; \
	IMPLEMENT_MATERIAL_SHADER_TYPE(template<>,TBasePassHS##LightMapPolicyName,TEXT("/Engine/Private/BasePassTessellationShaders.usf"),TEXT("MainHull"),SF_Hull); \
	typedef TBasePassDS< LightMapPolicyType > TBasePassDS##LightMapPolicyName; \
	IMPLEMENT_MATERIAL_SHADER_TYPE(template<>,TBasePassDS##LightMapPolicyName,TEXT("/Engine/Private/BasePassTessellationShaders.usf"),TEXT("MainDomain"),SF_Domain); 

#define IMPLEMENT_BASEPASS_PIXELSHADER_TYPE(LightMapPolicyType,LightMapPolicyName,bEnableSkyLight,SkyLightName) \
	typedef TBasePassPS<LightMapPolicyType, bEnableSkyLight> TBasePassPS##LightMapPolicyName##SkyLightName; \
	IMPLEMENT_MATERIAL_SHADER_TYPE(template<>,TBasePassPS##LightMapPolicyName##SkyLightName,TEXT("/Engine/Private/BasePassPixelShader.usf"),TEXT("MainPS"),SF_Pixel);

```

3. 在渲染过程中Material是通过对应的VertexFactoryType来获取对应的FMeshMaterialShaderType从而得到Shader，因为前面已经了了解到VertexFactoryType会对应一个FMeshMaterialMap，由该类定义即可知：
```cpp
/**
 * The shaders which the render the material on a mesh generated by a particular vertex factory type.
 */
class FMeshMaterialShaderMap : public TShaderMap<FMeshMaterialShaderType>
{}
```
而FMaterialShaderMap不是这样，它存储着一个Material里所有的Shader，同时包含引用其中Shader的FMeshMaterialShaderMap，如下定义：
```cpp
/**
 * The set of material shaders for a single material.
 */
class FMaterialShaderMap : public TShaderMap<FMaterialShaderType>, public FDeferredCleanupInterface
{}
```

在TShaderMap中会有一个Shaders的TArray成员变量用于存储ShaderMap中的Shader，这些shader执行渲染流程的时候会获取，如下：
```cpp

/**
 * Finds the shader matching the template type and the passed in vertex factory, asserts if not found.
 */
FShader* FMaterial::GetShader(FMeshMaterialShaderType* ShaderType, FVertexFactoryType* VertexFactoryType, bool bFatalIfMissing) const
{
	//根据VertexFactpryType获取Shader
	const FMeshMaterialShaderMap* MeshShaderMap = RenderingThreadShaderMap->GetMeshShaderMap(VertexFactoryType);
	FShader* Shader = MeshShaderMap ? MeshShaderMap->GetShader(ShaderType) : nullptr;
}
```
这些Shader会在编译时加入,要注意这并不是在执行前面所提到的一系列Compile方法得到，因为那些Compile并没有真正进行了Shader的编译，只是向编译的线程中加入任务（前面所提的Jobs）。这些Shader是在编译完成后加入，可以看到如下的声明：
```cpp
/** Finalizes the given shader map results and optionally assigns the affected shader maps to materials, while attempting to stay within an execution time budget. */
	void FShaderCompilingManager::ProcessCompiledShaderMaps(TMap<int32, FShaderMapFinalizeResults>& CompiledShaderMaps, float TimeBudget);

```
其相对应的调用堆栈（按调用顺序）：

```cpp
void FEngineLoop:Tick();
void FShaderCompilingManager::ProcessAsyncResults(bool bLimitExecutionTime, bool bBlockOnGlobalShaderCompletion);
void FShaderCompilingManager::ProcessCompiledShaderMaps(TMap<int32,FShaderMapFinalizeResults>& CompiledShaderMaps, float TimeBudget);
bool FMaterialShaderMap::ProcessCompilationResults(const TArray<FShaderCommonCompileJob*>& InCompilationResults, int32& InOutJobIndex, float& TimeBudget, TMap<const FVertexFactoryType*, TArray<const FShaderPipelineType*> >& SharedPipelines);
FShader* FMaterialShaderMap::ProcessCompilationResultsForSingleJob(FShaderCompileJob* SingleJob, const FShaderPipelineType* ShaderPipeline, const FSHAHash& MaterialShaderMapHash);
//向Material里添加Shader
void TShaderMap<FMeshMaterialShaderType>::AddShader(FShaderType* Type, int32 PermutationId, FShader* Shader);
```
由上面的调用堆栈看出，添加Shader是在Engine的loop里进行的由GShaderCompilingManager进行管理。

4. 在前面MeshDrawProcessor中已经了解到MeshDraw有很多Pass，BasePass只是其中的一个，还有DepthPass，Velocity等等，他们都有对应的MeshDrawProcessor和Shader。先看一下在为每个Pass配置时的代码：
```cpp
void ComputeDynamicMeshRelevance(EShadingPath ShadingPath, bool bAddLightmapDensityCommands, const FPrimitiveViewRelevance& ViewRelevance, const FMeshBatchAndRelevance& MeshBatch, FViewInfo& View, FMeshPassMask& PassMask)
{
	const int32 NumElements = MeshBatch.Mesh->Elements.Num();

	if (ViewRelevance.bDrawRelevance && (ViewRelevance.bRenderInMainPass || ViewRelevance.bRenderCustomDepth))
	{
		if (ShadingPath == EShadingPath::Mobile)
		{
			PassMask.Set(EMeshPass::MobileBasePassCSM);
			View.NumVisibleDynamicMeshElements[EMeshPass::MobileBasePassCSM] += NumElements;
		}
		//设置CustomDepth
		if (ViewRelevance.bRenderCustomDepth)
		{
			PassMask.Set(EMeshPass::CustomDepth);
			View.NumVisibleDynamicMeshElements[EMeshPass::CustomDepth] += NumElements;
		}
		//设置LightmapDensity
		if (bAddLightmapDensityCommands)
		{
			PassMask.Set(EMeshPass::LightmapDensity);
			View.NumVisibleDynamicMeshElements[EMeshPass::LightmapDensity] += NumElements;
		}Velocity
		//设置
		if (ViewRelevance.bVelocityRelevance)
		{
			PassMask.Set(EMeshPass::Velocity);
			View.NumVisibleDynamicMeshElements[EMeshPass::Velocity] += NumElements;
		}
		...
	}
```

而FPrimitiveViewRelevance之前则提到过，它是为了配置Mesh的一些参数，在FPrimitiveSceneProxy中会有对应的获取方法，声明如下：
```cpp
	/**
	 * Determines the relevance of this primitive's elements to the given view.
	 * Called in the rendering thread.
	 * @param View - The view to determine relevance for.
	 * @return The relevance of the primitive's elements to the view.
	 */
	ENGINE_API virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const;
```
总之这些数据都是从游戏线程中UPrimitiveComponent中获取的，在渲染的时候会根据这些配置来决定是否要绘制这个MeshPass。

## VertexFactory
这主要是定义了顶点相关数据的类型（VertexFactoryType），它与FMeshMaterialShaderMap是一一对应的关系，关系就如下图：

![image](https://github.com/haiaimi/PictureRepository/blob/master/PictureRepository/Rendering%20Learning/UE4_MaterialShader_1.jpg)

### Compile VertexFactory File
那么它是如何加入到编译文件中，主要就是在FVertexFactoryType中的方法：
```cpp
/**
	* Calls the function ptr for the shader type on the given environment
	* @param Environment - shader compile environment to modify
	*/
	void FVertexFactoryType::ModifyCompilationEnvironment(EShaderPlatform Platform, const FMaterial* Material, FShaderCompilerEnvironment& OutEnvironment){
		// Set up the mapping from VertexFactory.usf to the vertex factory type's source code.
		FString VertexFactoryIncludeString = FString::Printf( TEXT("#include \"%s\""), GetShaderFilename() );
		//加入到虚拟路径中
		OutEnvironment.IncludeVirtualPathToContentsMap.Add(TEXT("/Engine/Generated/VertexFactory.ush"), VertexFactoryIncludeString);

		OutEnvironment.SetDefine(TEXT("HAS_PRIMITIVE_UNIFORM_BUFFER"), 1);

		(*ModifyCompilationEnvironmentRef)(this, Platform, Material, OutEnvironment);
	}
```
在调用ModifyCompilationEnvironment后就会把文件加入到编译虚拟路径"/Engine/Generated/VertexFactory.ush中，而该路径则在各个MeshPass的shader中包含，如BasePassPixelShader.usf中包含了VertexFactory.ush。它会针对不同的Mesh类型选择不同的文件。

同时在FMeshMaterialShaderType中会调用该方法，如下:
```cpp
/**
 * Enqueues a compilation for a new shader of this type.
 * @param Platform - The platform to compile for.
 * @param Material - The material to link the shader with.
 * @param VertexFactoryType - The vertex factory to compile with.
 */
FShaderCompileJob* FMeshMaterialShaderType::BeginCompileShader(
	uint32 ShaderMapId,
	EShaderPlatform Platform,
	const FMaterial* Material,
	FShaderCompilerEnvironment* MaterialEnvironment,
	FVertexFactoryType* VertexFactoryType,
	const FShaderPipelineType* ShaderPipeline,
	TArray<FShaderCommonCompileJob*>& NewJobs
	)
{
    ...
	VertexFactoryType->ModifyCompilationEnvironment(Platform, Material, ShaderEnvironment);
}
```
### VirtualPath

关于虚拟路径，这是FShaderCompilerEnvironment里的一成员个变量，TMap<FString, FString> IncludeVirtualPathToContentMap，这个字典里的key就是虚拟路径，value是shader文件的具体内容，它会在编译的时候直接替换已存在shader文件中虚拟路径内容。如BasePassPixelShader.usf中的#include "Engine/Generated/Material.ush" 会被替换为Material 的具体Shader内容（就是以MaterialTemplate为模板生成的文件），可以看出虚拟路径并不是真正的路径，它只是shader文件的的代名词。同时也会生成很多对应的UniformBuffer对应的代码，这些UniformBuffer虚拟路径最终会存在/Engine/Generated/GeneratedUniformBuffers.ush的虚拟路径中，然后依次展开，在调试引擎时会生成如下的字符串：
```cpp
#include "Engine/Generated/UniformBuffers/Material.ush"  //最先加入到虚拟路径对应文件中
#include "Engine/Generated/UniformBuffers/Views.ush"
#include "Engine/Generated/UniformBuffers/DrawRectangleParameters.ush"
#include "Engine/Generated/UniformBuffers/InstancedViews.ush"
#include "Engine/Generated/UniformBuffers/Primitive.ush"
#include "Engine/Generated/UniformBuffers/PrimitiveFade.ush"
#include "Engine/Generated/UniformBuffers/SceneTextureStruct.ush"
#include "Engine/Generated/UniformBuffers/ShadowDepthPass.ush"
...
```
而这个虚拟路径则是被/Engine/Private/Common.ush包含，所以之前自定义UniformBuffer时要在Shader文件中包含Common.ush文件。
#### UniformBuffer

在定义UniformBuffer相关数据的时候，其具体内容是由FShaderParameterMetaData负责，有一个全局的链表用于存放，q其存放格式与ShaderType类似，如下定义:
```cpp
static TLinkedList<FShaderParametersMetadata*>* GUniformStructList = nullptr;

TLinkedList<FShaderParametersMetadata*>*& FShaderParametersMetadata::GetStructList()
{
	return GUniformStructList;
}

```