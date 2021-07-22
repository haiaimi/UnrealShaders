# Precomputed Radiance Transfer
## 球谐光照（Spherical Harmonic Lighting）
首先要先理解球谐光照，球谐光照就是首先Capture光照，然后在runtime时Relight，通常只要几个系数就可以表示一个球面的低频光照信息。

## 蒙特卡洛积分
设变量$x$服从概率密度函数$p(x)$，函数$f(x)$的数学期望就是：

$$E[f(x)]=\int f(x)p(x)dx$$

积分式$I=\int f(x)dx$可以有下面的变形：

$$I=\int \frac{f(x)}{p(x)}p(x)dx=E[\frac{f(x)}{p(x)}]$$

可以通过近似方法得到：

$$I=E[\frac{f(x)}{p(x)}]\approx\frac{1}{N}\sum_{i=1}^{N}\frac{f(x^i)}{p(x^i)}$$

意思就是随机均匀选取一些采样点

我们这里需要对球面进行积分，所以需要有均匀的随机变量，所以$\theta,\phi$如下：
球面上的面积微元$d_A$里的采样概率：
$$P_A=p(v)dA=\frac{1}{4\pi}dA$$
$$d_A=sin\theta d\theta d\phi$$
参数化概率密度函数为：
$$p(\theta,\phi)=\frac{1}{4\pi}sin\theta$$
求两个二维独立随机变量的概率密度函数的边缘密度函数：
$$f(\theta)=\int_{0}^{2\pi}p(\theta,\phi)d\phi=\frac{sin\theta}{2}$$
$$f(\phi)=\int_{0}^{\pi}p(\theta,\phi)d\theta=\frac{1}{2\pi}$$
再来一次边缘密度函数：
$$F_\theta(\theta)=\int_0^{\theta}f(\hat\theta)d\hat{\theta}=\frac{1-cos\theta}{2}$$
$$F_\phi(\phi)=\int_{0}^{\phi}f(\hat\phi)d\hat{\phi}=\frac{\phi}{2\pi}$$

求反函数：

$$
\begin{cases}
\theta=2arccos(\sqrt{1-\xi_x})\\
\phi=2\pi\xi_y
\end{cases}
$$

## 球谐函数
Spherical Harmonics中的Harmonics一般译为调和，就是“谐波”的意思。Harmonics一般就是拉普拉斯方程（Laplace's Equation）的解，解也一般含有$sin$和$cos$。在傅里叶分析里面，把单位圆上的周期函数用调和函数展开，再拓展到n维球面，就会得到球面调和（Spherical Harmonics）。

### 函数正交
傅里叶变换里的函数基底就是谐波$sin(nx)$和$cos(nx)$。这些基底函数一般就是正交函数。两个正交函数在定义域里内积为0：$\int f(x)g(x)d_x=0$。

### 勒让德多项式
正交多项式（Orthogonal Polynomial），如下特性：
$$
\int _{-1}^{1}F_m(x)F_n(x)dx=
\begin{cases}
0 (n\neq m)\\
c (n=m)
\end{cases}
$$

当$c=1$时就是标准正交。勒让德多项式（Legendre Polynomial）就是其中的一种。球谐光照里最为关注勒让德微分方程（Associated Legendre Polynomial）也称为ALP，勒让德函数就是勒让德微分方程的解：

$$(1-x^2)=\frac{d^2P(x)}{dx^2}-2x\frac{dP(x)}{dx}+n(n+1)P(x)=0$$

在试图球面坐标求解三维拉普拉斯方程时，问题就会归结为勒让德方程的解。

当$n\in Z^*$，$x=\pm1$时也有解，这样由正交多项式组成的多项式序列成为勒让德多项式：
$$P_n(x)=\frac{1}{2^n\cdot n!}\frac{d^n}{dx^n}[(x^2-1)^n]$$

伴随勒让德多项式就可以根据普通勒让德多项式$P_n(x)$来定义：
$$P_l^m=(-1)^m(1-x^2)^{m/2}\frac{d_m}{dx^m}(P_l(x))$$

### 球谐函数
单位球面参数化：
$(sin\theta cos\phi,sin\theta sin\phi,cos\theta)->(x,y,z)$

