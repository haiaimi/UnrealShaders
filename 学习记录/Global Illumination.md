## Global Illumination In UE4
UE4中移动平台的全局光照主要是来自LightMap和ILC（可以理解为三阶球谐），LightMap用在静态物体上，ILC则可以用在移动物体上。IBL也算是GI的一部分，UE中还有类似的Reflection Capture，下面主要区分一下这两个。

### Sky Light和Reflection Capture互斥
目前UE4 Mobile管线里的SkyLight和Reflection是互斥的，其本质上是因为Mobile平台上Reflection Texture的编码方式是RGBM，Alpha通道被用来提升前三个通道的亮度，而在PC的上Alpha通道是被用来作为SkyLight和ReflectionCapture的Mask，所以也就导致其不能共存。如果要修改的话就需要修改Reflection Texture的编码格式，如果只是为了能在离开Capture范围时切换为SkyLight，那么就直接修改一下CPP代码即可，主要就两个地方需要修改：
```cpp
MobileBasePass.cpp

FRHIUniformBuffer* ReflectionUB = GDefaultMobileReflectionCaptureUniformBuffer.GetUniformBufferRHI();
			FPrimitiveSceneInfo* PrimitiveSceneInfo = PrimitiveSceneProxy ? PrimitiveSceneProxy->GetPrimitiveSceneInfo() : nullptr;
			bool bIsInReflectionCapture = true;
			if (PrimitiveSceneInfo && PrimitiveSceneInfo->CachedReflectionCaptureProxy)
			{
                //增加距离的判断
				bIsInReflectionCapture = (PrimitiveSceneProxy->GetBounds().Origin - PrimitiveSceneInfo->CachedReflectionCaptureProxy->Position).SizeSquared() < (PrimitiveSceneInfo->CachedReflectionCaptureProxy->InfluenceRadius * PrimitiveSceneInfo->CachedReflectionCaptureProxy->InfluenceRadius);
			}
			// If no reflection captures are available then attempt to use sky light's texture.
			if (UseSkyReflectionCapture(Scene) || !bIsInReflectionCapture)
			{
				ReflectionUB = Scene->UniformBuffers.MobileSkyReflectionUniformBuffer;
			}
			else
			{
				//FPrimitiveSceneInfo* PrimitiveSceneInfo = PrimitiveSceneProxy ? PrimitiveSceneProxy->GetPrimitiveSceneInfo() : nullptr;
				if (PrimitiveSceneInfo && PrimitiveSceneInfo->CachedReflectionCaptureProxy)
				{
					ReflectionUB = PrimitiveSceneInfo->CachedReflectionCaptureProxy->MobileUniformBuffer;
				}
			}
			ShaderBindings.Add(ReflectionParameter, ReflectionUB);
```

```cpp
void FMobileSceneRenderer::UpdateSkyReflectionUniformBuffer()
{
	FSkyLightSceneProxy* SkyLight = nullptr;
    // 这里原本代码意思是只要场景存在ReflectionCapture就不会走天光，暴力法注了就行
	if (/*Scene->ReflectionSceneData.RegisteredReflectionCapturePositions.Num() == 0
		&& */Scene->SkyLight
		&& Scene->SkyLight->ProcessedTexture
		&& Scene->SkyLight->ProcessedTexture->TextureRHI
		// Don't use skylight reflection if it is a static sky light for keeping coherence with PC.
		/*&& !Scene->SkyLight->bHasStaticLighting*/)
	{
		SkyLight = Scene->SkyLight;
	}

	FMobileReflectionCaptureShaderParameters Parameters;
	SetupMobileSkyReflectionUniformParameters(SkyLight, Parameters);
	Scene->UniformBuffers.MobileSkyReflectionUniformBuffer.UpdateUniformBufferImmediate(Parameters);
}

```

### Sky Light
UE4中的天光就是环境光，作为IBL，其中有Staic、Stationary、Movable三种，Static的天光只会在烘焙的时候保存低频信息供实时渲染使用，Stationary的天光则会保存高光IBL信息。所以static的天光不会有高光IBL的效果。

1. 移动平台上的天光Diffuse则是直接用球谐表示，移动平台上使用的是简化版本**GetSkySHDiffuseSimple**，ShadingModel5则会使用**GetSkySHDiffuse**版本，主要区别就是PC版本取了更多的球谐系数，两者本身都是trick，这里计算得到的SkyLight Diffuse也会累加到间接光强度。
2. 天光Specular是使用一张带MipMap的HDR格式的CubeMap表示（Irradiance的预积分），BRDF部分的积分Lut使用一个简化近似函数代替：
```hlsl
half3 EnvBRDFApprox( half3 SpecularColor, half Roughness, half NoV )
{
	// [ Lazarov 2013, "Getting More Physical in Call of Duty: Black Ops II" ]
	// Adaptation to fit our G term.
	const half4 c0 = { -1, -0.0275, -0.572, 0.022 };
	const half4 c1 = { 1, 0.0425, 1.04, -0.04 };
	half4 r = Roughness * c0 + c1;
	half a004 = min( r.x * r.x, exp2( -9.28 * NoV ) ) * r.x + r.y;
	half2 AB = half2( -1.04, 1.04 ) * a004 + r.zw;

	// Anything less than 2% is physically impossible and is instead considered to be shadowing
	// Note: this is needed for the 'specular' show flag to work, since it uses a SpecularColor of 0
	AB.y *= saturate( 50.0 * SpecularColor.g );

	return SpecularColor * AB.x + AB.y;
}
```
辐照度部分在函数**GetImageBasedReflectionLighting**中，这里面也会计算ReflectionCapture的高光。

3. Static类型的物体在使用LightMap的情况下只会计算天光的Specular项，Diffuse项直接存在LightMap中，地形这类可以直接把lightmap去掉，直接使用Sky Light进行调光，因为地形不使用lightmap也不会有很大的影响。
4. 还有个问题是游戏中所有的物体都是用的是同一个SkyLight，也就是说室内物体也使用了天光的IBL，这会导致人物在室内很亮，所以这里使用Cubemap Normalization来处理，可以很好的保证动态物体的亮度变化，不会显得突兀，运算很简单，就是*IBLSpecular / IBLDiffuse * IndirectDiffuse*，在COD和战神4的技术分享中都有，[CubeMap Normalization](https://ubm-twvideo01.s3.amazonaws.com/o1/vault/gdc2019/presentations/Hobson_Josh_The_Indirect_Lighting.pdf)。
5. 为了更好的效果，还是要单独做室内IBL，这样才可以保证正确的光照效果，室内直接使用一张IBL cubemap效果也会错误，因为不管物体在哪都是采的同一张Cubemap，完全没有空间信息，所以为了更好的效果需要做*Parallax-Corrected*。
