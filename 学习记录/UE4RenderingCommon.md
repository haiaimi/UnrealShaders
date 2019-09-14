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