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


把球谐函数记为$Y_l^m(\theta,\phi)$，表达式为：