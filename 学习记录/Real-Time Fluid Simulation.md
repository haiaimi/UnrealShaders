# 简介
本篇是介绍游戏中实时流体的实现，来自paper [Real-Time Fluid Dynamic for Games](https://pdfs.semanticscholar.org/847f/819a4ea14bd789aca8bc88e85e906cfc657c.pdf)。本文里的流体是依据基于物理的流体等式，也就是*Navier-Stokes equations*，下面就称为NS等式。NS等式很早就被提出了，正常被应用高精度，并且有很高的物理精准度的工程模拟，如飞机、桥梁等。但是游戏中不需要这么精准，仅仅只要看起来差不多就行。所以本文只为视觉效果，不需要考虑物理学。

# 流程分析
首先看一下流体物理学的公式：    
$\frac{\partial u}{\partial t}=-(u\cdot\nabla)u+\nu{\nabla}^2u+f$

$\frac{\partial \rho}{\partial t}=-(u\cdot\nabla)\rho+\kappa{\nabla}^2\rho+S$

上式是针对速度场（以向量为单位）NS方程，下式是速度场中移动的密度方程。

速度场本身没有什么特别的，只有把它用于移动烟、尘埃粒子或者树叶才会有比较有趣的效果。通过转换物体周围的速度到对物体施加的力。比较轻的物体如尘埃就是被速度场携带。对于烟来说，由于模拟每个粒子过于昂贵，所以就用烟密度来代替，由一个连续函数计算在空间中的每一个点尘埃的数量。密度通常取值0-1。从速度场到密度场的进化也表示了精准的数学等式。密度方程实际上要比速度方程更加简单，因为前者是线性的而后者是非线性。

## A Fluid in a Box
本文中对流体的解析都是基于二维空间，当然拓展到3维空间也会比较容易。主要就是一个二维格子，如果尺寸是*N*，那么实际的数组尺寸就是$size=(N+2)*(N+2)$，多出来的一层用于边界处理。每一个格子都包含了密度和速度并定义在各自中心，所以有如下的声明：
```cpp
static u[size], v[size], u_prev[size], v_prev[size];
static dens[size], dens_prev[size];

#define IX(i,j) ((i)+(N+2)*(j)) //根据(i,j)求对应坐标的值
```
通常认为整体格子的边长为1，那么每个网格单元的宽是*h=1/N*，因为模拟就是速度和密度格子的一系列Snapshots，所以Snapshots之间的时间差就是*dt*。
如下图：

![image](https://github.com/haiaimi/UnrealShaders/blob/master/RenderPictures/Real-Time%20Fluid%20Simulation/FluidGrids.png)

## Moving Densities
首先解释密度场在一个固定的速度场中移，上述的等式表明导致密度改变有三个原因：1.密度需要跟随速度场，2.密度在一个固定的频率扩散，3.密度由于源头增加。

1. 第一步还是比较好实现，可以认为每帧的源由数组*s[]*提供，而这个数组则是跟据游戏中检测密度源的部分填充，例如可以是玩家鼠标的移动。可以看下图：

![image](https://github.com/haiaimi/UnrealShaders/blob/master/RenderPictures/Real-Time%20Fluid%20Simulation/DensityTerms.png)

如下代码：
```cpp
void add_source(int N, float* x, float* s, float dt)
{
    int i,size=(N+2)*(N+2);
    for(i=0;i<size;++i)
        x[i]+=dt*s[i];
}
```

2. 第二步解释了以*diff*扩散的可能，当*diff>0*时密度将在网格单元间扩散，首先考虑单一的网格单元，在这种情况下这个网格单元将只向周围的4个网格单元进行密度交换。一个可能的扩散解算方法是在每个网格单元进行简单的计算交换量，然后累加到已有值，如下代码：
```cpp
void diffuse_bad(int N, int b, float* x, foat* x0, float diff, float dt)
{
    int i,j;
    float a = dt*diff*N*N;
    for(i=1; i<=N; i++){
        for(j=1; j<=N; j++){
            x[IX(i, j)] = x0[IX(i, j)] + a * (x0[IX(i-1,j)]+x0[IX(i+1,j)]+x0[IX(i,j-1)]+x0[IX(i,j+1)]-4*x0[IX(i,j)]);
        }
    }
    set_bnd(N,b,x);
}
```
这里的set_bnd()方法是用来设置边界的网格单元。但是上面的计算方法实际中不管用，不稳定，对于较大的扩散率密度开始震荡，变为负数并最终发散，使模拟无效。所以需要考虑一个稳定的方法，这个方法的基本思想就是找到在时间向后时开始时的密度，如下代码：
```cpp
 x0[IX(i, j)] = x[IX(i, j)] - a * (x[IX(i-1,j)]+x[IX(i+1,j)]+x[IX(i,j-1)]+x[IX(i,j+1)]-4*x[IX(i,j)]);
```
对于未知数*X[IX(i,j)]*来说是一个线性系统，可以为这个线性系统构建矩阵然后调用标准矩阵求逆的方法。可以用*Gauss-Seidel迭代*法（高斯－赛德尔迭代）进行求解，如下代码:
```cpp
void diffuse(int N, int b, float* x, float* 0， float diff, float dt)
{
    int i,j,k;
    float a=dt*diff*N*N;

    for(k=0; k<20; ++k){
        for(i=1; i<N; ++i){
            for(j=1; j<=N; ++j)
            {
                x[IX(i,j)] = (x0[IX(i,j)] + a*(x[IX(i-1, j)] + x[IX(i+1, j)] + x[IX(i, j-1)] + x[IX(i, j+1)]))/(1 + 4*a);
            }
        }
        set_bnd(N, b x);
    }
}
```

这里说到高斯-塞德尔迭代，大致思想就是通过对n阶线性方程组进行迭代来收敛得到最后的解：  

$Ax=b$   同解变形得

$x=Bx+f$   构造迭代公式得

$x^{(k+1)}=Bx^{(k)}+f$

首先是雅可比迭代法：
 $x(n)=\begin{cases}
    a_{11}x_1+a_{12}x_2+...+a_{1n}x_n=b_1\\
    a_{21}x_1+a_{22}x_2+...+a_{2n}x_n=b_2\\
    ...\\
    a_{n1}x_1+a_{n2}x_2+...+a_{nn}x_n=b_n
     \end{cases}$

可迭代公式为：
 $x(n)=\begin{cases}
    x_{(0)}=(x_1^{(0)},x_2^{(0)},x_3^{(0)},...,x_n^{(0)})^T\\
  x_i^{(k+1)}=\frac{1}{a_{ii}}(b_i-\sum_{i=1,j\neq i}^na_{ij}x_j^{(k)})
     \end{cases}$
    

雅可比迭代的矩阵形式，上面的A为非奇异，A分裂为$A=D+L+U$，如下：
$$
 D=\left[
 \begin{matrix}
   a_{11} &  & &  \\
    & a_{22} & & \\
    &  & \ddots &  \\
    &  & & a_{nn}
  \end{matrix}
  \right]
$$
$$
 L=\left[
 \begin{matrix}
    0 & & &  \\
    a_{21}& 0 & & \\
    \cdots& & \ddots &  \\
    a_{n1}& a_{n2} & \cdots & 0
  \end{matrix}
  \right]
$$

$$
 U=\left[
 \begin{matrix}
    0 & a_{12} & \cdots &a_{1n}  \\
    & 0 & \cdots & a_{2n} \\
    & & \ddots & \cdots\\
    & & & 0
  \end{matrix}
  \right]
$$

所以可以变形为
$Dx=-(L+U)x+b$

即$x=-D^{-1}(L+U)x+D^{-1}b$

所以雅可比矩阵形式为：
$x(n)=\begin{cases}
    x_{(0)}=(x_1^{(0)},x_2^{(0)},x_3^{(0)},...,x_n^{(0)})^T\\
  x^{(k+1)}=-D^{-1}(L+U)x^{(k)}+D^{-1}b
     \end{cases}$

高斯赛德尔迭代法会使用前一个计算出来的值来加快收敛，因此效果可能会更好，所以G-S迭代公式如下：
$\begin{cases}
    x^{(0)}=(x_1^{(0)},x_2^{(0)},x_3^{(0)},...,x_n^{(0)})^T\\
  x_i^{(k+1)}=\frac{1}{a_{ii}}(b_i-\sum_{j=1}^{i-1}a_{ij}x_j^{(k+1)}-\sum_{j=i+1}^{n}a_{ij}x_j^{(k)})
     \end{cases}$

同样需要把A分裂，$A=D+L+U$，得到$Dx=-(L+U)x+b$，$G-S$迭代式可表示为：
$Dx^(k+1)=-Lx^{(k+1)}-Ux^{(k)}+b，然后整理可得：

$x^{(k+1)}=-(D+L)^{-1}Ux^{(k)}+(D+L)^{-1}b$

G-S迭代的矩阵形式为:
$x(n)=\begin{cases}
    x_{(0)}=(x_1^{(0)},x_2^{(0)},x_3^{(0)},...,x_n^{(0)})^T\\
  x^{(k+1)}=-(D+L)^{-1}Ux^{(k)}+(D+L)^{-1}b
     \end{cases}$

那么为了求解肯定要求迭代结果是收敛的，只有这样才能判断是不是能通过迭代求解，所以需要判断是否收敛，有下面几种定理：                       
a. 若线性方程组$Ax=b$的系数矩阵$A$为对称正定矩阵，则此线性方程组G-S迭代收敛。         
b. 若$A$为严格对角占优矩阵或不可约弱对角占有矩阵，则雅可比和G-S迭代法均收敛。          

所以判断迭代收敛的方法如下：        
a. 根据方程组的系数矩阵$A$的特点判断        
b. 可根据迭代矩阵的范数判断，注意是迭代矩阵不是系数矩阵，如雅可比的迭代矩阵可以理解$-D^{-1}(L+U)$            
c. 根据迭代矩阵的谱半径（特征值模的最大值）判断    

根据定理，迭代法是否收敛等价于迭代矩阵的谱半径是否$<1$，也就是最大特征值是否$<1$，如下一个例子：   

$\begin{cases}
    x_1+2x_2-2x_3=1\\
    x_1+x_2+x_3=2\\
    2x_1+2x_2+x_3=3
     \end{cases}$

其系数矩阵
$$
 D=\left[
 \begin{matrix}
    1 &  &  \\
    & 1 &  \\
    &  & 1  
  \end{matrix}
  \right]
$$

$$
 L=\left[
 \begin{matrix}
    0 & 0 & 0 \\
    1 & 0 & 0 \\
    2 & 2 & 0  
  \end{matrix}
  \right]
$$

$$
 U=\left[
 \begin{matrix}
    0 & 2 & -2 \\
    0 & 0 & 1 \\
    0 & 0 & 0  
  \end{matrix}
  \right]
$$

雅可比迭代的迭代矩阵，
$$B=-D^{-1}(L+U)=\left[
 \begin{matrix}
    0 & -2 & 2 \\
    -1 & 0 & -1 \\
    -2 & -2 & 0  
  \end{matrix}
  \right]$$
其特征方程为：
$$ \left|\begin{array}{cccc} 
    \lambda I-B 
\end{array}\right|=\left|\begin{array}{cccc} 
    \lambda & 2 & -2 \\ 
    1 &\lambda  & 1\\ 
    2 & 2 & \lambda 
\end{array}\right|=\lambda ^3=0 $$
所以$\rho(B)=0<1$，因此该雅可比迭代收敛。


如果是$G-S$迭代，则迭代矩阵为：   
$$G=-(D+L)^{-1}U=\left[
 \begin{matrix}
    0 & -2 & 2 \\
    0 & 2 & -3 \\
    0 & 0 & 2  
  \end{matrix}
  \right]$$

其特征方程为：

$$ \left|\begin{array}{cccc} 
    \lambda I-G 
\end{array}\right|=\left|\begin{array}{cccc} 
    \lambda & 2 & -2 \\ 
    0 &\lambda -2  & 3\\ 
    0 & 0 & \lambda -2
\end{array}\right|=\lambda (\lambda-2)^3=0 $$
这里$\lambda$可以等于2，所以 $\rho(B)=2>1$，G-S迭代发散。

1. 最后一步是把密度应用到速度场上，和算扩散一样，可以设置一个线性系统，并用*Gauss-Seidel*结算，然而得出线性方程基于速度，所以计算起来比较麻烦。但是有一个更有效的方法，这个方法的核心是把密度看成是一系列粒子（因为移动密度更加方便），所以可以很容易的在速度场中跟踪粒子，假如把每个网格单元的中心看成是粒子并且在速度场中追踪它，问题是如何把这些粒子转换成网格数据，更好的方法是找到单个时间步中最终精确位于网格单元中心的粒子，这些粒子所携带的密度大小可以简单的从四个最近的网格单元插值得到。从两个网格单元开始，一个网格包含上一个时间步的密度值，另一个包含新的密度值，对于后者的的网格单元，我们通过速度场追踪网格单元的中心位置。然后就可以从前一个密度值线性插值到当前网格单元。

如下代码：
```cpp
void advect(int N, int b, float* d, float* d0, float* v, float dt)
{
  int i, j, i0, j0, i1, j1;
  float x, y, s0, t0, s1, t1, dt0;
  dt0 = dt * N;
  for(i=1; i<=N; i++){
    for(j=1; j<=N; j++){
      x = i-dt0*u[IX(i,j)];
      y=j-dt0*v[IX(i,j)];
      if(x<0.5f>)x=0.5f;
      if(x>N+0.5f)x=N+0.5f;
      i0=(int)x;
      i1=i0+1;

      if(y<0.5f>)y=0.5f;
      if(y>N+0.5f)y=N+0.5f;
      j0=(int)y;
      i1=j0+1;

      s1=x-i0;
      s0=1-s1;
      t1=y-j0;
      t0=1-t1;

      d[IX(i,j)]=s0*(t0*d0[IX(i0,j0)]+t1*d0[IX(i0,j1)])+
                s1*(t0*d0[IX(i1,j0)]+t1*d0[IX(i1,j1)]);
    }
  }
  set_bnd(N,b,d);
}
```

上述的方法就是对密度求解的表述，可以比较容易的把三个步骤结合到一块，密度初始值为x0数组，如下代码：
```cpp
void dens_step(int N, float * x, float * x0, float * u, float * v, float diff, float dt)
{
  add_source(N, x, x0, dt);
  SWAP(x0, x); diffuse(N, 0, x, x0, diff, dt);
  SWAP(x0, x); advect(N, 0, x, x0, u, v, dt);
}

//交换两个数组指针
#define SWAP(x0, x) {float* tmp=x0; x0=x; x=tmp;}
```

## Envolving Velocities
速度随着时间变化主要有三个原因：力的添加、粘性扩散、对流（self-advection），对流可以理解为速度场沿自身移动。但是可以使用之前的密度解算器并且应用的速度场上。假设力场存在数组u0和v0中，可以有如下代码：
```cpp
void vel_step(int N, float * u, float * v, float * u0, float * v0, float visc, float dt)
{
  add_source(N, u, u0, dt);
  add_source(N, v, v0, dt);
  SWAP(u0, u);
  diffuse(N, 1, u, u0, visc, dt);
  SWAP(v0, v);
  diffuse(N, 2, v, v0, visc, dt);
  project(N, u, v u0, v0);
  SWAP(u0, u);
  SWAP(v0, v);
  advect(N, 1, u, u0, u0, v0, dt);
  advect(N, 2, v, v0, u0, v0, dt);
  project(N, u, v, u0, v0);
}
```

注意这里和密度结算的差别，这里多了一个*project()*方法，这在视觉上迫使气流具有许多涡流，这个涡流会产生逼真的涡旋气流。为了保证质量守恒，使用了叫做*霍齐分解*（Hodge decomposition）的纯数学计算结果：每个速度场是质量守恒场和梯度场和。注意，质量守恒场如何具有良好的旋涡状涡流，通常是我们想要的场类型,如下图上半部分。另一方面梯度场是下图上半部分右边，这种情况是最糟糕的情况，在某些点的流动都是向外或者向内，实际梯度场表示一些高度函数的最陡下降方向。一旦有了高度场，我们就可以从速度场中减去梯度来得到质量守恒场，如下图下半部分。

![image](https://github.com/haiaimi/UnrealShaders/blob/master/RenderPictures/Real-Time%20Fluid%20Simulation/ConstructVelocityField.png)

计算projection这里不会涉及很多数学细节，仅仅涉及一些计算高度场的线性系统也被称为*Poisson equation*，这个系统也是稀疏的，可以使用之前计算密度、扩散的G-S方程计算，下面是相关的代码：
```cpp
void project(int N, float * u, float * v, float * p, float * div)
{
  int i, j, k;
  float h;
  h = 1.0/N;
  for(i=1; i<=N; i++) {
    for(j=1; j<=N; j++) {
      div[IX(i, j)]=-0.5f*h*(u[IX(i+1, j)]-u[IX(i-1, j)]+
                              v[IX(i, j+1)]-v[IX(i, j-1)]);
      p[IX(i,j)]=0;
    }
  }
  set_bnd(N, 0, div);
  set_bnd(N, 0, p);


  for ( k=0 ; k<20 ; k++ ) {
    for ( i=1 ; i<=N ; i++ ) {
      for ( j=1 ; j<=N ; j++ ) {
        p[IX(i,j)] = (div[IX(i,j)]+p[IX(i-1,j)]+p[IX(i+1,j)]+
                      p[IX(i,j-1)]+p[IX(i,j+1)])/4;
      }
    }
    set_bnd ( N, 0, p );
  }
  for ( i=1 ; i<=N ; i++ ) {
    for ( j=1 ; j<=N ; j++ ) {
      u[IX(i,j)] -= 0.5*(p[IX(i+1,j)]-p[IX(i-1,j)])/h;
      v[IX(i,j)] -= 0.5*(p[IX(i,j+1)]-p[IX(i,j-1)])/h;
    }
  }
  set_bnd ( N, 1, u ); 
  set_bnd ( N, 2, v );
}
```
上面可以看到调用这个project()方程两次，这样做是为了保持质量守恒，从而使advect()更加精确。
流体模拟都会有一个边界，相当于一个墙，流体不能出这个墙，这意味着纵向的墙壁横向分量是0，相反同理。边界处理的代码如下：
```cpp
void set_bnd(int N, int b, float * x){
  int i;

  for(i=1; i<=N; i++){
    x[IX(0,i)] = b==1 ? -x[IX(1, i)]:x[IX(1, i)];
    x[IX(N+1,i)] = b==1 ? -x[IX(N, i)]:x[IX(N, i)];
    x[IX(i,0)] = b==2 ? -x[IX(i, 1)]:x[IX(i, 1)];
    x[IX(i,N+1)] = b==2 ? -x[IX(i, N)]:x[IX(i, N)];
  }
  x[IX(0 ,0 )] = 0.5*(x[IX(1,0 )]+x[IX(0 ,1)]);
  x[IX(0 ,N+1)] = 0.5*(x[IX(1,N+1)]+x[IX(0 ,N )]);
  x[IX(N+1,0 )] = 0.5*(x[IX(N,0 )]+x[IX(N+1,1)]);
  x[IX(N+1,N+1)] = 0.5*(x[IX(N,N+1)]+x[IX(N+1,N )]);
}
```

所以总体流体模拟的代码流程如下：
```cpp
get_from_UI(dens_prev, u_prev, v_prev);
vel_step(N, u, v, u_prev, v_prev, visc, dt);
dens_step(N, dens, dens_prev, u, v, diff, dt);
draw_dens(N, dens);
```

# GPU Gems

GPU Gems中有篇对应的二维流体模拟文章，描述的更加详细一些，并且更适合GPU计算。

## 涉及公式

### Equation 1
$\frac{\partial u}{\partial t}=-(u\cdot\nabla)u-\frac{1}{\rho}\nabla p + \nu{\nabla}^2u+F$

这里$F$为外力了，同时这里实际上是两个式子，因为这里的速度有两个分量。

$(u\cdot\nabla)u$：*Advection*（对流）项。

$\frac{1}{\rho}\nabla p$：*Pressure*（压力项），当有力施加到流体时，就会推动施力点的附近的分子，所以会产生压力，导致流体的加速度发生变化，从而导致流体速度的变化。这里的$\nabla$表示求梯度。

$\nu{\nabla}^2u$：*Diffusion*（扩散）项，不同流体的粘度不一样，扩散速度也会不一样。$\nabla ^2u$为$u$的[拉普拉斯算子](https://zh.wikipedia.org/wiki/%E6%8B%89%E6%99%AE%E6%8B%89%E6%96%AF%E7%AE%97%E5%AD%90)。

$F$：*External Forces*，施加到流体的外力项。

上面$\nabla$符号有三个不同的用法，分别是$gradient$（梯度），$divergence$（散度），$Laplacian$（拉普拉斯算子）,定义分别如下：

  | Operator | Definition | Finite Difference form |
  |:------------:|:--------------:|:-----------:|
  | Gradient   | $\nabla p=\lbrace \frac{\partial p}{\partial x}, \frac{\partial p}{\partial y} \rbrace$  |$\frac{p_{i+1,j}-p_{i-1,j}}{2\partial x}, \frac{p_{i,j+1}-p_{i,j-1}}{2\partial y}$ |
  | Divergence | $\nabla \cdot u=\frac{\partial p}{\partial x}+\frac{\partial p}{\partial y}$ | $\frac{u_{i+1,j}-u_{i-1,j}}{2\partial x}+ \frac{v_{i,j+1}-v_{i,j-1}}{2\partial y}$ |
  | Laplacian  | $\nabla ^2p=\frac{\partial ^2 p}{\partial x^2}+\frac{\partial ^2 p}{\partial y^2}$| $\frac{p_{i+1,j}-2p_{i,j}+p_{i-1,j}}{(\partial x)^2}+ \frac{p_{i,j+1}-2p_{i,j}+p_{i,j-1}}{(\partial y)^2}$ |

散度是比较重要的物理量，表示在一定密度下离开指定区域的速率。在NS等式中被应用到流体的速度上。

标量场的梯度是向量场，向量场的散度是标量。把散度应用到梯度结果上就是*拉普拉斯*算子。

### Equation 2
$\nabla \cdot u=0$
表示$u$的散度为0。由于是模拟不可压缩流体，所以等式2右侧为0。

### Equation 3
 $\nabla ^2p=\frac{p_{i+1,j}+p_{i-1,j}+p_{i,j+1}+p_{i,j-1}-4p_{i,j}}{(\partial x)^2}$

 这里假设网格单元为正方形（$d_x=d_y$），这样就可以把拉普拉斯算子简化成这样。


 ### Equation 4
$w=u+\nabla p$     
霍齐分解, 定义$D$为模拟流体的空间区域（这里为平平面），$n$为该区域的法线方向，$w$为该空间上的一个向量场，$u$散度为0，并且和$\partial D$平行，所以$u \cdot n=0$。所以任何一个向量场可以被分解为两个向量场之和，分别是无散度向量场和标量场的梯度（向量场）。

### Equation 5
$u=w-\nabla p$       
要解连续方程，需要保证每一个Time Step速度是零散度，所以就转化成上面的式子。

### Equation 6
$\nabla \cdot w=\nabla \cdot (u+\nabla p)=\nabla \cdot u+\nabla ^2p$      
对两边进行散度计算

### Equation 7
$\nabla ^2p=\nabla \cdot w$    
由于$u$的散度是0，即$\nabla \cdot u=0$，所以可以化简如上。

### Equation 8
$\frac{\partial u}{\partial t}=P(-(u\cdot \nabla)u+v\nabla ^2u+F)$

这里的$P$表示投影操作，上式就是对NS等式两边进行投影操作，可见经过投影后，压力项消去。
要求出$u$，首先需要求出$w$，这里引入投影的概念，主要是把非0散度向量场投影到自身0散度向量中。所以可得：

$Pw=Pu+P(\nabla p)$，由于根据定义投影是把非0散度投到0散度上，所以$Pw=Pu=u$，因此$P(\nabla p)=0$。并且散度为0的向量场，其导数也是。

### Equation 9
$q(x, t+\partial t)=q(x-u(x,t)\partial t, t)$        
这是对流计算公式，一般正常计算流体中粒子位置的方法就是通过步进的方法，如下：$r(t+\partial t)=r(t)+u(t)\partial t$，$r$表示位置，可见就是常规的根据的速度及deltaTime求位置的方法，但是这种方法不够稳定，也不适合在GPU上实现。所以使用上式，$q$代表数量，可以是速度、密度、温度等等，就是回到之前粒子在每个网格单元，然后复制到当前网格进行计算，一个比较取巧的方法。

### Equation 10
$\frac{\partial u}{\partial t}=v\nabla^2u$  
粘性方程

### Equation 11
$(I-v\partial t\nabla ^2)u(x, t+\nabla t)=u(x,t)$         
*Equation 10*是粘性方程，它会影响流体速度，可以表示如下：      
$u(x, t+\partial t)=u(x,t)+v\partial t\nabla ^2u(x,t)$，但是同样这种方法不稳定，所以依然需要像*Equation 9*的方法，得出方程如上。$I$是单位矩阵。

### Equation 12
$x_{i,j}^{(k+1)}=\frac{x_{i-1,j}^{(k)}+x_{i+1,j}^{(k)}+x_{i,j-1}^{(k)}+x_{i,j+1}^{(k)}+\alpha b_{i,j}}{\beta}$    
上式用于解泊松等式，我们有两个泊松等式需要解，并且都可以用上式表示，1是*泊松-压力*等式（Equation 7），2是*粘性* 等式（Equation 10），这里主要是使用迭代法求得近似解。在*泊松-压力*等式中，$x$表示$p$，$b$表示$\nabla \cdot w$，$\alpha =-(x)^2$， $\beta = 4。在*粘性*等式中，$x$,$b$表示$u$。$$\alpha=\frac{x^2}{t}$，$\beta = 4+\alpha$。

泊松等式是一个$Ax=b$形式的矩阵等式，$x$是我们需要求的值，这里是$p$或者$u$，$b$是一个常量向量，$A$是一个矩阵，这里隐式的表示*拉普拉斯算子*$\nabla ^2$，所以这里不需要存储完整的矩阵。

迭代初始的值$x^{[0]}$一般取一个大致猜想的值，$x^{[k]}$会逐渐接近准确值，最简单的迭代方式就是雅可比迭代。

泊松压力方程的正确解需要纯*诺伊曼边界条件*：$\frac{\partial p}{\partial n}=0$，在边界处垂直于边界的压力的变化率为0。

### Equation 13
$S=P \circ F\circ D\circ A$      
定义S为等式的解，A为*Advect*，D为*Diffusion*，F为*Force*，P为*Projection*。
大致流程如下：
```cpp
  u=advect(u);
  u=diffuse(u);
  u=addForce(u);

  //Apply projection operator to the result
  p=computePressure(u);
  u=subtractPressureGradient(u, p);
```

```cpp
// Advection
void advect(float2 coords   : WPOS,   // grid coordinates     
            out float4 xNew : COLOR,  // advected qty     
            uniform float timestep,             
            uniform    float rdx,        // 1 / grid scale     
            uniform    samplerRECT u,    // input velocity     
            uniform    samplerRECT x)    // qty to advect
{  
  // follow the velocity field "back in time"     
  float2 pos = coords - timestep * rdx * f2texRECT(u, coords); 
  // interpolate and write to the output fragment  
  // f4texRECTbilerp is bilinear lerp function 
  xNew = f4texRECTbilerp(x, pos); 
} 
```

```cpp
// Viscous Diffusion
void jacobi(half2 coords   : WPOS,   // grid coordinates     
            out half4 xNew : COLOR,  // result 
            uniform    half alpha,            
            uniform    half rBeta,   // reciprocal beta     
            uniform samplerRECT x,   // x vector (Ax = b)     
            uniform samplerRECT b)   // b vector (Ax = b) 
  {    // left, right, bottom, and top x samples    
        half4 xL = h4texRECT(x, coords - half2(1, 0));   
        half4 xR = h4texRECT(x, coords + half2(1, 0));   
        half4 xB = h4texRECT(x, coords - half2(0, 1));   
        half4 xT = h4texRECT(x, coords + half2(0, 1)); // b sample, from center     
        half4 bC = h4texRECT(b, coords); 
        // evaluate Jacobi iteration  
        xNew = (xL + xR + xB + xT + alpha * bC) * rBeta; 
  } 
```

```cpp
// Projection

// Divergence 
void divergence(half2 coords : WPOS,   // grid coordinates     
                out half4 div : COLOR,  // divergence     
                uniform half halfrdx,   // 0.5 / gridscale     
                uniform samplerRECT w)  // vector field
 {   
        half4 wL = h4texRECT(w, coords - half2(1, 0));   
        half4 wR = h4texRECT(w, coords + half2(1, 0));   
        half4 wB = h4texRECT(w, coords - half2(0, 1));   
        half4 wT = h4texRECT(w, coords + half2(0, 1)); 
        div = halfrdx * ((wR.x - wL.x) + (wT.y - wB.y)); 
 } 
 // Gradient Subtraction 
 void gradient(half2 coords   : WPOS,   // grid coordinates     
               out half4 uNew : COLOR,  // new velocity    
               uniform half halfrdx,    // 0.5 / gridscale     
               uniform samplerRECT p,   // pressure     
               uniform samplerRECT w)   // velocity 
  {  
        half pL = h1texRECT(p, coords - half2(1, 0));   
        half pR = h1texRECT(p, coords + half2(1, 0));  
        half pB = h1texRECT(p, coords - half2(0, 1));   
        half pT = h1texRECT(p, coords + half2(0, 1)); 
        uNew = h4texRECT(w, coords);   
        uNew.xy -= halfrdx * half2(pR - pL, pT - pB);
  } 
```

### Equation 14
$\frac{u_{0,j}+u_{1,j}}{2}=0, j \in [0,N]$

用于流体速度边界计算，$N$是流体网格尺寸，为了满足等式，需要使$u_{0,j}=-u_{1,j}$。

### Equation 15
$\frac{p_{1,j}-p_{0,j}}{\delta x}=0$

流体压力边界等式，

```cpp
void boundary(half2 coords : WPOS,    // grid coordinates     
              half2 offset : TEX1,    // boundary offset     
              out half4 bv : COLOR,   // output value     
              uniform half scale,     // scale parameter     
              uniform samplerRECT x)  // state field, velocity or pressure field
{   
  bv = scale * h4texRECT(x, coords + offset);
} 
```

*offset*参数用于将纹理坐标调整到边界内的坐标。*scale*参数用于缩放复制到边界的值，对于速度来说，*scale*设为-1，对于*pressure*来说，设为1。

### Equation 16
$\frac{\partial d}{\partial t}=-(u \cdot \nabla)d$

要更好的表现模拟的流体，需要添加一些东西使其更加明显，如颜料，用标量表示其浓度。上面把对流加到标量场。

### Equation 17
$\frac{\partial d}{\partial t}=-(u \cdot \nabla)d+\nu \nabla ^2d+S$

把扩散加入到上述标量场中。

### Equation 18
$f_{buoy}=\sigma (T-T_0)\hat{j}$

为了模拟温度的影响，可以加入浮力因素，$T$是温度场，$T_0$是环境温度。

### Equation 19
$f_{buoy}=(-kd+\sigma (T-T_0))\hat{j}$

模拟烟和云，上式涉及两个变量，$d$烟的密度，$T$温度。

### Equation 20
$f_{vc}=\xi (\Psi \times \omega)\delta x$

这个公式用于Vorticity Confinement（涡流限制）：
1. 首先需要计算涡流强度，也就是速度场的*旋度*，$\omega$。
2. 然后通过旋度计算涡流强度向量场$\eta$，$\eta = \nabla|\omega|$，在实际中就是*float2(abs(Top.x) - abs(Bottom.x), abs(Right.x) - abs(Left.x))*注意上面4个不同位置值都是涡流强度标量。
3. 求$\eta$的单位向量$\Psi$。
4. 最后计算$f_{vc}$，$\xi$是自己设置的系数，该值越大，涡流也就越多越密集。
5. 然后把$f_{vc}加到当前的速度场上，从而施加Vorticity Confinement。$

## Fluid Simulation 3D
  前面都是关于2D流体的模拟，拓展成3维也不是很复杂，就是多一个维度的计算，速度场变成三维的，在解泊松等式的时候就需要涉及6个速度场、压力场的值，结果也可能需要3D纹理来表示，但是也可以Atlas，用一张大图表示多张，如512x512可以表示成64x64x64的3维速度场，可能3维的流体渲染会有很大不同。
  
  下面就是Gpu Gems里3维流体模拟的内容，3维包括烟、水等。

### Solving for Velocity
#### Improving Detail
  之前的二维流体模拟使用的是*semi-Lagrangian*（半拉格朗日）对流，虽然这个方法可以解决TimeStep过大导致模拟错误的问题，但是也会产生不正常的平滑从而，导致失真，使水看起来十分粘稠，为了提高流体模拟的准确度，这里使用*MacCormack*的方案：就是操作两个中间*semi-Lagrangian*对流步骤，给定对流的数量和对流方案，如下操作：

  $\hat{\phi}^{n+1}=A(\phi ^n)$         
  $\hat{\phi}^{n}=A^R(\hat{\phi}^{n+1})$         
  ${\phi}^{n+1}=\hat{\phi}^{n+1)}+\frac{1}{2}(\phi^n-\hat{\phi}^n)$

  上式中$\phi^n$是对流的数量，$\hat{\phi}^{n+1}$和$\hat{\phi}^n$是中间量，$\phi^{n+1}$是最终的对流量，$A^R$表示逆过来的对流。不同于标准的拉格朗日方案，*MacCormack*方案不是无条件稳定，所以必须要对$\phi^{n+1}$有限制，就是取采样点周围的八个点，然后把值限制在这8个值的最大、最小中。
  
  在GPU中使用更优方案通常比直接提分辨率效果提升更大，毕竟计算相比于带宽要便宜很多。

### Solid-Fluid Interaction
  流体肯定就离不开交互，下面有两个方法。

  1. 可以把障碍物大概当成一些基本形状如Box和Ball，然后在基本模型的区域中加上障碍物的平均速度。
  2. 为了进行精准障碍模拟，可以直接用精确的模型作为障碍物。

  #### Dynamic Obstacles
  在支持动态障碍物的情况下，需要改变流体区域，在这里障碍使用*Inside-Ourside*（内外）体素表示。此外在障碍物临近流体的边界保持障碍物体素的表示。这里是*free-slip*边界条件，所以流体和固体的边界的法向速度是相同的。（和之前的二维流体*no-slip*有所区别）。

  $u\cdot n=u_{solid}\cdot n$

  *free-slip*的边界条件也会影响求解压力的过程，压力计算要满足下面的要求下面的等式：

  $\frac{\Delta t}{\rho \Delta x^2}(\mid F_{i,j,k}\mid p_{i,j,k}-\sum_{n\in F_{i,j,k}}p_n)=-d_{i,j,k}$

 * $\Delta t$：是流体模拟的TimeStep
 * $\Delta x$：可以理解为流体模拟格子的大小
 * $p_{i,j,k}$：是坐标为(i, j, k)格子中的压力值
 * $d_{i,j,k}$：离散的速度场散度
 * $F_{i,j,k}$：是一系列相邻于(i, j, k)格子的indices

上面等式就是考虑了固体边界的$\nabla ^2p=\nabla \cdot w$*压力-泊松*等式的离散形式。同样重要的是使用固体-流体边界的速度计算$d_{i,j,k}$。
在实际中会在周围采样压力值时会检测该点是不是固体障碍，如果是就使用中心单元的压力值来代替相邻单元压力值。简而言之就是忽略固体单元对上式的影响。在周围采样速度的时候，也要检查是否在障碍物中，如果是就直接用障碍物速度的值代替速度场的速度值。

由于不能一直保证Possion方程的收敛性，所以在压力投影后需要执行自由滑移边界条件，所以这里就需要计算垂直于边界的障碍物速度，这个速度会代替流体速度场中的速度的一个分量，大致就是如下图：

![image](https://github.com/haiaimi/UnrealShaders/blob/master/RenderPictures/Real-Time%20Fluid%20Simulation/Fluid3DFreeSlipBoundaryCondition.png)
灰色部分就是表示障碍物的速度，黄色部分就是流体速度场速度，可以看到右边的流体速度横向分量使用了障碍物速度的垂直于边界的分量。

  #### Voxelization
  为了表示出障碍物，和障碍物边界附近单元的固体速度。这里需要*Inside-Outside*和*Obstacle Velocity*纹理。

  #### Inside-Outside Voxelization
  这里就是使用*stencil shadow vloumes algorithm*。就是用正交投影为三维纹理每一片渲染一次，近平面就是当前片的位置。渲染几何体的时候，使用*Stencil Buffer*初始化为0，然后设置背面就增加，正面就减少，这样在模型内部的部分就肯定不为0。这样就会有三种情况：非0就是障碍物内部，0就是在障碍物外部，还有一种就是在内部但是靠近边界。

  #### Velocity Voxellization
  还要计算每个障碍物边界单元的速度，简单的方法就是计算顶点当前帧和上一帧位置差，然后根据TimeStep求速度，如下：
  $v_i=\frac{p_i^n-p_i^{n+1}}{\Delta t}$

  但是上面只是知道每个顶点的速度，我们要知道每个体素点的速度，所以其次还要计算任何单元包含一片表面三角形的内插速度，这次必须确定每个三角形与当前切片的交集。相交可能是线段、三角形、点，如果相交是线段，那么就使用一个加宽的四边形代替，所以除了需要两个线段的端点，还需要两个偏移量，$w$表示线段的宽，大小就是纹素对角线长度，$N$表示宽的方向，垂直于三角形。然后就可以根据线性插值表示每个端点的速度值，这样就可以得到线段上每个点的速度。

  这里会使用几何着色器来构建这个四边形（实际上是两个三角片），经过插值后就可以在PS中的到对应位置得速度。

  #### Optimizing Voxelization
  1. 从上面可以得知，体素化会产生大量的DrawCall，这里可以使用*Stream Output*的优化方法，*Stream Output*允许在体素化变形网格时缓存变换后的顶点，而不是为每一片重新计算转换。
  2. 此外还可以使用DrawInstance一次绘制所有的片，并且可以根据Instance_ID指定对应的Target。
  3. 可以使用低面数的模型，流体模拟实际上可以忍受这些误差。
  4. 在一些情况下如只是进行一些旋转、位移的基本转换，可以进行预计算，然后在运行期只需要转换就可以。

### Smoke
  和之前的二维流体一样，直接显示速度场并不能很好的描述流体，所以需要带入一些被流体速度带动的*quantities*，如墨水，这个额外的数据同样也需要有对流操作，如下：
  $\frac{\partial \phi}{\partial t}=-(u\cdot\nabla)\phi$。为了很好的模拟出烟雾的效果，在每一帧的颜色纹理中注入一个三维高斯"Splat"作为烟的源头，同时需要满足，温度高的的上升，温度低的下沉，所以也要追踪流体温度T，同样也是由速度场进行对流操作，不同于颜色的是，温度值对流体动力学有影响，用浮力来表示：
  
  
  $f_{buoyancy}=\frac{Pmg}{R}(\frac{1}{T_0}-\frac{1}{T})z$。

  $P$：压力       
  $m$：气体的摩尔质量     
  $g$：重力加速度   
  $R$：全局气体常数      
  $T_0$：环境温度或者室内温度     
  $T$：当前的温度      
  $z$：标准化的向上方向的向量

  浮力会被当成外力来施加到速度场中，这和之前的二维流体模拟一样。

### Fire
  火焰和之前的流体并没有什么不同，只不过这里会存储额外的量，称为*reaction coordinate*（反应坐标），用于记录气体被点燃后经过的时间，如果*reaction coordinate*的值等于1表示刚被点燃，小于0表示已经完全燃烧完，这些值的演变可以被解释为下面的等式： 

  $\frac{\partial}{\partial t}Y=-(u\cdot \nabla)Y-k$

  大致意思就是*reaction coordinate*会通过流体对流，并且每个TimeStep以一个常数$k$的速度衰减，实际上$k$就是被加到对流的计算过程中。*reaction coordinate*不会对流体动力学产生影响，只是后面会用于渲染。

### Water
  水的模拟和上面所讨论都不太一样，在之前的烟和水是注重于体积中的密度变化，但是水是注重于空气和液体的界限。所以这里需要一些方法来表示这个界面，水又是如何通过速度来推动的。

  *Level set*方法是一个比较流行的液体表面表示法，并且很适合GPU实现，它每个网格单元只需要一个标量值，在*level set*中，每个网格单元记录的是到水面的最短有符号距离，所以如果这个值<0那么就在水中，>0就是在空气中，等于0就是在水面上。由于对流不会保留*level set*的距离场特型，因此通常会重新初始化*level set*，就是为了确保每个单元格存储的是距离水面最短距离。但是不会使用*level set*来简单的定义曲面，所以在实时渲染中不进行重新初始化也可以有不错的效果。

  *level set*和前面的颜色、温度一样，也是受对流影响，也影响着流体动力学。实际上*level set*就是简单定义了流体域，在水、空气的简单模型中，空气对水的影响忽略不计，这意味着液体外部的压力值设为0，只改变液体内部的压力值。为了确保只处理流体单元，检查相关着色器中*level set*纹理的值，如果$f$大于一定的阈值就在单元格处进行遮罩计算。
