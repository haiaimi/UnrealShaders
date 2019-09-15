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