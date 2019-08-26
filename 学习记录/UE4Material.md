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

首先可以了解一些BasePassRendering，使用FBasePassMeshProcessor，用于渲染基本的mesh，其对应的shader文件中就有包含对应生成的hlsl文件，如下：
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
是不是很熟悉，他们就是对应于材质编辑器中的那些节点引脚