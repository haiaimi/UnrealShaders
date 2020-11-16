# 大气渲染

## 基本理论
下面的内容都是推导**单次散射（single scattering）**的渲染模型。如下图：

![image]()

实际上就是对视线路径AB上每一点的光照贡献进行积分。P就是每一个点。

## 推导过程
### P点的光照
从P点出发沿着光照方向与大气边缘的交点为C，C点沿着CP的方向衰减就达到P点的光照得到P的光照强度$I_p$。如下图：

![image]()

对应的公式就是$I_p=I_sT(\overline{CP})$。

$T$是衰减系数（Transmittance），表示某段路径上的光照的衰减程度。

### P点的过程
现在已知$I_p$，$P$经过一次散射后、经路径$PA$其他大气粒子的衰减后，到达人眼的光强$I_{PA}$为：

$I_{PA}=I_{p}S(\lambda,\theta,h)T(\overline{PA})$

其中$\lambda$是波长，$\theta$是光线入射方向和观察方向的夹角。$h$是$P$点的高度。$S$就是散射系数，表示经过这次散射事件有多少光散射到$\theta$方向上，$T$是衰减系数，这和$S$也是有联系的，毕竟后面也要经过一系列散射才能到终点。

1. $S(\lambda,\theta,h)$散射系数
   
   散射系数主要是粒子的大小以及波长有关，大气中主要存在大小两种粒子，小粒子如氧气、氮气之类的比波长还要小，所以散射就受波长影响，大粒子如尘埃，这些比波长大得多也就不受波长影响。

   所以两种粒子的散射计算方式也不同：

   * 小分子使用**Rayleigh散射（Rayleigh Scattering）**，受波长影响。
   * 大粒子使用**Mie散射（Mie Scattering）**，不受波长影响。
    两种方式的区别如下图：

    ![image]()

    $Rayleigh$的散射模型如下：
    $S(\lambda,\theta,h)=\frac{\pi ^2(n^2-1)^2}{2}\frac{\rho(h)}{N}\frac{1}{\lambda ^4}(1+cos\theta ^2)$

    $\lambda$是光的波长，$n$是粒子的折射率，$N$是海平面处的大气密度，$\rho(h)$是高度$h$处的相对大气密度也就是相对于海平面的密度（比值），一般都会使用指数函数进行拟合如下：

    $\rho(h)=exp(-\frac{h}{H})$ H为大气的厚度。

    可以看出$Rayleigh$散射大致和波长的4次幂成反比，波长越小（越靠近紫光），散射越厉害，所以白天天空为蓝色，黄昏天空会变红是因为阳光穿过的大气更厚，所以到达人眼前，大多数蓝光已经被散射到其他地方。光的波长如下：

    ![image]()

2. 衰减系数：$T(PA)$
    上面只是计算了一次衰减，但是实际上传播时是持续衰减。假设$I_0$在经过一次散射后损失$\beta$比例的能量，那么剩余的$I_1$就是：

    $I_1=I_0-\beta I_0=(1-\beta)I_0$

    下面就是经过距离$x$衰减后的结果：
    * 简单情况就是$\beta$为定值，可以直接获得最终的结果：
  
    $I=I_0exp(-\beta x)=I_0T(\overline{PA})$,
    $T(\overline{PA})=exp(-\beta x)$

    * 复杂情况，波长和$\beta$与波长$\lambda$和高度$h$都有关，这时就需要积分：
  
    $I=I_0exp\{-\int_P^A \beta(\lambda,h)ds\}=I_0T(\overline{PA})$,
    $T(\overline{PA})=exp\{-\int_P^A \beta(\lambda,h)ds\}$

    之前讨论的$S(\lambda,\theta,h)$只是某个方向上的散射系数，那么$\beta$就是所有方向上的总散射系数，就是对$S$进行球面积分，如果对$Rayleigh$散射进行球面积分，得到的总散射系数$\beta(\lambda,h)$就是：

    $\beta(\lambda, h)=\frac{8\pi^3(n^2-1)^2}{3}\frac{\rho(h)}{N}\frac{1}{\lambda^4}=\beta(\lambda)\rho(h)$

    有了这个式子就可以改写上面的$T(\overline{PA})$，并且$\beta(\lambda,h)$只和$\lambda$有关，所以可以提到积分外面，如下：

    $T(\overline{PA})=exp\{-\int_P^A \beta(\lambda,h)ds\}=exp\{-\beta(\lambda)\int_P^A\rho(h)ds\}$

    可以进一步改变积分项，积分对应的就是路径长度x，这部分积分可以理解成**光学距离--Optical Depth**：
    $D(\overline{PA})=\int_A^P\rho(h)ds$

    也可以改写$S(\lambda, \theta, h)$为：

    $S(\lambda, \theta, h)=\beta(\lambda,h)P(\theta)=\beta(\lambda)\rho(h)P(\theta)$

    上面的$P(\theta)$表示这些散射的能量中有多少被散射到$\theta$方向上，就是**相位函数（Phase Function）**也被称为$Scattering Geometry$，所以$Rayleigh$散射的相位函数就是：
    $P(\theta)=\frac{3}{16\pi}(1+cos\theta^2)$。

    基于上面散射和衰减的推导，我们可以把$I_{PA}$写成：

    $I_{PA}=I_{P}S(\lambda,\theta,h)T(\overline{{PA}})$

    $=I_ST(\overline{CP})S(\lambda,\theta,h)T(\overline{{PA}})$

    $=I_S\{exp\{-\beta(\lambda)D(\overline{CP})\}\}\{\beta(\lambda)\rho(h)P(\theta)\}\{exp\{-\beta(\lambda)D(\overline{PA})\}\}$

    $=I_S\{\beta(\lambda)\rho(h)P(\theta)\}\{exp\{-\beta(\lambda)\{D(\overline{CP})+D(\overline{PA})\}\}\}$

    计算路径AB上某一点P的光照贡献，大致流程就是如下：
    1. 阳光到达大气层边缘$C$
    2. 经过路径$CP$衰减到$P$点
    3. 在$P$点发生一次散射，一部分光散射到视线方向上
    4. 再经过$AP$路径衰减到眼睛

