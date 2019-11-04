## Physic of Light
1. 辐照度与振幅的平方是成比例的  $E = ka^2$，a就是振幅

![image](http://www.realtimerendering.com/figures/RTR4.09.03.png)

上图是3个不同场景波长相加下的情况constructive interference, destructive interference（破坏性的）,incoherent addition（不相干）

2. Surfaces，当光照射到一个平面，就需要考虑到折射、反射

3. 常见名词、公式
    + GGX 就是即Trowbridge-Reitz分布其分布公式：
  
    ![image](https://latex.codecogs.com/gif.latex?D_%7BGGX%7D%28m%29%3D%5Cfrac%7Ba%5E2%7D%7B%5Cpi%28n%5Ccdot%20m%29%5E2%28a%5E2-1%29&plus;1%29%5E2%7D)

    在UE4中有相对应的实现：
    ```cpp
    // GGX / Trowbridge-Reitz
    // [Walter et al. 2007, "Microfacet models for refraction through rough surfaces"]
    float D_GGX( float a2, float NoH )
    {
        float d = ( NoH * a2 - NoH ) * NoH + 1;	// 2 mad
        return a2 / ( PI*d*d );					// 4 mul, 1 rcp
    }
    ```

## BRDF

对于入射光的辐射率，可以大致简写为： $L_i(c,-v)$，c是camera的位置，v是视线。

光在传播过程中一般会有中间介质，但是现在不考虑这些，所以入射辐射率等于出射辐射率，如下:
 $L_i(c,-v)=L_o(p,v)$ p是视线与最近物体表面交点

 本节不考虑透明和全局次表面散射，关注于局部反射现象(local reflectance phenomena)，包含表面反射和局部次表面散射，它只会受入射光 *l* 和出射视线方向 *v* 。而局部反射的量化方式就是BRDF(bidirectional reflectance distribution function)双向反射分布函数。

 现实世界中物体表面往往不是统一的，不同的区域会有不同的效果，这就提出了基于空间位置的BRDF就是spatially varying BRDF (SVBRDF) 或 spatial BRDF (SBRDF)。

 可见入射光 *l* 和出射视线方向 *v* 是有两个自由度，所以正常情况四个标量值就能表示，两个角度$\theta$表示两个线相对于法线的角度，两个 $\phi$ 表示相对于平面切线（一般会有个切线）的角度，而Isotropic BRDF是个特例，它只有3个标量，如下图:
 ![image](http://www.realtimerendering.com/figures/RTR4.09.17.png)