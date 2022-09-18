## Voxel GI
第一眼看到Voxel GI就觉得Voxel GI是很有前途的GI，该方案很巧妙，理论上收敛效果会比其他直接Trace的GI要好，在不借助sdf的情况下，raymarch比普通的raymarch速度更快，可以有比较细腻的indirect light和visibility信息，这一点要比LPV好上不少。SVOGI应该是第一个实时VoxelGI方案，VXGI是Nvidia实际应用的产品。

### 不成熟的想法
#### 想法来源/基础
* 正常来说Voxel GI需要实时体素化，这在PC上都是一个很耗的操作，在移动平台更不可能，所以这里可以考虑可不可以通过其他方式来构建体素，当然可能不会那么精细，UE5里lumen的体素大小大概在80cm左右，这和正常的VXGI的体素大小相差很多，不过UE5的的Voxel主要用来计算稍远处的间接光，近处还是surface cache。但是完全使用Voxel作为间接光来源，效果也还可以，测试结果如下（以下对比是关闭LumenReflection, SSR, LumenScreenTrace）：
    * Voxel+SurfaceCache
        ![image](../RenderPictures/Voxel%20Global%20Illumination/Lumen_VoxelLight%2BSurfacecache.png)

    * Voxel
        ![image](../RenderPictures/Voxel%20Global%20Illumination/Lumen_VoxelLightOnly.png)

    * GI Off
        ![image](../RenderPictures/Voxel%20Global%20Illumination/Lumen_Off.png)

    * 可以看出有SurfaceCache细节比纯Voxel效果要好，但是纯Voxel也还可以（如果在移动平台上有这种效果很可以了），但是这里需要注意的是这里使用Screenspace Probe，效果肯定要比空间稀疏Probe分布要好。

    * Lumen默认的体素精度还是很低的，下图Voxel Visualiztion
        ![image](../RenderPictures/Voxel%20Global%20Illumination/Lumen_VoxelVisualization.png)

* 总的来说就是Lumen的粗糙Voxel可以实现一个还可以的效果，所以应该有尝试空间。

#### 体素化
* 常规的Runtime Voxelization是通过硬件光栅化加保守性光栅完成，Lumen是利用SurfaceCache信息和SDF信息构建体素，这两种方式在移动平台上无疑是不太现实的，所以必须要有另一种方式。
    * Offline Per Mesh Voxel：离线给每个Mesh烘焙Voxel，Runtime再拼接，有个问题是物体缩放、旋转后Voxel的细节可能就不够了，同时也需要占用磁盘空间，大地形的Voxel不好处理，可能只能Runtime生成地形的Voxel。
    * Voxel Based On GBuffer：可以预先对场景均匀分块，每块对应一个Voxel，在渲染GBuffer时向Voxel注入Albedo，Emissive等信息，这个可能会分摊多帧注入，比如利用Temporal 累加，不过缺点也很明显，可能runtime处理更复杂，屏幕相关，意味着屏幕外的信息有可能获取不到（只能取到已经Cache住的Voxel信息）

