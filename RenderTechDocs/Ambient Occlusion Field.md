# Ambient Occlusion Fields

## 简介
环境光遮蔽近似可以用如下的函数表示：

$A(x,n)=\frac{1}{\pi}\int_{\Omega}V(x,\omega)\lfloor\omega\cdot n\rfloor d\omega$

$x$：接收点的位置

$n$：接收点位置的法线

$V(x,n)$：可见性函数，当在$\omega$方向没有几何体遮挡就为0，否则为1

本文为了实时计算环境光遮蔽，使用预计算*Ambient Occlusion Fields*，大致思路如下：
* 在计算接受面上的AO时，用半球形（Spherical Cap）代表遮挡物
* Spherical Cap由一个方向和一个固体角表示
* 为了快速确定附近任意位置的Spherical Cap，我们在遮挡体周围预计算固体角和平均遮挡方向
* 同时为了保持低内存需求，这些Fields作为径向函数（Radial functions）存在遮挡体周围的Cubemap中

环境光遮蔽其实就是模糊照明模型的简化版本，上述的可见性函数被距离函数代替，如下：

$W(x,n)=\frac{1}{\pi}\int_{\Omega}\rho(d(x,\omega))\lfloor\omega\cdot n\rfloor d\omega$

上面的$\rho(d,\omega)$就是在$x$点沿着$\omega$方向相交的第一个点的距离。

本文可以认为是一个环境光实时阴影的算法，大多数实时阴影算法都是集中于光源和小区域光照。除了阴影技术，快速计算照度反射到物体也是相关工作，也就是简间接光，如*Neighborhood Transfer*和*
Spherical Radiance Transport Maps(SRTM)*都是为了解决这些问题

## Ambient Occlusion Fields
这里介绍使用半球冒（Spherical Cap）计算AO和为固体角和遮挡平均方向定义Fields

1. Spherical Cap Approximation
   为了快速决定空间中在遮挡体周围任意一点的AO，这里我们使用了Spherical Cap表示遮挡体的可见性。
这里我们定义函数$V_{cap}(x,\omega)$作为可见性函数，因此在每个点$x$都会有对应的Spherical Cap，当$\omega$落在cap内$V_{cap}(x,\omega)$为1否则为0，下面表示环境光遮蔽近似：

$\tilde{A}(x,n)=\frac{1}{\pi}\int_{\Omega}V_{cap}(x,\omega)\lfloor\omega\cdot n\rfloor d\omega$

为了计算上面的积分我们需要构建$V_{cap}(x,\omega)$函数，我们根据对着遮挡体的固体角来决定Spherical Cap的大小，如下：

$\Omega(x):=\int_{\Theta}V_{cap}(x,\omega)d\omega$

