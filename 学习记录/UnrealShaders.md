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

1. 首先看到的是INSTANCED_STEREO，它主要是VR以及高清前向渲染有关，它涉及到了ViewState这个结构体，它存放了很多数据来记录当前View的一些状态，如float4x4 TranslatedWorldClip;
float4x4 ScreenToWorld等等数据。这个结构体同样是在ShaderCompiler.cpp中的方法生成的，同时还对应着ViewState GetPrimaryView(), ViewState GetInstancedView(),ViewState ResolveView(uint ViewIndex)方法。

**值得注意的是PixelShader在接受SV_POSITION的值时，它xy给的是当前渲染像素的位置，就相当于屏幕分辨率。z应该就是NDC空间里的z**

在FPixelShaderInOut_MainPS开始阶段就会调用Common里的方法：
SvPositionToResolvedScreenPosition()  //转换到视口坐标
SvPositionToResolvedTranslatedWorld() //同样是转换到视口坐标

2. 获取到对应的坐标位置后，就进行PixelMaterial的计算，这一步就是运行材质编辑器中生成的代码获取到FPixelMaterialInputs类型的值。
3. 计算Output PixelDepthOffset相关内容，就是把当前像素深度，向后偏移一定位置，其对应的Shader代码:
```cpp
float ApplyPixelDepthOffsetToMaterialParameters(inout FMaterialPixelParameters MaterialParameters, FPixelMaterialInputs PixelMaterialInputs, out float OutDepth)
{
	// 获取材质编辑器计算的偏移值
	float PixelDepthOffset = max(GetMaterialPixelDepthOffset(PixelMaterialInputs), 0);

    //ScreenPosition的w分量添加偏移值
	MaterialParameters.ScreenPosition.w += PixelDepthOffset;
	MaterialParameters.SvPosition.w = MaterialParameters.ScreenPosition.w;
    //朝相机方向添加偏移量
	MaterialParameters.AbsoluteWorldPosition += MaterialParameters.CameraVector * PixelDepthOffset;
    //计算当前像素的深度值
	OutDepth = MaterialParameters.ScreenPosition.z / MaterialParameters.ScreenPosition.w;

	return PixelDepthOffset;
}
```
4. 当前像素剔除，这就涉及到DX11中的**clip(In)**方法当In值小于0就直接停止计算。
5. 存储在第2步获得的值（局部变量），在下面使用。
6. 计算次表面(Subsurface)相关内容，在FPixelMaterialInputs中可以获取到对应的值，然后根据配置是否加入Disffuse因素。
7. 计算DBffer相关，就是渲染Decal所需的步骤，这个会涉及4个值DBufferATexture，DBufferBTexture，DBufferCTexture以及DBufferRenderMask（这应该是决定前面几个DBuffer的混合规则），Decal会把所有相关的属性计算进去，代码如下:
```cpp
void ApplyDBufferData(
	FDBufferData DBufferData, inout float3 WorldNormal, inout float3 SubsurfaceColor, inout float Roughness, 
	inout float3 BaseColor, inout float Metallic, inout float Specular )
{
	WorldNormal = WorldNormal * DBufferData.NormalOpacity + DBufferData.PreMulWorldNormal;
	Roughness = Roughness * DBufferData.RoughnessOpacity + DBufferData.PreMulRoughness;
	Metallic = Metallic * DBufferData.RoughnessOpacity + DBufferData.PreMulMetallic;
	Specular = Specular * DBufferData.RoughnessOpacity + DBufferData.PreMulSpecular;

	SubsurfaceColor *= DBufferData.ColorOpacity;

	BaseColor = BaseColor * DBufferData.ColorOpacity + DBufferData.PreMulColor;
}
```
8. 计算体积光相关。
9. 计算GBuffer相关内容：
   * 首先是设置GBuffer相关的数据，通过ShadingModelsMaterial.ush中的SetGBufferForShadingModel()方法设置，其中还有一个GBufferDither值，这是计算的噪声值，用于ClearCloth的ShadingModel。
   * 添加Velocity因素到GBuffer，就是通过当前屏幕位置与前一帧屏幕位置差来计算，但是还要考虑到TAA的抖动(ResolvedView.TemporalAAJitter)
