在渲染VolumetricFog的时候，定义shader时可以发现其中有SHADER_PERMUTATION_BOOL的定义：

```cpp
class TVolumetricFogLightScatteringCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(TVolumetricFogLightScatteringCS)

	class FTemporalReprojection			: SHADER_PERMUTATION_BOOL("USE_TEMPORAL_REPROJECTION");
	class FDistanceFieldSkyOcclusion	: SHADER_PERMUTATION_BOOL("DISTANCE_FIELD_SKY_OCCLUSION");

	using FPermutationDomain = TShaderPermutationDomain<
		FTemporalReprojection,
		FDistanceFieldSkyOcclusion >;
}
```

这只能在FGlobalShader中使用，这是用于Shader编译的排列，如上声明了两个，那么在编译时就会编译4个不同的版本，可想而知这是指数型增长，所以一般不会声明太多。
如上SHADER_PERMUTATION_BOOL 本质上是下面的结构体，PermutationCount就是单个排列的数目，定义了2，类似于二进制
```cpp
struct FShaderPermutationBool
{
	/** Setup the dimension's type in permutation domain as boolean. */
	using Type = bool;

	/** Setup the dimension's number of permutation. */
	static constexpr int32 PermutationCount = 2;

    /** Converts dimension boolean value to dimension's value id. */
	static int32 ToDimensionValueId(Type E)
	{
		return E ? 1 : 0;
	}

}
```

在ShaderPermutation.h文件里有详细的内容，通过一系列的模板操作来实现这些功能，就是通过TShaderPermutationDomain来实现，如下定义:
```cpp
template <typename TDimension, typename... Ts>
struct TShaderPermutationDomain<TDimension, Ts...>
{
	/** Setup the dimension's type in permutation domain as itself so that a permutation domain can be
	 * used as a dimension of another domain.
	 */
	using Type = TShaderPermutationDomain<TDimension, Ts...>;

	/** Define a domain as a multidimensional dimension so that ModifyCompilationEnvironment() is used. */
	static constexpr bool IsMultiDimensional = true;

	/** Parent type in the variadic template to reduce code. */
	using Super = TShaderPermutationDomain<Ts...>;

    /** Total number of permutation within the domain. */
    // 这里实质上就是使用模板进行迭代在编译期计算出来的，可以看到constexpr的声明
	static constexpr int32 PermutationCount = Super::PermutationCount * TDimension::PermutationCount;
    ...

    typename TDimension::Type DimensionValue;
	Super Tail;
}
```
实际上就是通过模板进行迭代计算。

在编译GlobalShader时就是进行依次编译：
```cpp
int32 PermutationCountToCompile = 0;
		for (int32 PermutationId = 0; PermutationId < GlobalShaderType->GetPermutationCount(); PermutationId++)
		{
			if (GlobalShaderType->ShouldCompilePermutation(Platform, PermutationId) && !GlobalShaderMap->HasShader(GlobalShaderType, PermutationId))
			
				// Compile this global shader type.
				auto* Job = FGlobalShaderTypeCompiler::BeginCompileShader(GlobalShaderType, PermutationId, Platform, nullptr, GlobalShaderJobs);
				TShaderTypePermutation<const FShaderType> ShaderTypePermutation(GlobalShaderType, PermutationId);
				check(!SharedShaderJobs.Find(ShaderTypePermutation));
				SharedShaderJobs.Add(ShaderTypePermutation, Job);
				PermutationCountToCompile++;
			}
		}

//Shader的定义如下，会传入PermutationCount
#define IMPLEMENT_GLOBAL_SHADER(ShaderClass,SourceFilename,FunctionName,Frequency) \
	ShaderClass::ShaderMetaType ShaderClass::StaticType( \
		TEXT(#ShaderClass), \
		TEXT(SourceFilename), \
		TEXT(FunctionName), \
		Frequency, \
		ShaderClass::FPermutationDomain::PermutationCount, \
        ...
```

在所有的Shader定义的时候都会声明一个FPermutationDomain的局部类型，只不过在不需要的时候是如下定义 using FPermutationDomain = FShaderPermutationNone。