### 路径AB之间
上面这么多内容只是一个点，但是路径AB上有很多个点这就需要随AB上所有点进行积分，如下图：

![image]()

计算过程如下：

$I_A=\int_A^BI_{PA}ds$

$=\int_A^BI_pS(\lambda,\theta,h)T(\overline{PA})ds$

$=\int_A^BI_ST(\overline{CP})S(\lambda,\theta,h)T(\overline{PA})ds$

$=\int_A^BI_S\{exp\{-\beta(\lambda)D(\overline{CP})\}\}\{\beta(\lambda)\rho(h)P(\theta)\}\{exp\{-\beta(\lambda)D(\overline{PA})\}\}ds$

$=I_S\beta(\lambda)P(\theta)\int_A^Bexp\{-\beta(\lambda)\{D(\overline{CP})+D(\overline{PA})\}\}\rho(h)ds$

上面就是单级散射模型的简单推导，但是实际情况会复杂的多，因为光在到达人眼的时候已经散射了无数次。目前就相当于算了单级的散射。

## 大气渲染模型
上面的内容应用到游戏中就会有两个部分：天空背景渲染也就是SkyBox和大气透视渲染。

**大气透视（aerial perspective）**是渲染场景中物体距离远近的效果，如下图：

![image]()

就是A沿着AB接收到的光照，这是由两部分组成：
* $B$点反射的颜色$R_b$经过AB衰减后的光照
* $AB$路径上由于散射贡献的光照
  
  公式如下：
  $I_A=R_BT_{extinction}+I_{inscatter}$

## UE4中的大气散射实现

其中会用到一些系数：

Extinction Coefficient $\sigma_t$ 湮灭系数

Absorption Coefficient $\sigma_a$ 吸收系数

Scattering Coefficient $\sigma_S$ 散射系数

Phase Function $p$ 相位函数，表示光子在Scattering后朝各个方向出射的概率，单位是$sr^{-1}$，下面$p_r$是Rayleigh散射，$p_m$是Mie散射，$p_u$是各向同性散射的Phase Function。

Transmittance，$T(a,b)$，两个点a,b之间的透光度，通过对路径上的$\sigma_t(h)$进行积分得到，这个会通过预计算得到，公式如下：