* 这是受EA的Siggraph [Global Illumination Based on Surfels](https://media.contentapi.ea.com/content/dam/ea/seed/presentations/seed-siggraph21-surfel-gi.pdf)影响产生的想法，不过EA这个是通过GBuffer生成Surfel然后利用Hardware Raytracing计算间接光，而这里是想利用GBuffer生成体素，再进行ConeTracing，利用ConeTracing主要是因为这是除了Hardware Raytracing和SDF Tracinig外效率最高的Tracing方案了（移动平台没有Hardware RayTracing，同时SDF不太好获得）。

#### Shading
VXGI着色是在屏幕空间像素（3x3,5x5...）进行ConeTracing，即使分块，进行ConeTracing也会消耗大量性能，所以这里可不可以根据Voxel自动摆放Probe，作为Radiance Cache，所以每帧只需要对Probe做ConeTracing，比如果一个Probe x10 ConeTracing，同时可以分摊到多帧，同时可以作为Cache做Multi bounce，当然以上还是理论。

#### 整体流程
大概整体流程：
* Voxel GBuffer Injection
* Probe Adaptive Placement
* Probe Voxel Cone Tracing
* Probe->Voxel Multi Bounce
* Pixel IndirectLight Shading

当然以上全是猜想，后面会在UE里面尝试做一版效果验证。

### VXGI
之前只是对VXGI有个大致的了解，这次就详细了解一下Voxel GI的理论基础，Voxelization那块就随意带过，这个在之前做PRT时用到过GPU Voxelization。

实际上渲染到像素上都是Cone（由于是透视投影），并不是一条射线，这也是为什么real-time渲染会有高光锯齿的问题，就是因为采样数不足，所以才利用TAA这类超采样技术来缓解采样数不足产生的artifact，如下图：

![image](../RenderPictures/Voxel%20Global%20Illumination/RayIntegrate.png)

所以对一个像素值来说，它是一条入射光线圆锥区域的积分结果。

体渲染之前也接触过，实际上就是一条光线经过衰减、散射最终到达观察点的过程。这里就需要对这一过程进行拆分，使得每个体素就是表示该区域内的积分，首先体渲染积分表达式：
$$I(D) =I_0e^{-\tau(s_0,D)}+\int_{s_0}^{D}q(s)e^{-\tau(s,D)}ds$$
该表达式就表示辐射亮度$I_0$在$s_0$位置经过衰减，同时经过Emission $q(s)$的累加到$D$点的结果。
$$\tau(s_0,s_1)\int_{s_0}^{s_1}\kappa(t)dt$$
$\tau(s_0,s_1)$ 表示光学深度（Optical Depth），$\kappa$表示吸收系数，这在大气渲染中也会涉及。
定义$T(s_0,s_1)=e^{\tau(s_0,s_1)}$

$$I(D) =I_0T(s_0,D)+\int_{s_0}^{D}q(s)T(s,D)ds$$

那么对于普通渲染，这里创建一个对像素面积和光线距离的双重积分：
$$I(D,P)=\int_{r\in P}\int_{s_0}^Dj(s,r)e^{-\int_{s}^{D}\chi(t,r)^{dt}}dsdr$$

$\chi(x,r)$：光线$r$在$s$点的吸收系数
$j(s,r)$：光线$r$在$s$点的散射系数（内散）

如下图：
![image](../RenderPictures/Voxel%20Global%20Illumination/PreIntegration%20Model_0.png)

P为一个像素块，黄色虚线就表示一条光线，这是在正交投影模式下。

如果要体素化，就需要对这段积分进行分块预积分：
![image](../RenderPictures/Voxel%20Global%20Illumination/PreIntegration%20Model_1.png)

$$\tau(s_1,s_2,r)=\int_{s_1}^{s_2}\chi(s,r)ds$$
令$$Q(s_1,s_2,r)=\int_{s_1}^{s_2}j(s,r)e^{\tau(s,s_2,r)}ds$$

那么目前就可以拆分为多个离散区间$[s_i,s_{i+1}]$的积分：
$$I(D,P)=\int_{r\in P}\sum_{i=0}^{n}(Q(s_i,s_{i+1},r)\cdot e^{-\sum_{j=i+1}^{n}\tau(s_j,s_{j+1},r)})dr$$

基于函数的连续性，可以在整个表达式中将离散加和积分进行交换：
$$I(D,P)=\sum_{i=0}^{n}(\int_{r\in P}Q(s_i,s_{i+1},r)\cdot e^{-\sum_{j=i+1}^{n}\tau(s_j,s_{j+1},r)})dr$$

如果假设每段区间内的积分$Q(s_i,s_{i+1},r)\cdot e^{-\sum_{j=i+1}^{n}\tau(s_j,s_{j+1},r)}$之间是无关的，那么可以得到以下：
$$I(D,P)\approx \sum_{i=0}^{n}((\int_{r\in P}Q(s_i,s_{i+1},r)dr)\cdot (\int_{r\in P}e^{-\sum_{j=i+1}^{n}\tau(s_j,s_{j+1},r)}dr))$$

指数项可以直接拆成乘积：
$$I(D,P)\approx \sum_{i=0}^{n}((\int_{r\in P}Q(s_i,s_{i+1},r)dr)\cdot (\int_{r\in P}\prod_{j=i+1}^{n}e^{-\tau(s_j,s_{j+1},r)}dr))$$

同时积分与乘积位置交换：
$$I(D,P)\approx \sum_{i=0}^{n}((\int_{r\in P}Q(s_i,s_{i+1},r)dr)\cdot \prod_{j=i+1}^{n}(\int_{r\in P}e^{-\tau(s_j,s_{j+1},r)}dr))$$

最终就可以得到散射积分和投射积分两部分：
Scatter $\bar Q$: $$\bar Q(s_1,s_2,P)=\int_{r\in P}Q(s_1,s_2,r)dr$$

Transmission $\bar T$：$$\bar T(s_1,s_2,P)=\int_{r\in P}e^{-\tau(s_1,s_2,r)}dr$$

所以我们体素里就需要这两个数据的预积分，可以简单理解为颜色值和可见性。

根据以上离散信息，我们就可以进行增量计算，定义$\bar Q_i=\bar Q_i(s_i,s_{i+1},P)$和$\bar T_{i}=\bar T_{i}(s_i,s_{i+1},P)$，可得一下组合：
$$\hat I_{i}=\hat T_{i+1}+\hat T_{i+1}\bar Q_i$$

$$\hat T_i=\hat T_{i+1}\bar T_i$$

$\hat I_0$才是最终结果。

以上都是在光线角度定义，但是实际上体素肯定是在世界空间，所以我们就需要在世界空间预计算两个方程$\bar Q$和$\bar T$，

$$\bar Q(p,d,s,l)=\int_{r\in P}Q(s,s+l,r)dr$$
$$\bar T(p,d,s,l)=\int_{r\in P}e^{-\tau(s,s+l,r)}dr$$

$p$:位置
$d$:方向
$s$:面积
$l$:长度

由于体素是正方体结构，所以$s=l^2$，$l$就是体素宽度可以确定，体素为均匀等距分布，所以$p$也可以确定，就剩$d$没有确定。预积分能量值$\bar Q$不需要存储，只要存储对应的着色参数，以便动态计算处$\bar Q$。$d$参数在实际渲染的时候根据方向指定。

在实际渲染中，肯定是按透视投影，所以实际就会有如下：

![image](../RenderPictures/Voxel%20Global%20Illumination/PreIntegration%20Model_2.png)

在实际应用中会有voxel mipmap来表示，方便进行采样。

现在就需要计算前面说到的着色参数，以用来计算$\bar Q$，着色参数主要主要存储4类信息：体素透明度、漫反射材质颜色、法线分布、BRDF分布函数。

* 漫反射 
漫反射就是存储在贴图里的albedo，就是通过普通的贴图采样平均加权即可。

* 法线分布
实际上要存储法线分布函数（NDF）是比较占用内存的，所以这里还是使用平均法线和高斯分布（正态分布）来近似法线分布函数，其方差可以用平均法线模表示$|N|$：
$$\sigma ^2=\frac{1-|N|}{|N|}$$

    * 正态分布定义：
        若随机变量$X$服从一个位置参数为$\mu$、尺度参数为$\sigma$的常态分布，记为：
        $$X \sim N(\mu,\sigma ^2)$$

        其概率密度函数为：
        $$f(x)=\frac{1}{\sigma \sqrt{2\pi}}e^{-\frac{(x-\mu)^2}{2\sigma ^2}}$$

    * 高斯分布的pdf如下图：
    ![image](../RenderPictures/Voxel%20Global%20Illumination/Normal_Distribution_PDF.svg)

### 着色模型
有了上面两个参数以及可见性参数，就可以开始着色步骤。
首先光照方程：
$$L(x,\omega_o)=\int_{S^2}L(x,\omega_i)\rho(\omega_i,\omega_o;n(x))d\omega_i$$

这里$n(x)$是单点的法线，但是实际上我们这里会使用近似法线分布函数表示，同时体素表示的是多个表面位置的集合，不是单个点，假设集合点为$q\in x$：
$$L(x,\omega_o)=\frac{1}{N}\sum_{q\in x}\int_{S^2}L(x,\omega_i)\rho(\omega_i,\omega_o;n(q))d\omega_i$$
$$=\int_{S^2}L(x,\omega_i)(\frac{1}{N}\sum_{q\in x}\rho(\omega_i,\omega_o;n(1)))d\omega_i$$
这里假设位置对光照计算结果没有影响，这是因为对体素来说，法线变化影响才是最大的。

所以定义BRDF分布函数：
$$\rho_v(\omega_i,\omega_o;x)=\frac{1}{N}\sum_{q\in x}\rho(\omega_i,\omega_o;n(q))$$

那么如何高效计算$p_v$，其可以理解为对所有离散位置$p$的平均值计算，表示如下：
$$p_v(\omega_i,\omega_o;\gamma(n))=\int_{S^2}(\rho(\omega_i,\omega_o;n)\gamma(n)dn$$

这其实就是对表面所有法线方向的BRDF积分，$\gamma(n)$又表示体素法线分布函数。所以$\rho_v$就是表示基于像素的BRDF分布函数和法线分布函数的卷积。根据定义平面上的两个标准差为$\sigma$和$\sigma_s$的高斯分布，其卷积是一个标准差更大的高斯分布，为$\sigma_{s'}^2=\sigma^2+\sigma_{s}^2$，那么如果我们使用Diffuse BRDF，$f(x) = \frac{albedo}{\pi}$，我们又已知体素漫反射颜色值，那么就可以很轻松的计算出$\rho_{v}$。

从下图就可以看出，一个体素需要计算多个位置的反射光照结果，所以这也就需要围绕法线分布函数积分计算：

![image](../RenderPictures/Voxel%20Global%20Illumination/VoxelSample.png)
