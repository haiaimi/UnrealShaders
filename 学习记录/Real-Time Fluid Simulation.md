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
通常认为整体格子的边长为1，那么每个小格子的宽是*h=1/N*，因为模拟就是速度和密度格子的一系列Snapshots，所以Snapshots之间的时间差就是*dt*。
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

2. 第二步解释了以*diff*扩散的可能，当*diff>0*时密度将在小格子间扩散，首先考虑单一的小格子，在这种情况下这个小格子将只向周围的4个小格子进行密度交换。一个可能的扩散解算方法是在每个小格子进行简单的计算交换量，然后累加到已有值，如下代码：
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
这里的set_bnd()方法是用来设置边界的小格子。但是上面的计算方法实际中不管用，不稳定，对于较大的扩散率密度开始震荡，变为负数并最终发散，使模拟无效。所以需要考虑一个稳定的方法，这个方法的基本思想就是找到在时间向后时开始时的密度，如下代码：
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
b. 可根据迭代矩阵的范数判断
c. 根据迭代矩阵的谱半径（特征值模的最大值）判断

1. 最后一步是把密度应用到速度场上，和算扩散一样，可以设置一个线性系统，并用*Gauss-Seidel*结算，然而得出线性方程基于速度，所以要进行trick，

