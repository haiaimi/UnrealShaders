# Single Layer Water
Single Layer Water是UE自带的水面系统，有自己单独的Shading Model，使用Opaque或Mask进行渲染，但是在移动端上性能合效果存在一定的问题，这里需要完善一下。

在MobileBasePass中不支持阴影和局部点光，在ShadingModelContext中也会多出三个变量，如下：
```cpp
struct FMobileShadingModelContext
{
	half Opacity;
	half3 DiffuseColor;
#if NONMETAL
	half SpecularColor;
#else
	half3 SpecularColor;
#endif
	
#if MATERIAL_SHADINGMODEL_CLEAR_COAT
	half ClearCoat;
	half ClearCoatRoughness;
	float NoV;
	half3 SpecPreEnvBrdf;
#elif MATERIAL_SHADINGMODEL_SINGLELAYERWATER
	half BaseMaterialCoverageOverWater;
	half WaterVisibility;
	float3 WaterDiffuseIndirectLuminance;
#endif
};
```
在MobileBasePass中主要有以下几个不同的地方：
1. 天光漫反射部分：
```cpp
    ShadingModelContext.WaterDiffuseIndirectLuminance += SkyDiffuseLighting;
```
2. 单独的水面渲染部分：
```cpp
    const bool CameraIsUnderWater = false;	// Fade out the material contribution over to water contribution according to material opacity.
    const float3 SunIlluminance = ResolvedView.DirectionalLightColor.rgb * PI;			// times PI because it is divided by PI on CPU (=luminance) and we want illuminance here. 
    const float3 WaterDiffuseIndirectIlluminance = ShadingModelContext.WaterDiffuseIndirectLuminance * PI;	// DiffuseIndirectLighting is luminance. So we need to multiply by PI to get illuminance.
    const float3 EnvBrdf = ShadingModelContext.SpecularColor; // SpecularColor is not F0 as in BasePassPixelShader, it is EnvBRDF.
    const uint EyeIndex = 0;

    const float4 NullDistortionParams = 1.0f;
    WaterVolumeLightingOutput WaterLighting = EvaluateWaterVolumeLighting(
        MaterialParameters, PixelMaterialInputs, ResolvedView,
        Shadow, GBuffer.Specular, NullDistortionParams,
        SunIlluminance, WaterDiffuseIndirectIlluminance, EnvBrdf,
        CameraIsUnderWater, ShadingModelContext.WaterVisibility, EyeIndex);

    // Accumulate luminance and occlude the background according to transmittance to view and mean transmittance to lights.
    Color += WaterLighting.Luminance;
    ShadingModelContext.Opacity = 1.0 - ((1.0 - ShadingModelContext.Opacity) * dot(WaterLighting.WaterToSceneToLightTransmittance, float3(1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0)));
```

EvaluateWaterVolumeLighting