三维空间中的拉普拉斯算子可以表示为：
$$\nabla ^2=\frac{\partial ^2}{\partial x^2}+\frac{\partial ^2}{\partial y^2}+\frac{\partial ^2}{\partial z^2}=0$$

球面坐标代入拉普拉斯方程：
$$\frac{1}{r^2}\frac{\partial}{\partial r}(r^2\frac{\partial f}{\partial r})+\frac{1}{r^2sin\theta}\frac{\partial}{\partial \theta}(sin\theta\frac{\partial f}{\partial \theta})+\frac{1}{r^2sin^2\theta}\frac{\partial^2f}{\partial \phi^2}=0$$

为了求拉普拉斯的解把距离变量$r$和方向变量$\theta$和$\phi$分离，如下：
$$f(r,\theta,\phi)=R(r)Y(\theta,\phi)$$

代入到拉普拉斯方程中得到：
$$\frac{Y}{r^2}\frac{\partial}{\partial r}(r^2\frac{\partial R}{\partial r})+\frac{R}{r^2sin\theta}\frac{\partial}{\partial \theta}(sin\theta\frac{\partial Y}{\partial \theta})+\frac{R}{r^2sin^2\theta}\frac{\partial^2Y}{\partial \phi^2}=0$$

两边同时乘以:
$$\frac{r^2}{RY}$$
得:
$$\frac{1}{R}\frac{\partial}{\partial r}(r^2\frac{\partial R}{\partial r})=\frac{1}{Ysin\theta}\frac{\partial}{\partial \theta}(sin\theta\frac{\partial Y}{\partial \theta})-\frac{1}{Ysin^2\theta}\frac{\partial^2Y}{\partial \phi^2}$$

可以看到等式左边只和$r$有关，右边只和$\theta$，$\phi$相关，两边相等不可能，除非等于一个常数，这个常数记为$l(l+1)$：

$$\frac{1}{R}\frac{\partial}{\partial r}(r^2\frac{\partial R}{\partial r})=\frac{1}{Ysin\theta}\frac{\partial}{\partial \theta}(sin\theta\frac{\partial Y}{\partial \theta})-\frac{1}{Ysin^2\theta}\frac{\partial^2Y}{\partial \phi^2}=l(l+1)$$

继续分解为下面得等式：
$$\frac{\partial}{\partial r}(r^2\frac{\partial R}{\partial r})-l(l+1)R=0$$
$$\frac{1}{sin\theta}\frac{\partial}{\partial \theta}(sin\theta\frac{\partial Y}{\partial \theta})+\frac{1}{sin^2\theta}\frac{\partial^2Y}{\partial \phi^2}+l(l+1)Y=0$$

由于只和角度相关，所以方程也称为球函数方程：
$$Y(\theta,\phi)=\Theta(\theta)\Phi(\phi)$$

带入球函数方程：
$$\frac{\Phi}{sin\theta}\frac{\partial}{\partial \theta}(sin\theta\frac{\partial \Theta}{\partial \theta})+\frac{\Theta}{sin^2\theta}\frac{\partial^2\Phi}{\partial \phi^2}+l(l+1)\Theta\Phi=0$$

两边乘以：
$$\frac{sin^2\theta}{\Theta\Phi}$$
得：

$$\frac{sin\theta}{\Theta}\frac{\partial}{\partial \theta}(sin\theta\frac{\partial \Theta}{\partial \theta})+l(l+1) sin^2\theta=-\frac{1}{\Phi}\frac{\partial^2\Phi}{\partial \phi^2}$$

左边是$\theta$得函数，跟$\phi$无关，右边是$\phi$相关得，所以相等不可能，除非是个常量，记为$\lambda$：

$$\frac{sin\theta}{\Theta}\frac{\partial}{\partial \theta}(sin\theta\frac{\partial \Theta}{\partial \theta})+l(l+1) sin^2\theta=-\frac{1}{\Phi}\frac{\partial^2\Phi}{\partial \phi^2}=\lambda$$

