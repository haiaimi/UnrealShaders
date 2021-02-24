# Cascaded Shadow Map
级联阴影是用来绘制大场景阴影比较好的方案，大场景中要保证阴影的质量必须要有足够的分辨率，否则就会产生透视走样，但是单纯拉高Shadow Map的分辨率也不现实，因为这对性能和带宽来说都有很大的影响，用CSM可以进行分级，近处使用高分辨率的ShadowMap，远处就比较低（或者说同样的分辨率表现更大的范围），这样也充分的利用ShadowMap。算法流程基本如下：

* 对于每一个光锥体，从光源点绘制场景深度。
* 从视口相机位置渲染场景，基于像素点的深度选择对应级别的ShadowMap。

如下图：

![image]()

## 相关方案

其实解决阴影走样问题还有其他的解决方案，如
Perspective Shadow Maps（PSM），CSM就是PSM的离散形式。PSM的思想就是用光源锥体把整个视锥体包住。就是在当前的相机空间中进行标准的阴影贴图的应用，但是这不能对光源的位置和类型做出改变，所以应用的地方比较少。

还有Light Space Perspective Shadow Maps（LiPSM），用一种方法包裹了相机锥体并且不会改变光源方向。

Trapzoidal Shadow Maps（TSM）构建一个摄像机视锥的梯形包围盒（与前面的LiPSM不一样）。

## 生成Shadow-Map

算法的第一步就是计算相机空间中设置分割的z值，比如一个shadow-map上的像素实际表示长度是$d_s$，那么投射到屏幕上的大小$d_p$就是基于被投影对象的位置和法线，如下公式：

![image]()

$\frac{d_p}{d_s}=n\frac{d_z}{zd_s}\frac{cos{\varphi}}{cos{\theta}}$

这里$d_z$可以理解为1，$cos\varphi,cos\theta$就是光源方向相对于视锥体方向的夹角。

这里n就是视锥体近平面的距离，理论上为了保证在屏幕上相同的效果，在不同级别的下的ShadowMap保证相同的效果，$\frac{d_p}{d_s}$需要是一个常数，此外，我们也可以将余弦因素视为一个常数，所以，

$\frac{d_z}{zd_s}=\rho, \rho=ln(f/n)$

解上述的等式求$z$和离散化，分割数量需要呈指数分布，如下：

$z_i=n(\frac{f}{n})^{i/N}$


$N$就是分割的数量，但是如果按上面的方式分割，还会造成很多Shadow-Map面积浪费，有的区域在光锥体中但是不在视锥体中，但是当N趋于无穷时这个浪费的空间就趋于0，如下图：

![image]()

所以需要对这一块进行改善，如下：

$z_i=\lambda n(f/n)^{1/N}+(1-\lambda)(n+(i/N)(f-n))$

$\lambda$控制着矫正的强度。

在得出分块的z值后，当前视锥块的角点是通过视口大小和屏幕的宽高比得出的。
如下图：

![image]()

同时，将光源的ModelView矩阵$M$设为朝向光源方向，并且设置了通用正交投影矩阵$P=I$。然后将相机平截头体的每个角点投影到光源的Homogeneous空间的$P_h=PM_P$。每个方向上的最小$m_i$和最大$M_i$形成一个包围盒，该包围盒和光源视锥体对齐，从中我们可以确定缩放比例和偏移来使通用光源视锥与之重合。实际上这样可以确保我们在z轴上获得最佳精度，在x，y轴上尽可能的减少损失，这是通过构建矩阵$C$来实现的。最终光源投影矩阵$P$被改成$P=CP_z$，$P_z$矩阵是一个带有远近平面的矩阵。

# CSM In UE4

在UE4中有自带的Cascaded Shadow Map实现，下面主要是关于UE4中的CSM的渲染流程，主要针对的是移动管线。

## 流程

### Dynamic Shadow初始化
准备绘制CSM所需要的内容，主要内容在*FMobileSceneRenderer::InitDynamicShadows*中。

* FShadowProjectionMatrix：阴影投影矩阵

1. 收集受阴影影响的图元，这里就涉及到*FMobileCSMSubjectPrimitives*，该结构体用于存储受阴影投射的图元。
   调用堆栈：
   
    * *FSceneRenderer::InitDynamicShadows* 
        * *FSceneRenderer::AddViewDependentWholeSceneShadowsForView*：为指定光源整个场景的Shadow生成*FProjectedShadowInfos*。
          * *FDirectionalLightSceneProxy::GetShadowSplitBounds*：获取当前等级ShadowMap的Bound
          * *FProjectedShadowInfo::SetupWholeSceneProjection*：设置整个场景的投影信息
            * *FProjectedShadowInfo::UpdateShaderDepthBias*：更新当前的Bias
        * *FSceneRenderer::InitProjectedShadowVisibility*：初始化投影的阴影可见性，有的物体不在范围内就不会投射阴影，这里只是准备所需要的信息，一部分是前面计算好的
        * *FSceneRenderer::GatherShadowPrimitives*：进行视锥剔除，收集需要投射阴影的Primitive，这里使用八叉树收集然后多线程异步执行判断流程，这里面会收集PreShadow和ViewDependentWholeSceneShadow两种，前者就是烘焙阴影后者才是动态阴影
          * *FGatherShadowPrimitivesPacket::FilterPrimitiveForShadows*：多线程中的判断实际运行的函数
        * *FSceneRenderer::AllocateShadowDepthTargets*：为ShadowDepth分配RT