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
        float d = ( NoH * a2 - NoH ) * NoH + 1; // 2 mad
        return a2 / ( PI*d*d );     // 4 mul, 1 rcp
    }
    ```

## BRDF

### 基本信息

1. 对于入射光的辐射率，可以大致简写为： $L_i(c,-v)$，c是camera的位置，v是视线。光在传播过程中一般会有中间介质，但是现在不考虑这些，所以入射辐射率等于出射辐射率，如下:
 $L_i(c,-v)=L_o(p,v)$， p是视线与最近物体表面交点

2. 本节不考虑透明和全局次表面散射，关注于局部反射现象(local reflectance phenomena)，包含表面反射和局部次表面散射，它只会受入射光 *l* 和出射视线方向 *v* 。而局部反射的量化方式就是BRDF(bidirectional reflectance distribution function)双向反射分布函数，BRDF实际上就是表示反射光和入射光之间的比例关系。

3. 现实世界中物体表面往往不是统一的，不同的区域会有不同的效果，这就提出了基于空间位置的BRDF就是spatially varying BRDF (SVBRDF) 或 spatial BRDF (SBRDF)。可见入射光 *l* 和出射视线方向 *v* 是有两个自由度，所以正常情况四个标量值就能表示，两个角度$\theta$表示两个线相对于法线的角度，两个 $\phi$ 表示相对于平面切线（一般会有个切线）的角度，而Isotropic BRDF是个特例，它只有3个标量，如下图:
 ![image](http://www.realtimerendering.com/figures/RTR4.09.17.png)

### BRDF详细内容

1. **BRDF**，光的入射和反射都会考虑光的波长，光的反射会收到波长的影响，所以有两种模型来表示，第一种是把波长当成参数传入，这在离线渲染中使用较多，另一种是BRDF返回光谱的分布值，这在实时渲染中使用较多，因为实时渲染只需要返回RGB值即可。其对应反射公式如下：

 $L_o(p,v)=\int_{l\in\Omega}f(l,v)L_i(p,l)(n\cdot l)dl$

 $l$不是光源的方向，它是半球体里连续的区域，伴随着辐射度使用$dl$表示它的微分（differential）立体角。上述等式可见出射辐射量等于入射辐射的积分（integral）就是$l$相对于$\Omega$的积分。下面会简化，会把$p$省略掉，所以就剩如下公式：

  $L_o(v)=\int_{l\in\Omega}f(l,v)L_i(l)(n\cdot l)dl$

这个公式的由来和BRDF的定义有关，因为BRDF就是出射光辐射率和入射辐照度的比值，$f(l,v)$就是BRDF对应函数，看如下定义：
$f(l,v)=\frac{dL_o(l)}{dE(l)}$

$E$是辐照度，$E=\frac{d\phi}{dA}$，如果光不是垂直照射，那么面积就会改变，但是光通量不变，有该公式$E=\frac{d\phi}{dA^{\perp}}=\frac{d\phi}{dAcos\theta_i}$ ，$\phi$是辐射通量，$A$是面积，所以辐照度就是单位面积受到的辐射通量。

$L_i(l)$就是辐射率，$L_i(l)=\frac{dE}{dw}=\frac{d\phi}{dwdAcos\theta_i}$，这里的$dw$立体角的微分，也就是前面的$dl$，结合前面的公式可以得知，$dE(l)=L_i(l)cos\theta_id_w$，$(n\cdot l)$就是对应于$cos\theta$。至于为什么是微分辐射率与微分辐照度的比值，因为在现实世界中，反射方向通量通常只占整个反射通量很小部分，如果分母是入射光通量并且立体角趋于0那么这比值也会接近0，如果分母是微分辐照度那么它同样会有个立体角微分，这样会有意义。
在实际的实时渲染中，对于点光源、方向光等理想光源求辐射率不需要积分，只需要把每个光源的计算结果累加起来就行。

2. 而半球体通常会要参数化，通常就是用$\theta$和$\phi$来表示半球，上述公式的$L_o(v)$就可以替换为$L_o(\theta,\phi)$，立体角微分（$dl$）就是$sin\theta_id\theta_id\phi_i$ 那么对应的$n\cdot l$对应的可以改为$cos\theta_i$，那么上述的公式既可以推导为双重积分，如下:

$L_o(\theta_o,\phi_o)=\int_{\phi_i=0}^{2\pi}\int_{\theta_i=0}^{\pi/2}f(\theta_i,\phi_i,\theta_o,\phi_o)L_i(\theta_i,\phi_i)cos\theta_isin\theta_id\theta_id\phi_i$

在有些情况下，参数表示还会发生变化，可能会使用$\mu_i=cos\theta_i$，$\mu_o=cos\theta_o$，这就形成如下的表达式：

$L_o(\mu_o,\phi_o)=\int_{\phi_i=0}^{2\pi}\int_{\mu_i=0}^{1}f(\mu_i,\phi_i,\mu_o,\phi_o)L_i(\mu_i,\phi_i)\mu_id\mu_id\phi_i$

注意上式$sin\theta_id\theta_i$可以换元到$dcos\theta_i$ 。

在物理渲染中，BRDF都需要遵循两个法则，第一个就是，入射角和出射角交换所得的值应该一样：$f(l,v)=f(v,l)$，在实时渲染中一般不会注重这个，一般只是用来判断这个BRDF是否具有物理特性。第二种就是能量守恒，在实时渲染中不会太注重，但是不能相差太多。

3. *directional-hemispherical reflectance* $R(l)$，这个用来计算光的损耗程度，就是根据入射光方向来计算，公式如下：
$R(l)=\int_{l\in\Omega}f(l,v)(n\cdot v)dv$

这里的$v$就相当于之前反射公式的$l$，它代表的是一块区域，不是一个方向。$R(l)$的值在[0,1]区间内，0表示全部吸收，1表示全部反射。最简单的BRDF就是Lambertian光照，它通常会用来计算Diffuse（漫反射），但是在本章节中就主要是次表面散射。如下一个公式：

$f(l,v)=\frac{C_{diff}}{\pi}$

分子就是漫反射Color值，这个$\frac{1}{\pi}$系数是通过对整个半球面积分得出的结果。

### Illumination

1. 本章主要是介绍局部光照相关内容。
2. 在局部光照中计算精准光源（Punctual Light）和平行光（Directional Light）是比较简单的，不需要积分，计算公式如下：
   $L_o(v)=\pi f(l_c,v)C_{light}(n\cdot l)$

其中$C_{light}$就是光的颜色，它是通过光垂直照射到Lambert反射模型（$f_{Lambert}(l,v)$）上获取到的值（Albedo = 1），就像当于一束光垂直照到一张白纸上，这就定义了光源颜色，然后在指定的反射模型$f(l,v)$上计算出结果。而$\pi$就是用来抵消Lambert中的$\frac{1}{\pi}$系数。

### Fresnel Reflectance

正常的平面理想反射，如下图：

![image](http://www.realtimerendering.com/figures/RTR4.09.19.png)

其中反射的方向 $r_i=2(n\cdot l)n$

$n_1为表面上方的折光率，n_2为表面下方的折光率$

1. External Reflection（外部反射）
    当$n_1<n_2$就是外部反射，如光从空气找到水面反射，反射方程定义为$F(\theta_i)$，它的值取决于入射角大小，以及波长。当$\theta_i=0^{\circ}$为0，$\theta_i=90^{\circ}$为1。

   Schlick提出菲涅约等式，公式内容如下：
   $F(n,l)\approx F_0+(1-F_0)(1-(n\cdot l))^5$
   其中$F_0$就是控制菲涅耳反射率的参数，这个公式在UE4中有对应的实现（BRDF.ush文件F_Schlick方法）。

   在实际情况中使用更多的是下面一个约等式：
   $F(n,l)\approx F_0+(F_{90}-F_0)(1-(n\cdot l))^{\frac{1}{p}}$
   可见这个公式有更多的调整空间，有最大值$F_{90}$，不是直接给一个1，这样使范围更精确，同时可以调整锐度，$p$参数。

2. Typical Fresnel Reflectance Values
    物质可以大致分为3种，绝缘体、金属（导体）、半导体
    绝缘体的$F_0$参数都比较偏小，一般不超过0.06，因为当光照射到绝缘体的时候，光都被吸收和散射。而且都是用一个标量表示，因为绝缘体光学特性在可见光谱范围内变化不大，对RGB通道的影响都差不多。

    金属的$F_0$数值都比较大，一般在0.5以上，而且是使用RGB3通道来表示，因为很多金属有colore菲涅尔反射。

    半导体的$F_0$数值在0.1和0.6之间，在实际情况中很少会需要渲染这种材质（0.2-0.45）。

3. Parameterizing Fresnel Values
   菲涅尔涉及一些参数，diffuse color（UE4里的BaseColor，$\rho_{ss}$）、metallic，如果metallic=1，$F_0=\rho_{ss}$。通常还会有一个specular（高光度）来控制非金属表面上的高光量，这应用在UE4中。

4. Internal Reflection（内部反射）
与外部反射相对应，它会有一个临界角度$\theta_c$，当过了这个临界角度，$F_0$的值就会变为1，因为$F_0$最大就是1。它的曲线比Exernal Reflection陡，如下图：
![image](http://www.realtimerendering.com/figures/RTR4.09.24.png)

有一个现象就比较明显的表现出这种情况，就是水下的气泡看起来很有金属光泽，比较亮。

### Microgeometry（微几何）

介绍微表面的一些特性，在实际中一些微表面信息可能要小于一个像素，所以就需要一些形式来表示。物体表面的粗糙度可以用一些统计学方面的东西来表示，roughness就是用来表示这个，在UE4的材质编辑器中就有对应的值。

微观结构的几何效应主要就是三种有Shadowing、Masking、interreflection of light，主要由下图：
![image](http://www.realtimerendering.com/figures/RTR4.09.27.png)

### Microfacet Theory（微表面理论）

很多基于微表面几何研究的BRDF数学模型就是微表面理论（Microfacet Theory），它先后由Blinn和Cook and Torrance提出，所有微表面反射综合就会得到表面的BRDF反射，通常由Speclular $micro-BRDF_s$（用于表面反射），Diffuse  $micro-BRDF_s$（次表面散射），Diffraction  $micro-BRDF_s$（几何和波光学效应）。

微表面理论比较重要的就是表面法线的分布也就是 **normal distribution function(NDF)**，通常用$D(m)$来表示NDF，对整个球面积分的结果是1，其公式如下：

$\int_{m\in\Theta}D(m)(n\cdot m)dm=1$

$\Theta$就是表示整个球体，与之前的半球体不一样，$n$就是当前微表面的法线，$m$就是所需要计算的法线。
同时相对于视角$v$的积分公式：

$\int_{m\in\Theta}D(m)(v\cdot m)dm=v\cdot n$

如下图：

![image](http://www.realtimerendering.com/figures/RTR4.09.31.png)


上面的公式没有考虑不可见的微表面，但是实际上只需要可见微表面，所以就加入*masking function* $G_1(m,v)$，公式如下：

$\int_{m\in\Theta}G_1(m,v)D(m)(v\cdot m)^+ dm=v\cdot n$

注意这里的$(v\cdot m)$是clamp大于0的，$G_1(m,v)D(m)$又被称为 *distribution of visible normals*，就是可见法线分布。

### IBL（基于图片的光照）
    
就是根据环境贴图来计算环境光，环境光一般是一张CubeMap。

IBL同样也是分为Diffuse和Specular，它们需要分开计算，前面知道要计算光照需要对半球体进行积分，在实时渲染中不可能这样计算，所以这就需要蒙特卡洛积分。