又拆解成两个常微分方程：
$$\frac{\partial^2\Phi}{\partial \phi^2}+\lambda \Phi=0$$
$$\sin\theta\frac{\partial}{\partial \theta}(sin\theta\frac{\partial \Theta}{\partial \theta})+[l(l+1) sin^2\theta-\lambda]\Theta=0$$

对于常微分方程，有一个隐含得“自然周期条件”，$(\Phi(\phi+2\pi)=\Theta(\phi))$，两者构成本征值（特征值）问题，即：
$$\lambda=m^2,(m=0,\pm1,\pm2...)$$

其周期解得复数形式为：
$$\Phi(\phi)=e^{im\phi},m=0,\pm1,\pm2...$$

$$cosm\phi+isin m\phi=e^{im\phi}$$

可以把上面得常微分方程改成：
$$\frac{1}{sin\theta}\theta\frac{\partial}{\partial \theta}(sin\theta\frac{\partial \Theta}{\partial \theta})+[l(l+1) sin^2\theta-\lambda]\Theta=0$$

令$x=cos\theta$则：
$$\frac{\partial\Theta}{\partial\theta}=\frac{\partial\Theta}{\partial x}\frac{\partial x}{\partial\theta}=-sin\theta\frac{\partial\Theta}{\partial x}$$

代入上面得常微分方程可得：
$$(1-x^2)\frac{\partial^2\Theta}{\partial^2x}-2x\frac{\partial\Theta}{\partial x}+[l(l+1)-\frac{m^2}{1-x^2}]\Theta=0$$

上述得方程就是**I次连代勒让德方程**，当$m=0$时的特例就是：
$$(1-x^2)\frac{\partial^2}{\partial x^2}-2x\frac{\partial \Theta}{\partial x}+l(l+1)\Theta=0$$

称为**I次勒让德方程**

后面只考虑连带勒让德方程，它的解就称为**连带勒让德函数**，只有当$\lambda=l(l+1),l=0,1,...$时有周期解，用$P_l^m(x)$表示，*这里把$x$替换成$cos\theta$*，即
$$\Theta(\theta)=P_l^m(cos\theta)\space\{m=0,\pm1,...,\pm l\}$$

连带勒让德函数（连带勒让德方程的解）表示为：
$$P_l^m(x)=\frac{(-1)^m(1-x^2)^{\frac{m}{2}}}{2^ll!}\frac{d^{l+m}}{dx^{l+m}}(x^2-1)^l$$

上面就是l次m阶连代勒让德函数，当$m>l$，连带勒让德函数里面有一个$m+l$（上式内容$\frac{d^{l+m}}{dx^{l+m}}(x^2-1)^l$）次导数计算，在计算机上很难处理，但是有递归关系，如下：
$$
\begin{cases}
(l-m)P_l^m(x)=x(2l-1)P_{l-1}^m(x)-(l+m-1)P_{l-2}^m(x)\\
P_m^m(x)=(-1)^m(2m-1)!!(1-x^2)^{m/2}\\
P_{m+1}^m(x)=x(2m+1)P_m^m(x)
\end{cases}
$$
$!!$表示双阶乘，即$(2m-1)!!=1\cdot3\cdot5\cdot\cdot\cdot(2m-1)$

l次m阶连带勒让德函数得关系等式：
$$P_l^m(x)=(-1)^m\frac{(l+m)!}{(l-m)!}P_l^{-m}(x)$$

代入到球函数中，它的$Y(\theta,\phi)$的通解复数形式（结合前面的$\Phi(\phi)=e^{im\phi}$）表示为：
$$Y(\theta,\phi)=\sum^{\infty}_{l=0}\sum^{l}_{k=-l}P_l^k(cos\theta)e^{im\phi},m=0,\pm1,\pm2,...$$

一般得l次m阶球谐函数$Y_{lm}(\theta,\phi)$的复数形式可以表示为：
$$Y_{lm}(\theta,\phi)=P_{lm}(cos\theta)e^{im\phi},m=0,\pm1,\pm2,...$$

球谐函数的模长表示为：
$$(N_l^m)^2=\iint_SY_{lm}(x)[Y_{lm}(x)]^*sin\theta d\theta d\phi=\frac{2}{2l+1}\frac{(l+|m|)!}{(l-|m|)!}2\pi$$

