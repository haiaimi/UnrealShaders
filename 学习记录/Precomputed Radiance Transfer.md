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
$$Y(\theta,\phi)=\sum^{\infin}_{l=0}\sum^{l}_{k=-l}P_l^k(cos\theta)e^{im\phi},m=0,\pm1,\pm2,...$$

一般得l次m阶球谐函数$Y_{lm}(\theta,\phi)$的复数形式可以表示为：
$$Y_{lm}(\theta,\phi)=P_{lm}(cos\theta)e^{im\phi},m=0,\pm1,\pm2,...$$

球谐函数的模长表示为：
$$(N_l^m)^2=\int\int_SY_{lm}(x)[Y_{lm}(x)]^*sin\theta d\theta d\phi=\frac{2}{2l+1}\frac{(l+|m|)!}{(l-|m|)!}2\pi$$

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
$$\int_S\int Y_l^m(\theta,\phi)Y_k^n(\theta,\phi)sin\theta d\theta d\phi=
\begin{cases}
0 \space m\neq n,l\neq k\\
1 \space m=n,l=k
\end{cases}
$$

这就表示由球谐函数构成的函数组$\{Y_l^m(\theta,\phi)\}$是正交归一化的。

以某一正交归一函数组为基，把一个给定的函数用这个函数以线性组合来表示，这是一种重要的展开，一个著名的例子就是傅里叶变换。

任意一个球面函数$f(\theta,\phi)$可以用正交归一的球函数$Y_l^m(\theta,\phi)$进行展开，这种展开就是类似于傅里叶展开，称为**广义傅里叶展开**：
$$f(\theta,\phi)=\sum^\infin_{l=0}\sum^l_{m=-1}C_l^mY_l^m(\theta,\phi)$$

其中**广义傅里叶系数$C_l^m$**为：
$$C_l^m=\int_0^{2\pi}\int_0^{\pi}f(\theta,\phi)Y_l^m(\theta,\phi)sin\theta d\theta d\phi$$
当$l\to\infin$时，展开级数和会平均收敛于$f(\theta,\phi)$，也就是当$l$越大，级数和就会越趋近于被展开的函数$f(\theta,\phi)$。平均收敛并不代表收敛只是趋近的含义。
$n$的取值不可能无限大，一般就是取固定系数如$n=2$，如下：
$$\{Y_l^m(\theta,\phi)\}=\{Y_0^0,Y_1^{-1},Y_1^0,Y_1^1\}$$

给定系数$n$，得到的球谐函数组的个数就为$n^2$
广义傅里叶系数相当于这样的排列：
$$C_0^0,C_1^{-1},C_1^0,C_1^1,C_2^{-2}...$$

用一个系数$c_k$来表示上面的广义傅里叶系数，用函数$y_k(\theta,\phi)$来表示球谐函数，可以换成如下形式：
$$f(\theta,\phi)=\sum_{k=0}^{n^2-1}c_ky_k(\theta,\phi)$$

球谐函数相当于正交基，将函数$f(\theta,\phi)$表示为这组正交基的线性组合，生成线性组合系数的过程就是**投影**。相反，利用这组系数和正交基组合。得到函数的过程就是**重建**。投影就相当于计算函数积分，计算消耗大，一般离线处理。但是在实时中可以快速重建原始函数。
