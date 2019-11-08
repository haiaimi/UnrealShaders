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
###基本信息
1. 对于入射光的辐射率，可以大致简写为： $L_i(c,-v)$，c是camera的位置，v是视线。光在传播过程中一般会有中间介质，但是现在不考虑这些，所以入射辐射率等于出射辐射率，如下:
 $L_i(c,-v)=L_o(p,v)$， p是视线与最近物体表面交点

2. 本节不考虑透明和全局次表面散射，关注于局部反射现象(local reflectance phenomena)，包含表面反射和局部次表面散射，它只会受入射光 *l* 和出射视线方向 *v* 。而局部反射的量化方式就是BRDF(bidirectional reflectance distribution function)双向反射分布函数，BRDF实际上就是表示反射光和入射光之间的比例关系。

3. 现实世界中物体表面往往不是统一的，不同的区域会有不同的效果，这就提出了基于空间位置的BRDF就是spatially varying BRDF (SVBRDF) 或 spatial BRDF (SBRDF)。可见入射光 *l* 和出射视线方向 *v* 是有两个自由度，所以正常情况四个标量值就能表示，两个角度$\theta$表示两个线相对于法线的角度，两个 $\phi$ 表示相对于平面切线（一般会有个切线）的角度，而Isotropic BRDF是个特例，它只有3个标量，如下图:
 ![image](http://www.realtimerendering.com/figures/RTR4.09.17.png)

###BRDF详细内容
1. **BRDF**，光的入射和反射都会考虑光的波长，光的反射会收到波长的影响，所以有两种模型来表示，第一种是把波长当成参数传入，这在离线渲染中使用较多，另一种是BRDF返回光谱的分布值，这在实时渲染中使用较多，因为实时渲染只需要返回RGB值即可。其对应反射公式如下：

 $L_o(p,v)=\int_{l\in\Omega}f(l,v)L_i(p,l)(n\cdot l)dl$

 $l$不是光源的方向，它是半球体里连续的区域，伴随着辐射度使用$dl$表示它的微分（differential）立体角。上述等式可见出射辐射量等于入射辐射的积分（integral）就是$l$相对于$\Omega$的积分。下面会简化，会把$p$省略掉，所以就剩如下公式：

  $L_o(v)=\int_{l\in\Omega}f(l,v)L_i(l)(n\cdot l)dl$

这个公式的由来和BRDF的定义有关，因为BRDF就是出射光辐射率和入射辐照度的比值，$f(l,v)$就是BRDF对应函数，看如下定义：
$f(l,v)=\frac{dL_o(l)}{dE(l)}$

$E$是辐照度，$E=\frac{d\phi}{dA}$，如果光不是垂直照射，那么面积就会改变，但是光通量不变，有该公式$E=\frac{d\phi}{dA^{\perp}}=\frac{d\phi}{dAcos\theta_i}$ ，$\phi$是辐射通量，$A$是面积，所以辐照度就是单位面积受到的辐射通量。

$L_i(l)$就是辐射率，$L_i(l)=\frac{dE}{dw}=\frac{d\phi}{dwdAcos\theta_i}$，这里的$dw$立体角的微分，也就是前面的$dl$，结合前面的公式可以得知，$dE(l)=L_i(l)cos\theta_id_w$，$(n\cdot l)$就是对应于$cos\theta$。至于为什么是微分辐射率与微分辐照度的比值，因为在现实世界中，反射方向通量通常只占整个反射通量很小部分，如果分母是入射光通量并且立体角趋于0那么这比值也会接近0，如果分母是微分辐照度那么它同样会有个立体角微分，这样会有意义。

2. 而半球体通常会要参数化，通常就是用$\theta$和$\phi$来表示半球，上述公式的$L_o(v)$就可以替换为$L_o(\theta,\phi)$，立体角微分（$dl$）就是$sin\theta_id\theta_id\phi_i$ 那么对应的$n\cdot l$对应的可以改为$cos\theta_i$，那么上述的公式既可以推导为双重积分，如下:

$L_o(\theta_o,\phi_o)=\int_{\phi_i=0}^{2\pi}\int_{\theta_i=0}^{\pi/2}f(\theta_i,\phi_i,\theta_o,\phi_o)L_i(\theta_i,\phi_i)cos\theta_isin\theta_id\theta_id\phi_i$

在有些情况下，参数表示还会发生变化，可能会使用$\mu_i=cos\theta_i$，$\mu_o=cos\theta_o$，这就形成如下的表达式：

$L_o(\mu_o,\phi_o)=\int_{\phi_i=0}^{2\pi}\int_{\mu_i=0}^{1}f(\mu_i,\phi_i,\mu_o,\phi_o)L_i(\mu_i,\phi_i)\mu_id\mu_id\phi_i$

注意上式$sin\theta_id\theta_i$可以换元到$dcos\theta_i$ 。

在物理渲染中，BRDF都需要遵循两个法则，第一个就是，入射角和出射角交换所得的值应该一样：$f(l,v)=f(v,l)$，在实时渲染中一般不会注重这个，一般只是用来判断这个BRDF是否具有物理特性。第二种就是能量守恒，在实时渲染中不会太注重，但是不能相差太多。

3. *directional-hemispherical reflectance* $R(l)$，这个用来计算光的损耗程度，就是根据入射光方向来计算，公式如下：
$R(l)=\int_{l\in\Omega}f(l,v)(n\cdot v)dv$

这里的$v$就相当于之前反射公式的$l$，它代表的是一块区域，不是一个方向。$R(l)$的值在[0,1]区间内，0表示全部吸收，1表示全部反射。最简单的BRDF就是Lambertian光照，它会经常被用到次表面散射中