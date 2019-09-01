## UnrealShader
前面已经了解了UE4中Material在引擎中具体实现流程，从游戏线程的UPrimitivecomponent到渲染线程的FPrimitiveSceneProxy，再到MeshDrawProcessor，其中还包含对应的从材质编辑器转换为Shader代码的过程以及其编译过程。

已经知道了Shader的来源，下面主要就是尽量剖析UE4Shader的具体内容，首先从我们材质编辑器中最先接触的CalcPixelMaterialInputs发方法说起，定义如下：
```hlsl
void CalcPixelMaterialInputs(in out FMaterialPixelParameters Parameters, in out FPixelMaterialInputs PixelMaterialInputs)
{
    //填充材质编辑器里生成的shader代码
}
```
首先看第一个参数FMaterialPixelParameters，它是由LocalVertexFactory里定义的方法获取:
```hlsl
/** Converts from vertex factory specific interpolants to a FMaterialPixelParameters, which is used by material inputs. */
FMaterialPixelParameters GetMaterialPixelParameters(FVertexFactoryInterpolantsVSToPS Interpolants, float4 SvPosition)
{
```

而这个获取的方法则是由BasePassPixelShader.usf里的FPixelShaderInOut_MainPS调用，然后把获取到的FMaterialPixelParameters传给CalcPixelMaterialInputs。

这些都不得不提VertexFactrory，VertexFactory就是定义Shader中顶点数据的类型，包括顶点缓冲、索引缓冲，以及定点布局之类的。和编译Shader一样，会通过宏来编译，主要是如下两个宏：
```cpp
#define IMPLEMENT_VERTEX_FACTORY_TYPE
#define IMPLEMENT_VERTEX_FACTORY_TYPE_EX
```
相关内容可见UE4Material。