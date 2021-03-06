# Interactive Water
交互水体渲染，游戏中交互水体一般是通过波动方程来计算高度场，从而模拟水体的变化。

# Real-time Interactive Water Waves -DICE

## Background
比较好的流体模拟效果是使用N-S等式模式你不可压缩流体和使用SPH（Smoothed-Particle-Hydrodynamics）方法，但是这两种方法都需要消耗很大的性能，要想实时运算还需要限制到一定的尺寸。所以需要一种简化方法。

### The Height Field Model
简化流体模拟的常见方案就是仅模拟流体表面，通过高度场。本文主要专注于**线性波理论**（Linear Wave Theory）和**浅水方程**（Shallow Water Equation）。

#### Linear Wave Theory
线性波理论就是把波理解为对水体表面的扰动，不会对水流产生影响，粘度为0。同时也不能表示尖波或者河流。下面的表达式为水流速度：

$c=\sqrt{ \frac{g}{\mid k\mid}tan h(kH)}$

上式中$g$是重力加速度，$k$是angular wavenumber，$H$是水体深度。但是对浅水和深水来说，可以分别简化为：

$c=\sqrt{gh},\lambda>>H$

$c=\sqrt{\frac{g}{\mid k\mid}},\lambda<<H$

上面第二个式子就可以用于深水模拟例如海水，但是我们更关心浅水。

#### The Shallow Water Equations
浅水方程就是一个近似的结果，最重要的假设就是水面高度是小明显于波长的，纵向的水流也是恒定的。因为浅水方程只是正确渲染大水波，小波相对于高度来水有比较小的波长，这样就与假设冲突。这也和当前建模的线性波理论相反。同时在浅水区可以进行波形锐化，如下等式：

$\frac{dV}{dt}+g\nabla h+(v\cdot\nabla)v=0$

$\frac{dh}{dt}+(h+b)\nabla\cdot v=0$

上式中$v$是横向水流$g$还是重力加速度，$h$是相对于$h_0$水平线的高度。值得一提的是，如果删除对流项$(v\cdot\nabla)v$，可以假设为从浅水等式简化为线性波理论。

#### Turbulence
无论是线性波还是浅水等式，扰动在真实模拟中起着很大的作用。比如人在水面上走动就会产生扰动。需要注意的是不解N-S方程是不能精确模拟扰动的，所以就需要一些特殊方法。

### Related Work

#### Height Field Methods
传统模拟水面是使用傅里叶变换，但是这种方法很难和交互结合起来，因为这都是被当作环境波形。另一种方法就是使用格子和有限差分来解算简化的*Shallow Water*，水流被当作0。这里流体模拟也是使用半拉格朗日方法，就是和之前的流体模拟相似，用格子来表示流体，由于使用离散方法，也很容易消散。还有就是使用卷积来进行。还有一种是粒子系统，使用2D的SPH来模拟流体。还有*Wave Particle*，就是把每个粒子当作水体表面的扰动，然后再使用这些解算简单的波动方程，一个很大的优点是仿真集中到需要的地方，不需要的地方不需要计算。但是在扰动点多了后就会消耗很多性能。

#### Control and Detail
只是单纯的提高分辨率和粒子数量会加大性能消耗。因此在向上采样并向低分辨率添加细节就有很多工作。不带扰动的向上采样很容易。可以添加噪声或者在高分辨率模拟扰动生成。

#### Interaction
主要有两种实现交互的方案，第一种是使用刚体模拟，在移动对象的前面增加高度，在后面就降低高度。另一种方法就是迭代计算并应用压力。

## OverView
### Variable Wave Speed
在前一章中了解到Wave Speed独立于Wave length。线性波理论提供十分简单的水面理解。但是尽管已经十分简单，还是需要捕捉扩散属性。在线性波中波速直到波长和波高大致相似的时候才停止增加，然后保持常量。水面可以用格子表示，使用常量速度可以很容易的表示出波形，如下等式：

$\frac{\partial^2h}{\partial t^2}=c^2\nabla^2h$

上式中$c^2\nabla ^2$可以用卷积$L$表示，如下：
$\frac{\partial ^2h}{\partial t^2}=L*h$

然而考虑到扩散关系这个方法不是实时运算的最佳方法。卷积&L&的大小取决于可能最大的波长，如果表示1m宽的水体，每个格子5
cm，那么卷积的大小就是20x20，每个像素有400次计算，压力很大。

这里的方法是：$h$被划分到不同的部分$(h_1,h_2,h_3,...,h_k)$代表不同范围的波长。$h_1$代表波长为$\lambda$，$h_2$就是$\lambda$到$\frac{\lambda}{2}$，以此类推。由于高度$h_i$可以在半分辨率下模拟$h_{i+1}$，所以全高度通过插值和对所有可能的$h_i$求和来获得。这个方法的最大优势就是格子里所有的波长都小于格子宽度，因为卷积$L$可以粗略到更小的尺寸。通过划分一些子网格会增加格子计算量，但是每个格子的计算量会小很多。

## Theory and Techniques

### Dispersion as Convolution
我们的推导基于线性波理论，所以会使用一些不同的变量：对h进行时间的导数，对h使用速度势能：

$\frac{\partial ^2h}{\partial t^2}=c^2\nabla^2h$

这种差分等式可以在离散化下使用欧拉步进解算。但是这需要保证$c$恒定，因为所有的波长都是以相同的方式处理。从前面内容可以了解到波速和水面到水底的高度$H$和角波数$k$相关，如下关系：

$c^2=\frac{g}{\mid k\mid}tanh\frac{\mid k\mid}{H}$

但是上式不能直接积分，因为这依赖于$k$。但是通过转换成傅里叶可以直接使用。

$F(\frac{\partial ^2h}{\partial t^2})=c^2(iK)^2F(h)$

$F(\frac{\partial ^2h}{\partial t^2})=-\frac{g}{\mid k\mid}tanh\frac{\mid k\mid}{H}\mid k\mid^2 F(h)$

可以应用傅里叶逆变换来获得最终结果，如下：

$\frac{\partial ^2h}{\partial t^2}=L*h$

$L=F^{-1}(-g\mid k\mid tanh\frac{\mid k\mid}{H})$

通过计算L可以得出最终的数值解。

### Laplacian Pyramids

# Fast Interactive Water Simulate

下面是波动方程：
$\frac{\partial^2z}{\partial t^2}=c^2\nabla^2z$

PDE离散后：
$\frac{\partial ^2z}{\partial t^2}=c^2(\frac{\partial ^2z}{\partial x^2}+\frac{\partial ^2z}{\partial y^2})$

要对每个点求解通常要进行大量计算，可能会用到FFT来求解。

这里使用中心差分法来近似PDE，如下公式：

$\frac{z_{i,j}^{n+1}-2z_{i,j}^n+z_{i,j}^{n-1}}{\Delta t^2}=c^2(\frac{z_{i+1,j}^n+z_{i-1,j}^n+z_{i,j+1}^n+z_{i,j-1}^n-4z_{i,j}^n}{h^2})$

$z_{i,j}^{n+1}=\frac{c^2\Delta t^2}{h^2}(z_{i+1}^n+z_{i-1,j}^n+z_{i,j+1}^n+z_{i,j-1}^n)+(2-\frac{4c^2\Delta t^2}{h^2})z_{i,j}^n-z_{i,j}^{n-1}$

上式中$c$就是速度，$h$是单个格子的大小。