$\Theta$表示球面的积分，当点$x$周围每个方向都被挡住$\Omega(x)=4\pi$，当完全不可见时就为0。我们使用遮挡的平均方向作为Cap的方向：
$\Upsilon(x):=normzlize(\int_{\Theta}V_{cap}(x,\omega)\omega d\omega$

2. Storing $\Omega$ and $\Upsilon$ 
目前有了用于评估任意点的环境光遮蔽的向量$\Upsilon(x)$和$\Omega(x)$，这时需要考虑怎么存储到3D的场中，由于我们需要准确表达接触阴影，我们需要在物体周围使用较高的分辨率，在更远的距离使用低分辨率。最好是分辨率和到物体的距离相关，为了能够参数化的表示，这里使用$\omega$和$r$表示从遮挡体中心的方向和距离，这样就可以转化为：$\Omega(x)=\Omega(\omega,r),\Upsilon(x)=\Upsilon(\omega,r)$。

为了充分紧凑的存储$\Omega$和$\Upsilon$，我们假定在给定方向ω的情况下，这些场可预测地表现为径向（Radial）距离的函数。因此，在下文中，我们将两个量的模型构造为r的函数，根据知识对着物体的立体角大致和$r$的平方成反比，所以使用如下的方式表示立体角：

$\Omega(\omega,r)\approx \tilde\Omega(\omega,r)=\frac{1}{a(\omega)r^2+b(\omega)r+c(\omega)}$

为了把上述的模型匹配上数据，必须为每个方向$\omega$确定$a(\omega),b(\omega), c(\omega)$系数。

下面就要找出平均方向$\Upsilon$的模型，当距离物体很远时，平均方向可以认为是到物体中心点的方向，当距离比较近的时候，我们可以用下面的方式建模：

$\Upsilon(\omega,r)\approx\tilde{\Upsilon}(\omega,r)=normalize(C_o(\omega)-r\omega)$

上式中$C_o(\omega)$可以理解为是物体的特征位置（例如遮挡体中主要指向的点）

3. Inside the Convex Hull

遮挡体中心出射的线可能会多次与对象多次相交，这就导致了不连续的$\Upsilon(x)$和$\Omega(x)$，为了使$r$的函数平滑。但是实际上不会进行分段，因为这样会导致运行期查找复杂度提高以及存储更多的数据。取而代之的是我们假设接收的几何体很少进入凸包内部，因此在凸包外部就可以保证数据的准确。在凸包的内部和遮挡体外部，我们通过下面的公式可以通过常量$A_0$映射到AO值$\tilde{A}$：

$t=(\frac{r}{r_0(\omega)})^p$

$\tilde{A}(\omega,r)=t\tilde{A}(\omega,r_0(\omega))+(1-t)A_0$

$r_0$是从对象的中心位置到凸包的径向距离，这个必须为每个方向进行独立保存，参数$p$用来调整阴影向$A_0$映射到中心的速度。

4. Implementation
    这里将预处理和运行期分开描述。
*  Preprocess
    在预计算中我们使用最佳方案来$适应\tilde{\Omega}$和$\tilde{\Upsilon}$到计算的遮挡和方向采样，然后这些参数化的函数就存储到围绕物体的两个CubeMap，存储的内容如下：

    $a(\hat{\omega}),b(\hat{\omega}),c({\hat{\omega}}), for \tilde{\Omega},3$

    $C_o(\hat{\omega}), for \tilde{\Upsilon}, 3$

    $r_0(\hat{\omega}), 1$ distance from center to convex hull

    可以看出总共有7个标量，可以使用Ray Tracing的方式和光栅化的方式来计算这些采样信息，这里使用光栅化的方式来生成，然后从显存回读到内存进行处理，这里可以使用一些优化方式，如使用Stencil-Buffer和bitmasks来优化回读次数。我们集中精力和精度用于计算表面根据$r$对数分布的采样点。我们不会再凸包体内部或者太远的位置进行采样。

    $\varepsilon_{\Omega}(\hat{\omega})=\sum^{N}_{i=1}(\Omega(\hat{\omega},r_i)-\tilde{\Omega}(\hat\omega,r_i))^2$

    上式中$\Omega(\hat{\omega},r_i)$就是采样的遮挡值，$\tilde{\Omega}(\hat\omega,r_i)$就是采样点$(\hat\omega,r_i)$处的近似值，$N$就是采样点的数量。拟合过程就为每个方向$\hat{\omega}$得出$a,b,c$系数。

    为了适应模型平均方向$\tilde{\Upsilon}$，我们最小化采样方向误差：

    $\varepsilon_{\Upsilon}(\hat{\omega})=\sum^{N}_{i=1}(1-\Upsilon(\hat{\omega},r_i)\cdot\Upsilon(\hat\omega,r_i))^2$

    上式中$\Upsilon(\hat{\omega},r_i)$就是平均方向，$\hat\Upsilon(\hat\omega,r_i)$是近似方向。

    尽管AO通常是空间平滑的，但是难免会有高频细节，因此需要注意采样不足的Artifact。我们使用标准方案进行解决，就是在采样前对信号进行low-pass滤波，这可以通过构建高分辨率的Cube-map然后降采样滤波（例如高斯滤波）。尽管分辨率很低的cube-map，例如8x8也可以有很好的效果

* Run-Time
  
    当渲染接收表面时，多项式信息就会从对应的遮挡体的cube-map获取到。为了从方向$\Upsilon$和指向的固体角$\Omega$计算出AO值我们需要进行余弦权重（Spherical Cap）积分。直接进行积分在PS中会十分昂贵。所以我们通过固体角和相对于表面的仰角对查找表参数化。当我们超出拟合范围时，方法$\tilde{\Omega}(\omega,r)$和$\tilde\Upsilon(\omega,r)$就会偏离真实的$\Omega$和$\Upsilon$，解决方案是到达一定的距离时AO值设为0，因为AO只能影响一定范围内，同时也优化了性能。所有的处理流程包括查找、计算最终的AO值都是在Fragment阶段完成，下面一段伪代码就表示在Shader中的处理流程：
    
    **Inupt:**

    $x$: 接收点的位置（相对于场空间）

    $n$: 接收点表面法线

    **Output:**

    $\tilde{A}$: 环境光遮蔽值

    **Constants:**

    $r_{near}$: 衰减近处距离

    $r_{far}$：衰减远处距离

    $A_0$：凸包体内部的固定AO值

    $p$：凸包体内部衰减

    **Textures**

    $T_{O_{cc}}$：遮挡数据Cube-map

    $T_{C_{0}}$：方向数据Cube-map

    $T_{LUT}$：Spherical cap积分查找表


    $r=||x||\newline \omega=normalize(x)\newline if\ r>r_{far}\ then\ dsicard \newline (a,b,c,r_0)=T_{O_{cc}}\newline r_{clamp}=max(r,r_0)\newline\tilde{\Omega}=(ar_{clamp}^2+br_{clamp}+c)^{-1}\newline\tilde{\Upsilon}=normalize(T_{C_0}[\omega]-r_{clamp}\omega)\newline\tilde{A}=T_{LUT}[\tilde{\Omega,n\cdot\tilde{\Upsilon}}]\newline if\ r<r_0\ then\newline t=(r/r_0)^p\newline \tilde{A}=t\tilde{A}+(1-t)A_0\newline endif\newline\tilde{A}=\tilde{A}*smoothstep(r_{far},r_{near},r)\newline return\  \tilde{A}$

* Combine Occluders

    在实际情况中一定要考虑多个环境光遮蔽投射体叠加的情况。理论上，两个表示遮挡体的Spherical Caps可以通过查找表结合，但是由于Spherical Cap在一个任意的位置已经是近似值，不值得做额外的计算。实际上我们只是简单的进行$1-A$的相乘混合，这其实时有理论基础的，现在先考虑两个遮挡体重叠的情况，为了计算$a$，$b$两个遮挡体，需要解算下面的积分：

    $A_{ab}(x,n)=\frac{1}{\pi}\int V_{ab}(x,\omega)\lfloor \omega\cdot n \rfloor d{\omega}$

    $V_{ab}(x,n)$就是$a$，$b$两个对象的可见性函数的结合