归一化球谐函数$Y_l^m(\theta,\phi)$的复数形式表示为：
$$Y_l^m(\theta,\phi)=K_l^mY_{lm}(\theta,\phi)$$

其中$K_l^m$表示为：
$$K_l^m=\frac{1}{N_l^m}=\sqrt{\frac{2l+1}{4\pi}\frac{(l-|m|)!}{(l+|m|)!}}$$

其中$Y_{lm}(\theta,\phi)$表示一般形式的球谐函数，$Y_l^m(\theta,\phi)$表示归一化球谐函数。

球谐函数的实数表现形式，当$m>0$时采用实数$cos$部分，当$m<0$时采用虚数$sin$部分，归一化的球谐函数的实数表现形式为：
$$Y_l^m(\theta,\phi)=
\begin{cases}
\sqrt{2}K_l^mcos(m\phi)P_l^m(cos\theta)\space m>0\\
\sqrt{2}K_l^msin(-m\phi)P_l^{-m}(cos\theta)\space m<0\\
K_l^0P_l^m(cos\theta)\space m=0
\end{cases}
$$

根据上面的公式就可以推导出球谐函数。

### 球谐性质

* 正交完备性
* 旋转不变性

#### 正交完备性
任意两个归一化的球谐函数在球面上的积分有：
$$\iint_S Y_l^m(\theta,\phi)Y_k^n(\theta,\phi)sin\theta d\theta d\phi=
\begin{cases}
0 \space m\neq n,l\neq k\\
1 \space m=n,l=k
\end{cases}
$$

这就表示由球谐函数构成的函数组$\{Y_l^m(\theta,\phi)\}$是正交归一化的。

以某一正交归一函数组为基，把一个给定的函数用这个函数以线性组合来表示，这是一种重要的展开，一个著名的例子就是傅里叶变换。

任意一个球面函数$f(\theta,\phi)$可以用正交归一的球函数$Y_l^m(\theta,\phi)$进行展开，这种展开就是类似于傅里叶展开，称为**广义傅里叶展开**：
$$f(\theta,\phi)=\sum^\infty_{l=0}\sum^l_{m=-1}C_l^mY_l^m(\theta,\phi)$$

其中**广义傅里叶系数**$C_l^m$为：
$$C_l^m=\int_0^{2\pi}\int_0^{\pi}f(\theta,\phi)Y_l^m(\theta,\phi)sin\theta d\theta d\phi$$
当$l\to\infty$时，展开级数和会平均收敛于$f(\theta,\phi)$，也就是当$l$越大，级数和就会越趋近于被展开的函数$f(\theta,\phi)$。平均收敛并不代表收敛只是趋近的含义。
$n$的取值不可能无限大，一般就是取固定系数如$n=2$，如下：
$$\{Y_l^m(\theta,\phi)\}=\{Y_0^0,Y_1^{-1},Y_1^0,Y_1^1\}$$

给定系数$n$，得到的球谐函数组的个数就为$n^2$
广义傅里叶系数相当于这样的排列：
$$C_0^0,C_1^{-1},C_1^0,C_1^1,C_2^{-2}...$$

用一个系数$c_k$来表示上面的广义傅里叶系数，用函数$y_k(\theta,\phi)$来表示球谐函数，可以换成如下形式：
$$f(\theta,\phi)=\sum_{k=0}^{n^2-1}c_ky_k(\theta,\phi)$$

球谐函数相当于正交基，将函数$f(\theta,\phi)$表示为这组正交基的线性组合，生成线性组合系数的过程就是**投影**。相反，利用这组系数和正交基组合。得到函数的过程就是**重建**。投影就相当于计算函数积分，计算消耗大，一般离线处理。但是在实时中可以快速重建原始函数。

#### 傅里叶级数（Fourier Series）
任意周期的函数都可以写成三角函数之和，傅里叶级数就是通过三角函数和常数项来叠加逼近周期为T的函数$f(x)$，任何周期函数都可以看成是不同振幅，不同相位正弦波叠加。
傅里叶级数就是向量，如下：
$$f(x)=a_0+\sum^\infty_{n=1}(a_ncos(\frac{2\pi n}{T}x)+b_nsin(\frac{2\pi n}{T}x))$$