10. 计算normal curvature to roughness（官方解释"The ‘Normal to Roughness’ feature can help reduce specular aliasing from detailed normal maps"）相关内容，[Curvature Shader](http://madebyevan.com/shaders/curvature/)。其相关代码：
```cpp
float NormalCurvatureToRoughness(float3 WorldNormal)
{
	float3 dNdx = ddx(WorldNormal);
	float3 dNdy = ddy(WorldNormal);
	float x = dot(dNdx, dNdx);
	float y = dot(dNdy, dNdy);
	float CurvatureApprox = pow(max(x, y), View.NormalCurvatureToRoughnessScaleBias.z);
	return saturate(CurvatureApprox * View.NormalCurvatureToRoughnessScaleBias.x + View.NormalCurvatureToRoughnessScaleBias.y);
}
```
11. PostProcess SubSurface 计算后处理次表面
12. 开始计算光照相关内容，这里需要提及的是，在保存法线向量的时候，有时候只能用两个分量来存，通过对应的Encode和Decode来压缩和获取这里需要涉及一个[Octahedron normal vector encoding](https://knarkowicz.wordpress.com/2014/04/16/octahedron-normal-vector-encoding/)方法
	* 首先是BentNormal相关（环境法线），就是根据周围环境改变原始法线，在ClearCoat的ShaderingModel中会用到上面的Octahedron normal，还有就是通过GetBentNormal()方法来获得，下面是自动生成的一个默认方法:
  
	```cpp
	MaterialFloat GetBentNormal0(FMaterialPixelParameters Parameters)
	{
		MaterialFloat4 Local1 = ProcessMaterialColorTextureLookup(Texture2DSampleBias(Material.Texture2D_0,Material.Texture2D_0Sampler,Parameters.TexCoords[0].xy,View.MaterialTextureMipBias));
		return Local1.a;
	}
	```
    这只是取得一个向量，重要的是还要把曲率(表面弯曲程度)考虑进去，这主要是BassPassPixelShader.usf中的ApplyBentNormal()方法计算。
    * 计算环境光遮蔽，在AOMultiBounce()方法中计算，这里使用的是[Ground Truth Ambient Occlusion (GTAO)](http://iryoku.com/downloads/Practical-Realtime-Strategies-for-Accurate-Indirect-Occlusion.pdf)算法，传入亮度值和之前计算好的AO值，方法见AOMultiBounce()。
    * DiffuseColorForIndirect，在SubSurface,PreIntegrated,Cloth,Hair的ShadingModel中有不同的计算方法，尤其是HairShading，使用了[Marschner](http://www.graphics.stanford.edu/papers/hair/hair-sg03final.pdf)和[Pekelis](https://graphics.pixar.com/library/DataDrivenHairScattering/paper.pdf)论文中的技术。
    * DiffuseIndirectLighting，SubsurfaceIndirectLighting，IndirectIrradiance，通过GetPreComputedIndirectLightingAndSkyLight()方法计算，就是通过预计算的值来计算光照，其预计算光照参数如下定义:
    ```cpp
    BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FIndirectLightingCacheUniformParameters, )
	SHADER_PARAMETER(FVector, IndirectLightingCachePrimitiveAdd) // FCachedVolumeIndirectLightingPolicy
	SHADER_PARAMETER(FVector, IndirectLightingCachePrimitiveScale) // FCachedVolumeIndirectLightingPolicy
	SHADER_PARAMETER(FVector, IndirectLightingCacheMinUV) // FCachedVolumeIndirectLightingPolicy
	SHADER_PARAMETER(FVector, IndirectLightingCacheMaxUV) // FCachedVolumeIndirectLightingPolicy
	SHADER_PARAMETER(FVector4, PointSkyBentNormal) // FCachedPointIndirectLightingPolicy
	SHADER_PARAMETER_EX(float, DirectionalLightShadowing, EShaderPrecisionModifier::Half) // FCachedPointIndirectLightingPolicy
	SHADER_PARAMETER_ARRAY(FVector4, IndirectLightingSHCoefficients0, [3]) // FCachedPointIndirectLightingPolicy
	SHADER_PARAMETER_ARRAY(FVector4, IndirectLightingSHCoefficients1, [3]) // FCachedPointIndirectLightingPolicy
	SHADER_PARAMETER(FVector4,	IndirectLightingSHCoefficients2) // FCachedPointIndirectLightingPolicy
	SHADER_PARAMETER_EX(FVector4, IndirectLightingSHSingleCoefficient, EShaderPrecisionModifier::Half) // FCachedPointIndirectLightingPolicy used in forward Translucent
	SHADER_PARAMETER_TEXTURE(Texture3D, IndirectLightingCacheTexture0) // FCachedVolumeIndirectLightingPolicy
	SHADER_PARAMETER_TEXTURE(Texture3D, IndirectLightingCacheTexture1) // FCachedVolumeIndirectLightingPolicy
	SHADER_PARAMETER_TEXTURE(Texture3D, IndirectLightingCacheTexture2) // FCachedVolumeIndirectLightingPolicy
	SHADER_PARAMETER_SAMPLER(SamplerState, IndirectLightingCacheTextureSampler0) // FCachedVolumeIndirectLightingPolicy
	SHADER_PARAMETER_SAMPLER(SamplerState, IndirectLightingCacheTextureSampler1) // FCachedVolumeIndirectLightingPolicy
	SHADER_PARAMETER_SAMPLER(SamplerState, IndirectLightingCacheTextureSampler2) // FCachedVolumeIndirectLightingPolicy
    END_GLOBAL_SHADER_PARAMETER_STRUCT()
    ```
    *  结合上面计算的结果(DiffuseColorForIndirect, DiffuseIndirectLighting, SubsurfaceIndirectLighting)以及AO来计算DiffuseColor
    * 计算前向渲染相关的光照
    * 计算Fog相关内容，Vertex_Fogging,Pixel_Fogging，还有Volumetric_Fogging(体积雾)
    * 体积光相关
    * 光照Color的叠加计算以及对应Blend模式计算，光照叠加调用LightAccumulator_Add方法来计算
    * 给FPixelShaderOut的对应的MRT[8]赋值
