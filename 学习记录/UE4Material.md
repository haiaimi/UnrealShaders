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

那么什么时候是Shader生成的，代码如下：
```cpp
bool FMaterial::BeginCompileShaderMap(
	const FMaterialShaderMapId& ShaderMapId, 
	EShaderPlatform Platform, 
	TRefCountPtr<FMaterialShaderMap>& OutShaderMap, 
	bool bApplyCompletedShaderMapForRendering)
{
#if WITH_EDITORONLY_DATA
	bool bSuccess = false;

	STAT(double MaterialCompileTime = 0);

	TRefCountPtr<FMaterialShaderMap> NewShaderMap = new FMaterialShaderMap();

	SCOPE_SECONDS_COUNTER(MaterialCompileTime);

	// Generate the material shader code. 生成对应Shader代码
	FMaterialCompilationOutput NewCompilationOutput;
	FHLSLMaterialTranslator MaterialTranslator(this,NewCompilationOutput,ShaderMapId.GetParameterSet(),Platform,GetQualityLevel(),ShaderMapId.FeatureLevel);
	bSuccess = MaterialTranslator.Translate();

	if(bSuccess)
	{
		// Create a shader compiler environment for the material that will be shared by all jobs from this material
		TRefCountPtr<FShaderCompilerEnvironment> MaterialEnvironment = new FShaderCompilerEnvironment();

		MaterialTranslator.GetMaterialEnvironment(Platform, *MaterialEnvironment);
		const FString MaterialShaderCode = MaterialTranslator.GetMaterialShaderCode();
		const bool bSynchronousCompile = RequiresSynchronousCompilation() || !GShaderCompilingManager->AllowAsynchronousShaderCompiling();

		MaterialEnvironment->IncludeVirtualPathToContentsMap.Add(TEXT("/Engine/Generated/Material.ush"), MaterialShaderCode);

		// Compile the shaders for the material. 正式编译代码
		NewShaderMap->Compile(this, ShaderMapId, MaterialEnvironment, NewCompilationOutput, Platform, bSynchronousCompile, bApplyCompletedShaderMapForRendering);
    }

```

上面代码中调用的函数，如下：
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
```
在平常使用的IMPLEMENT_SHADER_TYPE中本质上也是调用了这个函数进行编译，因为平常使用的本质上是FShaderType这个类，在构造函数中会进行注册，会有个全局的FShaderType链表，如下：
```cpp
static TLinkedList<FShaderType*>*			GShaderTypeList = nullptr;

void FMaterialShaderMap::Compile(...)
{
    ...
    // Iterate over all material shader types.
    //迭代所有的ShaderType选择对应的Shader进行编译
    TMap<FShaderType*, FShaderCompileJob*> SharedShaderJobs;
    for(TLinkedList<FShaderType*>::TIterator ShaderTypeIt(FShaderType::GetTypeList());ShaderTypeIt;ShaderTypeIt.Next())
    {
        FMaterialShaderType* ShaderType = ShaderTypeIt->GetMaterialShaderType();
        if (ShaderType &&  ShouldCacheMaterialShader(ShaderType, InPlatform, Material))
        {
            // Verify that the shader map Id contains inputs for any shaders that will be put into this shader map
            check(InShaderMapId.ContainsShaderType(ShaderType));

            // Compile this material shader for this material.
            TArray<FString> ShaderErrors;

            // Only compile the shader if we don't already have it
            if (!HasShader(ShaderType, /* PermutationId = */ 0))
            {
                auto* Job = ShaderType->BeginCompileShader(
                    CompilingId,
                    Material,
                    MaterialEnvironment,
                    nullptr,
                    InPlatform,
                    NewJobs
                    );
                check(!SharedShaderJobs.Find(ShaderType));
                SharedShaderJobs.Add(ShaderType, Job);
            }
            NumShaders++;
        }
    }
}

```

## ShaderCompile
无论是GlobalShader，MaterialShader还是MeshMaterialShader都会进行编译。
首先要了解一下FShaderCompilingManager类型，这用于管理Shader编译，使用多线程的方式编译。UE4中有一个全局的CompilingManager，如下：
```cpp
/** The global shader compiling thread manager. */
extern ENGINE_API FShaderCompilingManager* GShaderCompilingManager;
```
它用于管理所有Shader的编译。
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
而GlobalShader有些不同