$T(a,b)=exp(-\int_a^b\sigma_t(h)ds$

Scattering，$G(x,v)$，在某点$x$上，各个方向的入射光朝着方向$v$出射散射强度之和，如下：

$G_n(x,v)=\int_{\Omega}L_{n-1}(x,-w)pdw$

In-Scattering，$L_{scat}$，在$x$点朝$v$方向到达另一点的散射强度，就是Scattering乘上Transmiitance，如下公式：

$L_{scat}(x,v,c=G(x,v)T(c,x)$

Luminance，L，在路径上对In-Scattering做积分得到的光照强度，如下：

1阶情况：
$L_1(x,v)=\int_{t=0}^{\left\| p-x\right \|}\sigma_s(x)T(x,x-tv)T(x-tv,toSun)p(v,sunDir)E_{sun}dt$

n阶情况：
$L_n^{(x,v)}=\int_{t=0}^{\left\| p-x\right \|}\sigma_s(x)T(x,x-tv)G_n(x-tv,-v)dt$

下标$n$表示第几阶散射。

上面是一些基本理论，在UE4中主要有下面几个部分：

1. Transmittance LUT计算
2. Sky-View LUT
3. Aerial Perspective LUT
4. Multiple Scattering LUT
   
### Transmittance LUT
Transmittance LUT计算入口是*RenderTransmittanceLutCS*，默认尺寸就是256x64，

### Sky-View LUT
计算当前相机位置接收到的各个角度的*Luminance*，根据视线方向做RayMarch，并且是包含所有阶的Scattering。由于靠近地平线部分大气散射会变得高频，所以UV映射不是线性的，所以这个方法需要实时RayMarch。映射曲线如下：

$v=0.5+0.5*sign(l)*\sqrt{\frac{\left| l \right|}{\pi/2}}, l\in[-\frac{\pi}{2},\frac{\pi}{2}].$

上面的$l$就是纬度（latitude）。

### Aerial Perspective LUT
计算相机和远处物体之间的大气散射
1. 分割Camera Frustum，就像Cluster Lighting那样
2. 在Z方向上根据Transmittance累加In-Scattering，每个格子存储的是到Camera的Luminance
3. 该计算会使用到Transmittance LUT和Multiple Scattering LUT

### Multiple Scattering LUT
这里是预计算多级Scattering，也是本文的核心部分，多级计算是十分耗时的部分，一般实时计算就算一级，但是这里使用了一些Trick，做出如下设定:
1. 大于等于2阶的散射，Rayleigh和Mie散射都被视为各向同性散射。
2. 计算某点大于2阶的Scattering时，认为该点周围任意一点的Illuminance是相同的。

以前预计算多级散射会有4维的信息，但是现在各向同性以后就直接少了两个维度。
上面第二点的意思就是在计算某点的Scattering时，空间中任意一点的某一阶的Scattering都视作与该点相同。Multiple Scattering LUT计算结果输出到一张32*32的LUT上，在不同高度和不同光照角度的情况下。

首先要计算$L_{2ndorder}$，就是对PhaseFunction*Luminance做球面积分，下面就是$G_2$，公式如下：

$G_2=\int_{\Omega}L_1(x_s-\omega)p_ud\omega$

$L_1=T(x,p)L_0(p,v)+\int_{t=0}^{\left\| p-x\right \|}\sigma_s(x)T(x,x-tv)S(x,\omega_s)p_uE_Idt$

这里$T(x,p)L_0(p,v)$是地面反射光，$S(x,\omega_s)$是阳光到达x的照度，$E_I$是一个单位值的阳光照度。

在得到$G_2$后还要计算$G_n$，也就是更高阶的散射，这就需要*Transform Function*$f_{ms}$，其对应的公式就是：

$f_{ms}=\int_\Omega L_f(x_s,-\omega)p_ud\omega$

$L_f(x,v)=\int_{t=0}^{\left\|p-x\right\|}\sigma_s(x)T(x,x-tv)1dt$

通过$f_{ms}$就可知$G_{n+1}=G_{n}*f_{ms}$，下面就对$f_{ms}$进行推导：

传统散射计算方法：

$G_n(x,v)=\int_{\Omega}L_{n-1}(x,-\omega)pd\omega$

$L_{n-1}(x,v)=\int_{t=0}^{\left\| p-x\right \|}\sigma_s(x)T(x,x-tv)G_{n-1}(x-tv,-v)dt$

根据前面的简化方式，$G_{n-1}$在任意一点都是固定的数值，$L_{n-1}$可以如下：

$L_{n-1}(x，v)=G_{n-1}\int_{t=0}^{\left\| p-x\right \|}\sigma_s(x)T(x,x-tv)dt$

简化为$L_{n-1}(x，v)=G_{n-1}L_f(x,v)$代到$G_n$中可得：

$G_n(x,v)=\int_{\Omega}G_{n-1}L_f(x,v)pd\omega$，那么两边同时除以$G_{n-1}$，将$p$替换为$p_u$得：

$\frac{G_n}{G_{n-1}}=\int_{\Omega}L_f(x,v)p_ud\omega=f_{ms}$

由于$f_{ms}$小于1，这样就可以得到无穷阶数的$G_n$之和，如下：
$F_{ms}=1+f_{ms}+f_{ms}^2+f_{ms}^3+...=\frac{1}{1-f_{ms}}$

$G_2+G_3+G_4+G_5+G_6+...=G_2*F_{ms}=\Psi_{ms}$.


最终计算任意点所有阶数的Scattering之和，包括一阶和高阶之和，如下：

$G_{all}=\sum_{i=1}^{N_{light}}(T(c,x)S(x,l_i)p(v,l_i)+\Psi_{ms})E_i$

## Radiance To Luminance