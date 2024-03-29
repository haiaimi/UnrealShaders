UE4的渲染线程中会常见RDG，这是指Render Graph，依托于FRDGBuilder类，从AddPass到Execute完成一个渲染流程

# Volumetric Fogging（体积雾）
**以下内容参考自:Raudsepp, Siim. “Siim Raudsepp Volumetric Fog Rendering.” (2018).**

普通的雾如深度雾和高度雾是比较固定的（密度是固定的），不能是动态的（可以通过使用Billboard来解决，也被称作soft particles），并且不能考虑光照的影响，因此需要体积雾。

体积雾需要考虑到光的transmission（透射），absorption（吸收），scattering（散射，同时有Out-scattering,In-scattering），模拟图如下:
![image](https://github.com/haiaimi/PictureRepository/blob/master/PictureRepository/Rendering%20Learning/UnrealRendering_VolumeFog_1.png)

![](https://latex.codecogs.com/gif.latex?L_{incoming}=L_{transmitted}&plus;L_{absorbed}&plus;L_{scattered})

计算到散射值，有一些已有的算法：
* Rayleigh phase function 适用于较低波长

 ![](https://latex.codecogs.com/gif.latex?p(\theta,g)=\frac{3*(1&plus;cos^2(\theta))}{16*\pi})

* Henyey-Greenstein phase function 可以计算更大波长
 
![](https://latex.codecogs.com/gif.latex?p(\theta,g)=\frac{1-g^2}{4\pi*(1&plus;g^2-2*g*cos(\theta))^\frac{3}{2}})

* Cornette-Shanks phase function

![](https://latex.codecogs.com/gif.latex?p(\theta,g)=\frac{3*(1-g^2)*(1&plus;cos^2(\theta))}{2*(2&plus;g^2)*(1&plus;g^2-2*g*cos(\theta))^\frac{3}{2}})

在UE4中有对应的方法：
```cpp
float HenyeyGreensteinPhase(float g, float CosTheta)
{
	g = -g;
	return (1 - g * g) / (4 * PI * pow(1 + g * g - 2 * g * CosTheta, 1.5f));
}

float RaleighPhase(float CosTheta)
{
	return 3.0f * (1.0f + CosTheta * CosTheta) / (16.0f * PI);
}
//这是另外的方法
float SchlickPhase(float k, float CosTheta)
{
	float Inner = (1 + k * CosTheta);
	return (1 - k * k) / (4 * PI * Inner * Inner);
}

```

渲染体积雾的步骤：
1. 噪点采样，Noise Texture可以通过一些算法来生成，然后再进行采样，noise texture的范围是0-1，1表示透光率为0，1表示没有fogging
2. 阴影贴图采样，对阴影贴图进行采样，用来判断volume里的点是否在阴影中
3. 添加Lighting
    计算3个值：
    * extinction，与噪点贴图中对应的值，也就是系数相关
    * scattering，由Cornette-Shranks和Rayleigh的phase function计算得到，上面提到在UE4中有对应的实现方法
    * transmittance，由Deers's law（比尔-朗伯定律）计算得到
4. 对雾进行模糊
5. 混合并渲染到屏幕

在UE4中计算体积雾的大致步骤与上面一致，也使用了RayMarching来计算体积雾，主要流程就是下面几个Shader：
* FVolumetricFogMaterialSetupCS， 初始化计算所需资源
* 绘制VoxelizeFogVolumePrimitives（这个只有在渲染体积材质的时候才会执行，就是材质域为Domain时，而且只能在粒子中使用）它会直接调用DrawDynamicMeshPassPrivate全局方法进行绘制，而不是普通的DispatchDraw方法
* RenderLocalLightForVolumetricFog，计算局部的体积雾光照
* TVolumetricFogLightScatteringCS
* FVolumetricFogFinalIntegrationCS，计算出最终的结果，并输出到对应得RenderTarget，对应于FViewInfo的 VolumetricFogResources.IntegratedLightScattering，这在后面计算最终的雾时会使用到。

在VolumetricFog.usf的shader文件中第一个就是 ComputeDepthFromZSlice()方法，在渲染体积雾的时候，使用的是3维的计算着色器，默认尺寸是4x4x4，可见体积雾是按立体方式渲染，其z向深度是64，定义在GVloumetricFogGridSizeZ中，C++中会计算出GridZParams参数（根据近、远平面），然后给Shader使用，通过ZSlize来计算深度，通过O，B两个参数来计算，注意的是深度分配不是均匀的，如下代码：
```cpp
//code in cpp
FVector GetVolumetricFogGridZParams(float NearPlane, float FarPlane, int32 GridSizeZ)
{
	// S = distribution scale
	// B, O are solved for given the z distances of the first+last slice, and the # of slices.
	//
	// slice = log2(z*B + O) * S

	// Don't spend lots of resolution right in front of the near plane
	double NearOffset = .095 * 100;
	// Space out the slices so they aren't all clustered at the near plane
	double S = GVolumetricFogDepthDistributionScale;

	double N = NearPlane + NearOffset;
	double F = FarPlane;

	double O = (F - N * FMath::Exp2((GridSizeZ - 1) / S)) / (F - N);
	double B = (1 - O) / N;

	return FVector(B, O, S);
}

//code in hlsl
//这个就是根据 slice = log2(z*B + O) * S 公式来计算结果，Z是要获取的结果，这样计算使得越近，slice就越小，越近计算越精细
float ComputeDepthFromZSlice(float ZSlice)
{
	float SliceDepth = (exp2(ZSlice / VolumetricFog.GridZParams.z) - VolumetricFog.GridZParams.y) / VolumetricFog.GridZParams.x;
	return SliceDepth;
}
```

可以根据如下公式推导：
  ![](https://latex.codecogs.com/gif.latex?slize=log_2^{(Z*B&plus;O)}*S=log_2^{\frac{Z*(1-O)&plus;N*O}{N}}*S=log_2^{\frac{Z-(Z-N)*O}{N}}*S)

当Z的值为N时slize = 0，当Z值为F时slize = GridSizeZ，也就是最大slize数。C·++中的计算公式应该就是根据 **slice = log2(z*B + O) * S**推导出来 。

```cpp
//根据计算着色器的当前DispatchThreadId来计算世界位置
float3 ComputeCellWorldPosition(uint3 GridCoordinate, float3 CellOffset, out float SceneDepth
{
	float2 VolumeUV = (GridCoordinate.xy + CellOffset.xy) / VolumetricFog.GridSize.xy;  //计算UV坐标
	float2 VolumeNDC = (VolumeUV * 2 - 1) * float2(1, -1);    //转换到-1 - 1

	SceneDepth = ComputeDepthFromZSlice(GridCoordinate.z + CellOffset.z);  //计算深度

	float TileDeviceZ = ConvertToDeviceZ(SceneDepth);  //转换到NDC空间深度
	float4 CenterPosition = mul(float4(VolumeNDC, TileDeviceZ, 1), UnjitteredClipToTranslatedWorld);    //通过逆观察投影矩阵转换到观察空间
	return CenterPosition.xyz / CenterPosition.w - View.PreViewTranslation;     //View.PreViewTranslation 一般都是 -ViewOrigin，所以是'-'
}
```

## LightScatteringCS

这个ComputeShader是计算光的散射值，主要是以下步骤：

1. 计算Shadow影响因素（ShadowFactor）
	* 主要是围绕ForwardLightData来计算，同样是一个C++中定义的Buffer，在其内容在FDeferredShadingSceneRenderer::ComputeLightGrid中计算，在ComputeDirectionalLightStaticShadowing()方法中就是取得阴影值，可以看到其中使用了PCF阴影采样方法，这在DX11和RealTimeRendering书中都有介绍。光的散射值（结合阴影）通过如下代码计算出：
	```cpp
	LightScattering += DirectionalLightColor
				* (ShadowFactor      //ComputeDirectionalLightStaticShadowing()和ComputeDirectionalLightDynamicShadowing()计算出来的结果，同时还要考虑LightFunction()这只有在材质中定义才会有
				* ForwardLightData.DirectionalLightVolumetricScatteringIntensity           //DirectionalLigh的散射强度
				* PhaseFunction(PhaseG, dot(ForwardLightData.DirectionalLightDirection, -CameraVector))); //前面所提到的散射值计算公式，传入的是到眼睛的方向和光线方向夹角，PhaseG 值C++中有相关注释：
				```cpp
				/** 
				* Controls the scattering phase function - how much incoming light scatters in various directions.
				* A distribution value of 0 scatters equally in all directions, while .9 scatters predominantly in the light direction.  
				* In order to have visible volumetric fog light shafts from the side, the distribution will need to be closer to 0.
				*/
				UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = VolumetricFog, meta=(DisplayName = "Scattering Distribution", UIMin = "-.9", UIMax = ".9"))
				float VolumetricFogScatteringDistribution;
				```
	```
2. 计算SkyLight相关影响因素
   * FTwoBandSHVector RotatedHGZonalHarmonic;  RotatedHGZonalHarmonic.V = float4(1.0f, CameraVector.y, CameraVector.z, CameraVector.x) * float4(1.0f, PhaseG, PhaseG, PhaseG); HG区域谐波
   * 使用 ComputeInscatteringColor(float3 CameraToReceiver, float CameraToReceiverLength) 计算出 高度雾在散射中的颜色
   * 涉及到辐照度(Irradiance)：SkyIrradianceSH，计算出SkyLighting
   * 计算SkyVisibility
   * 计算LightScattering: LightScattering += (SkyVisibility * SkyLightVolumetricScatteringIntensity) * SkyLighting;

3. 计算光照相关的影响因素，体积雾的最直接表现方式就是通过光源如聚光灯
	* 首先获取Light Grid Cell的Index，这个光照格子的划分与前面的体积雾cell划分相似，根据这个Index来获取ForwardLightData.NumCulledLifhtGrid和ForwardLightData.DataStartIndex（就是FCulledLightsGridData结构体），这个会用于下面计算FDeferredLightData
	* 计算Distance Bias，Bias一般用于阴影，这里用这个来防止voxel接近光源时产生锯齿
	* 下面就是通过循环（根据上面的FCulledLightsGridData，会依次计算当前视口可见光源，因为场景中可能会有多个光源）来获取FLocalLightData，值来自ForwardLightData的Uniformuffer中ForwardLocalLightBuffer，然后计算FDeferredLightData。
	* 根据光的类型来计算光的衰减：LightMask，整合光照（就是把相关的辐照度考虑进去计算，根据相应的光照模型来计算）主要就分为CapsuleLight和Rect（只有RectLight类型的计算方法不同）：Lighting，然后计算得出CombineAttenuation = Light * LightMask。
	* 根据LightColor和LightAttenuation以及VolumetricScatteringIntensity来计算最终的LightScattering，公式如下:
	```cpp
	LightScattering += LightColor * (PhaseFunction(PhaseG, dot(L, -CameraVector)) * CombinedAttenuation * VolumetricScatteringIntensity);
	```
4. 考虑超采样（抗锯齿）的影响，需要除以采样数
5. 加入Shadow Point和Spot Light这些提前计算的值，这是在InjectShadowedLocalLightPS中计算，然后渲染到LocalShadowedLightScattering RenderTarget上
6. 进行HDR编码（这里实际上就是直接返回），最终存放到RWLightScattering中，如下代码：
   ```cpp
	float4 MaterialScatteringAndAbsorption = VBufferA[GridCoordinate];
	float Extinction = MaterialScatteringAndAbsorption.w + Luminance(MaterialScatteringAndAbsorption.xyz);
	float3 MaterialEmissive = VBufferB[GridCoordinate].xyz;
	float4 ScatteringAndExtinction = EncodeHDR(float4(LightScattering * MaterialScatteringAndAbsorption.xyz + MaterialEmissive, Extinction));

	if (all(GridCoordinate < VolumetricFog.GridSizeInt))
	{
		ScatteringAndExtinction = MakePositiveFinite(ScatteringAndExtinction);  //判断当前值是否是有限大小
		RWLightScattering[GridCoordinate] = ScatteringAndExtinction;
	}
   ```
   上面所提到的浮点数有限大小，实际上就是运用IEEE浮点数规范来判断，判断指数位的大小，首先把浮点数转化为uint来表示，然后和 0x7F800000 比较，注意该数就是 二进制 0111 1111 1000 0000 0000 0000 0000 0000，可以看到指数位都为1，只要指数为小于它那么就是有限数。
7. 这时候值已经写入到RWLightScattering中，这个是UAV资源，所以其对应的Texture3D值已经改变，因为它们指向的是同一块内存。

## FinalIntegrationCS
1. 光的散射值已经计算出来，就是LightScattering的Texture3D，下面就需要整合起来
2. 该计算着色器是个二维的（z向为1），因为z向是确定值，所以这里是直接迭代计算出不同z深度的值（因为要计算当前深度的光照值也需要前一个深度的值），因为会有累加的属性，AccumulatedLighting（累加的光照值）和AccumulatedTransmittance（累加的穿透率），如下代码：
   ```cpp
    float3 AccumulatedLighting = 0;
	float AccumulatedTransmittance = 1.0f;
	float3 PreviousSliceWorldPosition = View.WorldCameraOrigin;
   //按照深度进行迭代，VolumetricFog.GridSizeInt.z默认为64
   for (uint LayerIndex = 0; LayerIndex < VolumetricFog.GridSizeInt.z; LayerIndex++)
	{
		uint3 LayerCoordinate = uint3(GridCoordinate.xy, LayerIndex);
		float4 ScatteringAndExtinction = DecodeHDR(LightScattering[LayerCoordinate]);

		float3 LayerWorldPosition = ComputeCellWorldPosition(LayerCoordinate, .5f);
		float StepLength = length(LayerWorldPosition - PreviousSliceWorldPosition);
		PreviousSliceWorldPosition = LayerWorldPosition;

		//计算当前深度的穿透率
		float Transmittance = exp(-ScatteringAndExtinction.w * StepLength);

		// See "Physically Based and Unified Volumetric Rendering in Frostbite"
		#define ENERGY_CONSERVING_INTEGRATION 1
		#if ENERGY_CONSERVING_INTEGRATION
			float3 ScatteringIntegratedOverSlice = (ScatteringAndExtinction.rgb - ScatteringAndExtinction.rgb * Transmittance) / max(ScatteringAndExtinction.w, .00001f);
			//累加的光，距离增加，层数越高光叠加的就越多，因为IntegratedLightScattering每个Z分量计算的是当前深度的累加值
			AccumulatedLighting += ScatteringIntegratedOverSlice * AccumulatedTransmittance;
		#else
			AccumulatedLighting += ScatteringAndExtinction.rgb * AccumulatedTransmittance * StepLength;
		#endif
		
		AccumulatedTransmittance *= Transmittance;  //累计穿透率，越来越小，因为随着距离增加，光的穿透率肯定是越来愈小，因为透过的雾越来越大

		RWIntegratedLightScattering[LayerCoordinate] = float4(AccumulatedLighting, AccumulatedTransmittance);
	}
   ```
  IntegratedLightScattering 3D贴图会在FogStruct的Buffer中有引用，在计算HeightFog中会有 float4 CombineVolumetricFog(float4 GlobalFog, float3 VolumeUV) 方法把VolumetricFog与进行HeightFog进行混合。其中还会考虑到 [light shafts](https://docs.unrealengine.com/en-US/Engine/Rendering/LightingAndShadows/LightShafts/index.html) 的影响。这个会渲染到SceneColor的RenderTarget（这个在FSceneRenderTarget）中，此时这个RT存在了已经渲染图元的颜色，然后把雾的颜色与该背景色进行混合，如下代码：
  ```cpp
	bool FDeferredShadingSceneRenderer::RenderFog(FRHICommandListImmediate& RHICmdList, const FLightShaftsOutput& LightShaftsOutput)
	{
		check(RHICmdList.IsOutsideRenderPass());

		if (Scene->ExponentialFogs.Num() > 0 
			// Fog must be done in the base pass for MSAA to work
			&& !IsForwardShadingEnabled(ShaderPlatform))
		{
			//设置RenderTarget 为SceneColor
			FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
			SceneContext.BeginRenderingSceneColor(RHICmdList, ESimpleRenderTargetMode::EExistingColorAndDepth, FExclusiveDepthStencil::DepthRead_StencilWrite, true);

			...
			//执行RenderViewFog
			RenderViewFog(RHICmdList, View, LightShaftsOutput);
			...

			SceneContext.FinishRenderingSceneColor(RHICmdList);
		}
	}

	void FDeferredShadingSceneRenderer::RenderViewFog(FRHICommandList& RHICmdList, const FViewInfo& View, const FLightShaftsOutput& LightShaftsOutput)
	{
		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

		// Set the device viewport for the view.
		RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);
				
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
				
		// disable alpha writes in order to preserve scene depth values on PC，设置混合模式，这里Source使用的是Alpha混合，这是RenderTarget对应的值
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_SourceAlpha>::GetRHI();

		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;
		//设置FogShader参数
		SetFogShaders(RHICmdList, GraphicsPSOInit, Scene, View, ShouldRenderVolumetricFog(), LightShaftsOutput);

		// Draw a quad covering the view.
		// 开始渲染
		RHICmdList.SetStreamSource(0, GScreenSpaceVertexBuffer.VertexBufferRHI, 0);
		RHICmdList.DrawIndexedPrimitive(GTwoTrianglesIndexBuffer.IndexBufferRHI, 0, 0, 4, 0, 2, 1);
	}
  ```

# Light Rendering(光照渲染)
UE4中的光照渲染在之前已经接触过，在渲染体积雾的时候也会用到光照相关内容。

FDeferredLightUniformStruct 就是计算光照时所需要的数据，这些数据又是从FLightSceneInfo中获取,其中有一个值比较特殊，就是SourceTexture，这个texture一般是给RectLight使用，而这个texture一般在游戏线程中设置
TDeferredLightVS，该顶点着色器会针对不同的光源使用不同的几何体，directional light是矩形，point light是球体，spot light是锥形，这些几何体都是动态生成
FDeferredLightPS，该PixelShader就是计算光照内容

FDeferredLightPS主要就是通过GetDynamicLighting()来计算光照结果，主要有以下步骤：
 * 计算当前ShadingModel为coat时的法线，这会通过GBuffer中的CustomData.za分量和当前法线的Octahrdron值计算得出
 * 计算光的衰减（当前光源有Radial属性），不同的光源衰减计算方式也不相同，如下：
  ```cpp
	float GetLocalLightAttenuation(
	float3 WorldPosition, 
	FDeferredLightData LightData, 
	inout float3 ToLight, 
	inout float3 L)
	{
		ToLight = LightData.Position - WorldPosition;
			
		float DistanceSqr = dot( ToLight, ToLight );
		L = ToLight * rsqrt( DistanceSqr );

		float LightMask;
		if (LightData.bInverseSquared)
		{
			LightMask = Square( saturate( 1 - Square( DistanceSqr * Square(LightData.InvRadius) ) ) );
		}
		else
		{
			//计算放射光源的衰减
			LightMask = RadialAttenuation(ToLight * LightData.InvRadius, LightData.FalloffExponent);
		}

		if (LightData.bSpotLight)
		{
			//聚光灯的衰减
			LightMask *= SpotAttenuation(L, -LightData.Direction, LightData.SpotAngles);
		}

		//Rect光源的衰减
		if( LightData.bRectLight )
		{
			// Rect normal points away from point
			LightMask = dot( LightData.Direction, L ) < 0 ? 0 : LightMask;
		}

		return LightMask;
	}
  ```
  * 计算阴影相关，如果是RadialLight主要就是计算StaticShadowing，非RadialLight就要考虑DynamicShadow。同时还要计算 [ContactShadow](https://docs.unrealengine.com/en-US/Engine/Rendering/LightingAndShadows/ContactShadows/index.html) 相关，然后代入渲染方程中计算IntegratedBxDF()方法，RectLight与其他光源计算的方式也不相同，同时在函数里会调用不同ShaderModel对应的计算方法，如下：
  ```cpp
  FDirectLighting IntegrateBxDF( FGBufferData GBuffer, half3 N, half3 V, half3 L, float Falloff, float NoL, FAreaLight AreaLight, FShadowTerms Shadow )
{
	switch( GBuffer.ShadingModelID )
	{
		case SHADINGMODELID_DEFAULT_LIT:
			return DefaultLitBxDF( GBuffer, N, V, L, Falloff, NoL, AreaLight, Shadow );
		case SHADINGMODELID_SUBSURFACE:
			return SubsurfaceBxDF( GBuffer, N, V, L, Falloff, NoL, AreaLight, Shadow );
		case SHADINGMODELID_PREINTEGRATED_SKIN:
			return PreintegratedSkinBxDF( GBuffer, N, V, L, Falloff, NoL, AreaLight, Shadow );
		case SHADINGMODELID_CLEAR_COAT:
			return ClearCoatBxDF( GBuffer, N, V, L, Falloff, NoL, AreaLight, Shadow );
		case SHADINGMODELID_SUBSURFACE_PROFILE:
			return SubsurfaceProfileBxDF( GBuffer, N, V, L, Falloff, NoL, AreaLight, Shadow );
		case SHADINGMODELID_TWOSIDED_FOLIAGE:
			return TwoSidedBxDF( GBuffer, N, V, L, Falloff, NoL, AreaLight, Shadow );
		case SHADINGMODELID_HAIR:
			return HairBxDF( GBuffer, N, V, L, Falloff, NoL, AreaLight, Shadow );
		case SHADINGMODELID_CLOTH:
			return ClothBxDF( GBuffer, N, V, L, Falloff, NoL, AreaLight, Shadow );
		case SHADINGMODELID_EYE:
			return EyeBxDF( GBuffer, N, V, L, Falloff, NoL, AreaLight, Shadow );
		default:
			return (FDirectLighting)0;
	}
}
  ```
   * 最后就是光的累加，LightAccumulator_GetResult()

渲染光照的时候，所对应的RenderTarget也是SceneColor，混合模式是TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One>，RGB和Alpha都是直接相加

# Occlusion Cull（遮挡剔除）

UE4的遮挡剔除执行的比较靠前，在InitView的时候执行，主要是在FSceneRenderer::ComputeViewVisibility中执行。剔除时有两种方法，分为软剔除（也就是通过CPU运算）和硬件剔除（正常使用HZBOcclusion）。

## HZB原理

遮挡检测主要有三种类型：
	1. Object-Space：基于空间，如Ray Cast
	2. Image-Space：图片空间，基于像素的检测，一般是由硬件支持，但是不支持透明物体检测
	3. temporal coherence：时间关联性，一般就是使用上一帧的内容进行检测
上面3中方式各有优点，HZB就是把这三种方式结合起来。

* HZB在空间上使用Octree（八叉树），使用Octree可以剔除掉大部分不需要渲染的图元，八叉树会通过递归与视锥体进行相交检测，从而筛选出需要渲染的块（就是八叉树中分出的块）
* 结合Image-Space，HZB会使用深度信息，所以需要绘制一个含有深度信息的MipMap，根据图元所占屏幕区域大小来选择对应level的mipmap进行采样，区域越大所需mipmap等级越低，因为这样会使采样次数固定（UE4中是4x4）的情况下结果更加精确
* 在时间关联性上，HZB会使用前一帧可见图元的数据，当前帧检测完还会更新这些数据

UE4 HZB Shader代码如下：
```cpp
	// 4x4 samples ，进行16次采样
	float2 Scale = HZBUvFactor.xy * ( Rect.zw - Rect.xy ) / 3; //Rect的scale，相对于HZBTexture，因为在构建HZBTexture尺寸缩放过
	float2 Bias = HZBUvFactor.xy * Rect.xy;  //基础UV坐标

	float4 MinDepth = 1;
	UNROLL for( int i = 0; i < 4; i++ )
	{
		// TODO could vectorize this
		float4 Depth;
		Depth.x = HZBTexture.SampleLevel( HZBSampler, float2( i, 0 ) * Scale + Bias, Level ).r;
		Depth.y = HZBTexture.SampleLevel( HZBSampler, float2( i, 1 ) * Scale + Bias, Level ).r;
		Depth.z = HZBTexture.SampleLevel( HZBSampler, float2( i, 2 ) * Scale + Bias, Level ).r;
		Depth.w = HZBTexture.SampleLevel( HZBSampler, float2( i, 3 ) * Scale + Bias, Level ).r;
		MinDepth = min( MinDepth, Depth );
	}
	//筛选最小的深度
	MinDepth.x = min( min(MinDepth.x, MinDepth.y), min(MinDepth.z, MinDepth.w) );

	// Inverted Z buffer，最小深度大于该图元最大深度，那么它必定被遮挡
	OutColor = RectMax.z >= MinDepth.x ? 1 : 0;
```


首先看一下硬件剔除，硬件剔除分为两个步骤，要有准备阶段和检测阶段，UE4中遮挡检测使用的是前一帧计算的内容。

遮挡检测在 FSceneRenderer::ComputeViewVisibility() 方法中这个方法在之前也见到过，在执行完这个步骤后才会网渲染管线中加入需要渲染的模型，当然这个方法不仅包含了遮挡剔除的操作，还有其他的剔除，主要有如下：

	1. FrustumCull，截锥体剔除
	2. Hidden Primitives，指定隐藏的prim，就是在游戏线程中设置Hidden属性
	3. ShowOnlyPrimitives，指定渲染的prims，其他一概不渲染
	4. 在Wireframe模式下，没有遮挡剔除剔除
	5. 遮挡剔除，比较复杂的一部分，主要实现就是在OcclusionCull中


Occlusion准备阶段，在Render方法里：
* RenderOcclusion，在不使用HZB的情况，使用FOcclusionQueryVS，FOcclusionQueryPS
	a. 这个PixelShader很简单，就是直接往RT（注意这个RenderTarget一般是SceneDepth，SceneDepth在PrePass也就是EarlyZ中已经计算好）上渲染固定值 float4(1, 0, 0, 1)因为这里不需要这个值，只需要VS里输出的深度值，就是把包围盒绘制在RT上。
	b. 在渲染这些包围盒的时候就已经比较了，这就考虑到图形API的Query特性，在D3D11中有 [ID3D11Query](https://s0docs0microsoft0com.icopy.site/en-us/windows/win32/api/d3d11/nn-d3d11-id3d11query) 接口，以及其对应的Qury类型 [D3D11_QUERY](https://s0docs0microsoft0com.icopy.site/zh-cn/windows/win32/api/d3d11/ne-d3d11-d3d11_query)， 在UE4中也有对应的枚举，如下：
	```cpp
	enum ERenderQueryType
	{
		// e.g. WaitForFrameEventCompletion()
		RQT_Undefined,
		// Result is the number of samples that are not culled (divide by MSAACount to get pixels)
		RQT_Occlusion,
		// Result is current time in micro seconds = 1/1000 ms = 1/1000000 sec (not a duration).
		RQT_AbsoluteTime,
	};
	```
	显然这里使用的就是RQT_Occlusion，这在MSDN上有对应的文档 *获取在ID3D11DeviceContext :: Begin和ID3D11DeviceContext :: End之间通过深度和模具测试的样本数. ID3D11DeviceContext :: GetData返回一个UINT64. 如果禁用了深度或模板测试，则这些测试中的每一个都将计为通过.*
	UE4中对应的渲染代码：
	```cpp
	// Draw the batches.
	// BatchOcclusionQueries里有多个batch需要渲染，一个batch有多个primitive
	for(int32 BatchIndex = 0, NumBatches = BatchOcclusionQueries.Num();BatchIndex < NumBatches;BatchIndex++)
	{
		FOcclusionBatch& Batch = BatchOcclusionQueries[BatchIndex];
		FRenderQueryRHIParamRef BatchOcclusionQuery = Batch.Query;
		FVertexBufferRHIParamRef VertexBufferRHI = Batch.VertexAllocation.VertexBuffer->VertexBufferRHI;
		uint32 VertexBufferOffset = Batch.VertexAllocation.VertexOffset;
		const int32 NumPrimitivesThisBatch = (BatchIndex != (NumBatches-1)) ? MaxBatchedPrimitives : NumBatchedPrimitives;
			
		RHICmdList.BeginRenderQuery(BatchOcclusionQuery);
		RHICmdList.SetStreamSource(0, VertexBufferRHI, VertexBufferOffset);
		RHICmdList.DrawIndexedPrimitive(
			IndexBufferRHI,
			/*BaseVertexIndex=*/ 0,
			/*MinIndex=*/ 0,
			/*NumVertices=*/ 8 * NumPrimitivesThisBatch,
			/*StartIndex=*/ 0,
			/*NumPrimitives=*/ 12 * NumPrimitivesThisBatch,
			/*NumInstances=*/ 1
			);
		RHICmdList.EndRenderQuery(BatchOcclusionQuery);
	}
	```
	在渲染结束的时候已经计算出比较结果了
* RenderHzb，是用来渲染HZB所需要的Texture，分为BuildHZB和Submit，HZB也属于硬件剔除，主要就是FHZBOcclusionTester，下面分别来看这两个方法
	1. BuildHZB，就是构建HZB(Hierarchical Z-Buffering)
		a. 根据视口大小来确定MipMap的大小以及数目
		b. 对ScenceDepth进行相对应的采样来获取对应的深度，注意这个MipMap都是对上一级的结果再采样，在FHZBuildPS对应的Shader中
	2. Submit，要利用上面计算的MipMap
		a. 填充两个float4的Texture资源，BoundsCenterTexture和BoundsExtentTexture，这两个资源代表了需要遮挡检测图元的包围盒（在HZB方法中则是使用 HZBOcclusionTests.AddBounds() 添加primitive的包围盒）
		b. 在FHZBTestPS中进行数据的计算，其计算结果可以供后面查询使用，这里就牵扯到HZB算法
		c. 把计算好的数据从GPU转到CPU(FHZBOcclusionTester::ResultsTextureCPU)中，之后会通过FHZBOcclusionTester::MapResults()方法来把数据映射到FHZBOcclusionTester::ResultsBuffer(uint8*类型)上
		d. 调用HZBOcclusionTester::IsVisible()方法就可以判断是否可见

OcclusionCull的大致流程：

	1. 判断当前是否支持HZB，OpenGl和Switch平台不支持，UE4默认关闭
	2. 使用PreomputedVisibilityData进行第一轮剔除
	3. 判断使用软剔除还是硬件剔除
      a. 软剔除
      b. 硬件剔除，只支持FeatureLevel >= ES_3_1，硬件剔除也分两种，其中一种是HZB，还有就是正常的Occlusion
	  c. 调用FetchVisibilityForPrimitives，其中本质上还是调用FetchVisibilityForPrimitives_Range，这个方法除了检测遮挡还会提前填充检测用的OcclusionBounds，在FViewInfo中会有下面两个成员变量用于非HZB的OcclusionCull渲染，如下：
	  ```cpp
	  	FOcclusionQueryBatcher IndividualOcclusionQueries;
		FOcclusionQueryBatcher GroupedOcclusionQueries;
	  ```
	  这个类会存储多个待检测的batch，通过BatchPrimitive方法加入，Flush来绘制。

上面主要都是GPU硬件剔除，UE4支持软件剔除，使用的是Potential Visible Set(PVS)，这些数据需要提前烘焙，消耗的时间比较多

# Atmosphere World In Red Dead Demption2
下面看一下荒野大镖客2中的大气渲染，体积雾同样是固定的部分，如下：

基于物理的体积雾渲染：        
$Scattered Light: SL(x)=\sum_{i}^{|light|}P(g,\theta _i)V(i,x)L(i,x)$   
$Transmittance (Beer's Law): T(a,b)=e^{-\int _{x=a}^b\sigma_{ext}(x)dx}$       
$Extinction: \sigma_{ext}(x)=\sigma_{abs}(x)+\sigma_{scat}(x)$   

$Integration: Li(a,b)=Li(b)T(a,b)+\int _{x=a}^bSL(x)\sigma_{scat}(x)T(a,x)dx$

越多的粒子在In-scattering light path中，就会有越多的光分散（out scatter）。每个粒子可以认为是所有经过V方法和P方法调制过的光强度之和。

*Phase Function*就是解释散射光的方向性的概率函数。

*Transmittance*可以被解释为指数型的衰减方法。并且解释了在体积媒介中指定的path中传送了多少光。


前期工作：  
* Analytical Models
* Raymarching Fog
* Volumetric Fog
* PBR Froxels
* RayMarched cloudscapes
* Atmospheric scattering models

## Data Model
### Cloud
#### Cloud Map
云的分布主要是通过生成一张从上向下的CloudMap覆盖游戏世界。通过混合mask和noise纹理并且结合天气和时间来生成有一个有两层云密度的2通道贴图，这里是*512x512 R16G16 Texture*。

#### Cloud Height Lut
Cloud Map是二维分布，这里的Lut是基于海拔分布（altitude-based）包括积云和层云（cumulus and stratus）信息，这张贴图的。通过从一张包含高度梯度的查找表中采样来生成积云和层云，在指定的Pass中我们从*Cloud Lut atlas*中复制对应的天气片段到一个单独的长条纹理（*1x128 RGBA Texture*）中，这允许我们混合LUTs到天气变换中。

#### Cloud Detail
我们从*Cloud Map*和*LUT*定义的Cloud Shape中雕刻出额外的细节。这需要从一个2D的Displacement Map中在xy和xz采样两次，并且生成一个3D Vector用来沿着风速方向给3D noise采样进行偏移。如下一段伪代码：
```cpp
float rescale(vMin, vMax, v)
{
	return saturate((v - vMin) / (vMax - vMin));
}

float2 c = SampleCloudMap(ray.p);
float3 cloudLut = SampleCloudLut(altitude);
float density = smoothstep(g_CloudShape.xz + cloudLut.xy, g_CloudShape.yw + cloudLut.xy, c.xy);
density = rescale(noise, cloutLut.z, density);
```

雕刻Cloud Detail利用rescale方法，也被称为*linstep*或者*inverse lerp*，LUT的z通道控制着噪声的柔和度。

### Rain Fog
#### Fog Map
局部雾通过查找一张Fog Map，和Cloud Map相似覆盖游戏世界，这张Map包含三个通道（包含开始高度，下落距离和密度，*512x512x512 R11G11B10*）。通过混合噪声贴图，天气和游戏时间参数合成，所以它支持水体上雾的消散效果。局部雾可以和全局高度雾结合起来。

#### Fog Volumes
另外一种局部雾通过体积雾来实现，可以附着在粒子和载具，可以是球形和方盒形，并且支持Additive和aplha混合。

## Rendering
* Scattered Light
* Frustum Volumes
* RayMarch
* Performance

在近距离使用一个视锥体进行采样位置，在远处使用RayMarch。

## Shading Model
* 在每个点上应用的着色模型是着重于单个散射行为和波长无关的投射率。
* 多重散射通过应用两条八度的直射光（two octave of direct light）
* 具体的计算方法就是前面提到的*Scattered Light*方法
* *Visibiliy V*是通过阴影贴图的查找和二次*Ray T*

## Phase Function
这里的Phase Function考虑美术的便捷性和性能，使用*Henyey-GreenStein*