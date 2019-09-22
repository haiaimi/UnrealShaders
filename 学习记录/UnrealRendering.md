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


  $\ slize=log_2^{(z*B+O)}*S = log_2^{\frac{z*(1-O)+NO}{N}}*S=log_2^{\frac{z-(z-N)*O}{N}}*S$
   
当Z的值为N时slize = 0，当Z值为F时slize = GridSizeZ，也就是最大slize数。C++中的计算公式应该就是根据 **slice = log2(z*B + O) * S**推导出来 。

```hlsl
//根据计算着色器的当前DispatchThreadId来计算世界位置
float3 ComputeCellWorldPosition(uint3 GridCoordinate, float3 CellOffset, out float SceneDepth)
{
	float2 VolumeUV = (GridCoordinate.xy + CellOffset.xy) / VolumetricFog.GridSize.xy;  //计算UV坐标
	float2 VolumeNDC = (VolumeUV * 2 - 1) * float2(1, -1);    //转换到-1 - 1

	SceneDepth = ComputeDepthFromZSlice(GridCoordinate.z + CellOffset.z);  //计算深度

	float TileDeviceZ = ConvertToDeviceZ(SceneDepth);  //转换到NDC空间深度
	float4 CenterPosition = mul(float4(VolumeNDC, TileDeviceZ, 1), UnjitteredClipToTranslatedWorld);    //通过逆观察投影矩阵转换到观察空间
	return CenterPosition.xyz / CenterPosition.w - View.PreViewTranslation;     //View.PreViewTranslation 一般都是 -ViewOrigin，所以是-
}
```