上面实际上就是把$f(x)$当作如下基的向量：
$$\{1,cos(\frac{2\pi n}{T}),sin(\frac{2\pi n}{T}x)\}$$

为了求基的系数，可以两边同时乘以$cosnx$，如下：
$$\int_{-\pi}^{\pi}f(x)cosnxdx=a_0\int_{-\pi}^{\pi}cosnxdx+\sum_{n=1}^{\infty}a_n\int_{-\pi}^{\pi}cosnxcosnxdx+\sum_{n=1}^{\infty}b_n\int_{-\pi}^{\pi}sinnxcosnxdx$$

由于不同向量是正交的（周期内积分为0），所以非零项只有$\sum_{n=1}^{\infty}a_n\int_{-\pi}^{\pi}cos^2nxdx$，$a_n\int_{-\pi}^{\pi}cos^2nxdx=\pi a_n$也就得到：
$$an=\frac{1}{\pi}\int_{-\pi}^{\pi}f(x)cosnxdx$$

可以用下面的方式表示：
$$an=\frac{<f(x),cosnx>}{<cosnx,cosnx>}$$

时域就是我们正常看到的波形。

正弦波其实就是一个圆周运动在一条直线上得投影，频域得基本单元可以理解为一个始终在旋转得圆：

![image](../RenderPictures/Fourier/Fourier01.jpg)

那么多个正弦波叠加情况就如下动图：

