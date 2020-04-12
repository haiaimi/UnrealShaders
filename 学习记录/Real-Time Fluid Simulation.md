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