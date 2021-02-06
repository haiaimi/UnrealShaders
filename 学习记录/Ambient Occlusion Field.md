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

    可以看出总共有7个标量，可以使用Ray Tracing的方式和光栅化的方式来计算这些采样信息。

* Run-Time