在阅读源码的过程中可以学到不少东西，如在比较类型是否一致的时候使用了模板技巧来实现，如下：
```cpp
//如果类型相同，那么编译期会使用下面第二个类型（属于第一个的特化），同时还使用枚举来避免内存的申请
template<typename A, typename B>	struct TIsSame			{ enum { Value = false	}; };
template<typename T>				struct TIsSame<T, T>	{ enum { Value = true	}; };
```


## 注意事项
1. 编写自定义Shader时一定要对每个ShaderParameter进行序列化，这些参数是放在ConstBuffer中，序列化后才能和绑定的参数对应上，不进行序列化可能会导致对应的参数传不进去。

## 光照相关

1. Stationary天光在烘焙的时候会产生影响，但是静态物体天光高光项还是在runtime计算。Static天光会在只在烘焙时起作用，runtime静态物体不受天光影响，所以就没有天光高光。Movale天光则是纯实时运算，不参与烘焙。

|天光/Object|Static|Staionary|Movable|
|:----:|:----:|:----:|:----:|
|Static|烘焙天光Diffuse|烘焙天光Diffuse、Runtime天光高光|Runtime天光Diffuse、天光高光|
|Movable|无天光|Runtime天光Diffuse、天光高光|Runtime天光Diffuse、天光高光|

2. 移动平台的SkyLight高光无法正常显示，这是因为在Mobile天光预计算的Pixel Shader里有错误，在*ReflectionEnvironmentShaders.usf*中的*FilterPS*中：
```cpp
uint CubeSize = 1 << (NumMips - 1);
// (6*CubeSize*CubeSize)在移动平台上会当作半精度计算，所以这里会溢出
const float SolidAngleTexel = 4 * PI / (6 * CubeSize * CubeSize) * 2;
```
如下修改即可：
```cpp
uint CubeSize = 1 << (NumMips - 1);
const float SolidAngleTexel = 4 * PI / float(6 * CubeSize * CubeSize) * 2;
```

3. 在烘焙时，如果天空材质勾选*IsSky*选项，在开始烘焙是，天空的材质就不会被Capture到，因为烘焙时的Capture会设置为*EmmisveOnly*，这就导致Capture的标记*ViewFamily.EngineShowFlags.Lighting = !bCaptureEmissiveOnly;*为false，那么在CopmuteRelevance时被标记为*bDynamicRelevance*，但是在后续的*ComputeDynamicMeshRelevance*函数中又不会添加*EMeshPass::SkyPass*的材质，这也就导致天空球画不出来，解决方法就是在CopmuteRelevance时跳过这个选项，如下：
```cpp
bool IsRichView(const FSceneViewFamily& ViewFamily)
{
	// Flags which make the view rich when absent.
	if( !ViewFamily.EngineShowFlags.LOD ||
		// Force FDrawBasePassDynamicMeshAction to be used since it has access to the view and can implement the show flags
		!ViewFamily.EngineShowFlags.VolumetricLightmap ||
		!ViewFamily.EngineShowFlags.IndirectLightingCache ||
		(!ViewFamily.EngineShowFlags.Lighting) || // 这里就是非Lighting标记下，就会强制标记为DynamicRelevance，所以这里可以选择使用其他条件跳过
		!ViewFamily.EngineShowFlags.Materials)
	{
		return true;
	}
}
```

4. 天光IBL的Diffuse和Specular都是在GPU计算的，桌面端和移动端都会进行实时计算，主要内容就在*ReflectionEnvironmentCapture.cpp*中，入口函数就是*UpdateSkyCaptureContents()*， *ComputeAverageBrightness()* 函数计算平均亮度，*FilterReflectionEnvironment()* 计算Diffuse球谐系数和Specular预积分。

5. 在UE4中添加一个新的MeshPass时，要注意要专门为材质编辑器修改对应的代码，否则就会报错，在*PreviewmMaterial.cpp*文件中的*void ShouldCache*函数中：
```cpp
virtual bool ShouldCache(EShaderPlatform Platform, const FShaderType* ShaderType, const FVertexFactoryType* VertexFactoryType) const
{
	//```
	if (FCString::Stristr(ShaderType->GetName(), TEXT("RainDepth")))
	{
		bShaderTypeMatches = true;
	}
	//```
}
```
如果添加了一个名为*RainDepth*的Pass，需要加入上面一段代码，这样在检测的时候才不会Shader对应不上。