![image](https://upload.wikimedia.org/wikipedia/commons/1/1a/Fourier_series_square_wave_circles_animation.gif)

至于频域的表示，横轴就是正弦波函数，也就是基，纵轴（高度）就是振幅，也就是向量：

![image](https://upload.wikimedia.org/wikipedia/commons/2/2b/Fourier_series_and_transform.gif)

在频域分析中振幅、频率、相位缺一不可，不同相位决定了波的位置，首先要直到时间差，时间差就是距离频率轴最近的波峰的距离，相位差就是时间差在一个周期里的比例，并乘以2PI。

#### 傅里叶变换（Fourier Transformation）
上面说的傅里叶级数是时域周期且连续的函数，频域是一个非周期离散函数。
傅里叶变换就是把一个时域非周期的连续信号转换为一个频域非周期的连续信号。

之前的区间都是$[-\pi,\pi]$，这里需要把区间换成$[-a,a]$，相当于把函数和基都拉伸了$a/\pi$倍，基也就变成了：
$$\{1,sin\space nwx,cos\space nwx\},\{exp(inwx)\},w=\frac{\pi}{a}$$

傅里叶级数也让就变成了如下：
$$f(x)=a_0+\sum_{n=1}^{\infty}a_ncos\space nwx+b_nsin\space nwx ,or \space f(x)=\sum_{n=-\infty}^{\infty}c_nexp(inwx)$$

* 区间变成$[-a,a]$后，两边同时乘以$exp(-inwx)$，并在$[-a,a]$上积分，傅里叶系数就变为:
  $$\int_{-a}^{a}f(x)exp(-inwx)dx=\sum_{n=-\infty}^{\infty}\int_{-a}^{a}c_nexp(inwx)exp(-inwx)dx$$
  $$c_n=\frac{1}{2a}\int_{-a}^{a}f(t)exp(-in\pi t/a)dt$$
* 把傅里叶系数代入到$f(x)$中，得到：
  $$f(x)={lim}_{a\to\infty}[\sum_{n=-\infty}^{\infty}(\frac{1}{2a}\int_{-a}^{a}f(t)exp(-in\pi t/a)dt)exp(in\pi x/a)]$$
* 上式积分是与$t$相关的，外部的项$exp(in\pi x/a)$与$t$无关，可以写成：
  $$f(x)={lim}_{a\to\infty}[\sum_{n=-\infty}^{\infty}\frac{1}{2a}\int_{-a}^{a}f(t)exp(in\pi(x-t)/a)dt]$$
* 为了凑出黎曼和，令$\lambda_n=\frac{n\pi}{a},\Delta\lambda=\lambda_{n+1}-\lambda_{n}=\frac{\pi}{a}$，代入上式：
  $$f(x)={lim}_{a\to\infty}[\sum_{n=-\infty}^{\infty}\frac{1}{2\pi}\int_{-a}^{a}f(t)exp(i\lambda_n(x-t))dt]\Delta\lambda$$
* 当$a\to\infty$时，$\Delta\lambda\to 0$，由于是[黎曼和](https://zh.wikipedia.org/wiki/%E9%BB%8E%E6%9B%BC%E7%A7%AF%E5%88%86)的形式上式可以写成定积分：
  $$f(x)=\frac{1}{2\pi}\int_{-\infty}^{\infty}[\int_{-\infty}^{\infty}f(t)exp(i\lambda(x-t))dt]d\lambda$$
* 上式变形可得：
  $$f(x)=\frac{1}{\sqrt{2\pi}}\int_{-\infty}^{\infty}(\frac{1}{\sqrt{2\pi}}\int_{-\infty}^{\infty}f(t)exp(-i\lambda t)dt)exp(i\lambda x)d\lambda$$


在$a\to\infty$下，本质上还是求和式，因为极限写成了积分，这里把$f(x)$写成了$exp(i\lambda x)d\lambda$的线性组合。这里的变量$\lambda$是取遍整个数轴的，数轴上每个点都对应函数的一个基。括号里的内容就是函数的傅里叶变换：
$$\hat{f(x)}=\frac{1}{\sqrt{2\pi}}\int_{-\infty}^{\infty}f(t)exp(-i\lambda t)dt$$
这个函数的意义：在$\lambda$处的函数值$\hat{f(\lambda)}$表示函数$f(x)$在$\lambda$对应基上的系数。

#### 球谐旋转不变性
旋转不变性，表示原函数发生了旋转，只需要对生成的广义傅里叶系数进行变换，就能保证变换后的系数能等价还原出新函数。在图形渲染上的表现就是当光源发生旋转后，只需要同步计算出变换后的广义傅里叶系数，就能保证画面的光照效果不会抖动跳变。旋转不变性并不是表示原函数发生旋转后对重建结果没有影响。
如何对生成的系数进行变换：

对于$l$次的球谐函数，会有$2l+1$个系数，表示为：
$$C_l=\{C_l^{-l},C_l^{-l+1},...,C_l^{l-1},C_l^l\}$$
设变换矩阵为$R_{SH}^l$，它是一个$(2l+1)*(2l+1)$的矩阵，系数的变换可以表示为：
$$B_l^m=\sum_{k=-l}^{k=l}M_l^{m,k}C_l^k$$
用向量与矩阵的乘积形式，为：
$$B_l=C_l\cdot R_{SH}^l$$
经过旋转变换后的函数展开就可以表示为：
$$f(R(\theta,\phi))=\sum_{l=0}^{\infty}\sum_{m=-1}^lB_l^mY_l^m(\theta,\phi)$$

系数的变换是基于球谐函数的次数，即第3次的球谐函数的系数$B_3$只能由第3次的球谐函数的系数$C_3$变换而来。若取前3次的球谐函数构成正交基，函数组共有0，1，2次三类球谐函数，则三个子矩阵需要整合成一个完整的变换矩阵，对于前3次球谐函数的例子，就组成一个9x9的变换矩阵，形状如下：
$$\left(
 \begin{matrix}
   X & 0 & 0 & 0 & 0 & 0 & 0 & 0 & 0 \\
   0 & X & X & X & 0 & 0 & 0 & 0 & 0 \\
   0 & X & X & X & 0 & 0 & 0 & 0 & 0 \\
   0 & X & X & X & 0 & 0 & 0 & 0 & 0 \\
   0 & 0 & 0 & 0 & X & X & X & X & X \\
   0 & 0 & 0 & 0 & X & X & X & X & X \\
   0 & 0 & 0 & 0 & X & X & X & X & X \\
   0 & 0 & 0 & 0 & X & X & X & X & X \\
   0 & 0 & 0 & 0 & X & X & X & X & X \\
\end{matrix}
  \right)
$$

在低维情况下，旋转可以用旋转矩阵，欧拉角，四元数表示，任意一个旋转矩阵$R$可以用$Z_\alpha Y_\beta Z_\gamma$型的欧拉角表示：
$$\left(
 \begin{matrix}
   R_{0,0} & R_{0,1} & R_{0,1} \\
   R_{1,0} & R_{1,1} & R_{1,2} \\
   R_{2,0} & R_{2,1} & R_{2,2} \\
\end{matrix}
  \right)=
  \left(
 \begin{matrix}
   c_\alpha c_\beta c_\gamma-s_\alpha s_\gamma & c_\alpha s_\gamma + s_\alpha c_\beta c_\gamma & -s_\beta c_\gamma \\
   -s_\alpha c_\gamma-c_\alpha c_\beta s_\gamma & c_\alpha c_\gamma-s_\alpha c_\beta s\gamma & s_\beta s_\gamma \\
   c_\alpha s_\beta & s_\alpha s_\beta & c_\beta \\
\end{matrix}
  \right)
$$
$c$表示$cos$，$s$表示$sin$。

有了这个变换，可以很容易的计算出欧拉角$\alpha,\beta, \gamma$：
$$sin\beta=\sqrt{1-R_{2,2}^2}$$
$$
\begin{cases}
\alpha=atan2f(R_{2,1}/sin\beta,R_{2,0}/sin\beta) \\
\beta=atan2f(sin\beta,R_{2,2}) \\
\gamma=atan2f(R_{1,2}/sin\beta,-R_{0,2}/sin\beta)
\end{cases}
$$

在$R_{2,2}=1$的退化情况下，欧拉角表示：
$$
\begin{cases}
\alpha=atan2f(R_{0,1},R_{0,0}) \\
\beta=0 \\
\gamma=0
\end{cases}
$$

相应的$l$次球谐系数的变换矩阵可以表示为：
$$R_{SH}^l(\alpha,\beta,\gamma)=Z_{\gamma}Y_{-90}Z_{\beta}Y_{+90}Z_{\alpha}$$

第0次球谐变换矩阵为：
$$R_{SH}^0(\alpha,\beta,\gamma)=1$$

第一次球谐变换矩阵：
$$R_{SH}^1(\alpha,\beta,\gamma)= 
  \left(
 \begin{matrix}
   c_\alpha c_\gamma-s_\alpha c_\beta s_\gamma & -s_\beta s_\gamma & - s_\alpha c_\gamma-c_\alpha c_\beta s_\gamma \\
   -s_\alpha s_\gamma & c_\beta & -c_\alpha s_\beta \\
   c_\alpha s_\gamma + s_\alpha c_\beta c_\gamma & s_\beta c_\gamma & c_\alpha c_\beta c_\gamma - s_\alpha s_\gamma\\
\end{matrix}
  \right)=\left(
 \begin{matrix}
   R_{1,1} & -R_{1,2} & R_{1,0} \\
   -R_{2,1} & R_{2,2} & R_{2,0} \\
   R_{0,1} & -R_{0,2} & R_{0,0} \\
\end{matrix}
  \right)
$$

旋转特性后面再说

### 预计算传输与着色
了解了球谐的基本理论，就要应用到具体的光照计算上：
首先光照公式：
$$L(x,\vec{\omega}_{o})=L_e(x,\omega_{o})+\int_s f_r(x,\vec{\omega}_i\to \vec{\omega}_o)L(x',\vec{\omega}_i)G(x,x')V(x,x')d{\omega_i}$$

$L(x,\vec{\omega}_o)$：在$x$点$\omega_o$的方向反射的光强

$L_e(x,\vec{\omega}_o)$：$x$点的自发光

$f_r(x,\vec{\omega}_i\to \vec{\omega}_o)$：光源在$x$点表面从入射方向$\vec{\omega}_i$到出射方向$\vec{\omega}_o$的BRDF结果

$L(x',\vec{\omega}_i)$：从另一个物体的$x'$点沿着$\vec{\omega}_i$方向到达的光

$G(x,x')$：$x$与$x'$之间的几何关系

$V(x,x')$：$x$到$x'$的可见性

