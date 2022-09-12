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

当然以上全是猜想，后面会在UE里面尝试做一般效果验证。

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
