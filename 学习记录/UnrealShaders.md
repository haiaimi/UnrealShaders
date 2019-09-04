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

## BassPass-MainPS
在延迟渲染中会有多个RenderTarget，所以在MainPS会有多个RenderTarget可选，如下：
```hlsl
void MainPS(
#if PIXELSHADEROUTPUT_MRT0
		, out float4 OutTarget0 : SV_Target0
#endif

#if PIXELSHADEROUTPUT_MRT1
		, out float4 OutTarget1 : SV_Target1
#endif

#if PIXELSHADEROUTPUT_MRT2
		, out float4 OutTarget2 : SV_Target2
#endif

#if PIXELSHADEROUTPUT_MRT3
		, out float4 OutTarget3 : SV_Target3
#endif
)
```
而这些宏是由C++中定义，然后在Shader中二次定义，如下：
```hlsl
#define PIXELSHADEROUTPUT_BASEPASS 1
#define PIXELSHADEROUTPUT_MRT0 (!USES_GBUFFER || !SELECTIVE_BASEPASS_OUTPUTS || NEEDS_BASEPASS_VERTEX_FOGGING || USES_EMISSIVE_COLOR || ALLOW_STATIC_LIGHTING)
#define PIXELSHADEROUTPUT_MRT1 (USES_GBUFFER && (!SELECTIVE_BASEPASS_OUTPUTS || !MATERIAL_SHADINGMODEL_UNLIT))
#define PIXELSHADEROUTPUT_MRT2 (USES_GBUFFER && (!SELECTIVE_BASEPASS_OUTPUTS || !MATERIAL_SHADINGMODEL_UNLIT))
#define PIXELSHADEROUTPUT_MRT3 (USES_GBUFFER && (!SELECTIVE_BASEPASS_OUTPUTS || !MATERIAL_SHADINGMODEL_UNLIT))
```

这一切宏的设定都是在FShaderCompilerEnvironment::SetDefine()中在ShaderCompiler.cpp中的GlobalBeginCompileShader方法中有很多宏的设置。

### FPixelShaderInOut_MainPS
下面主要就是理解FPixelShaderInOut_MainPS里的内容，他是计算最终显示效果的PixelShader，与材质编辑器里的内容息息相关。

首先看到的是INSTANCED_STEREO，它主要是VR以及高清前向渲染有关，它涉及到了ViewState这个结构体，它存放了很多数据，
