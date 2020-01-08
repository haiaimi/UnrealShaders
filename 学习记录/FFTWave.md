## 使用FFT进行海洋水体模拟

### 形体
下面内容主要是FFT水体的形体相关内容

#### 傅里叶
首先需要了解一下快速傅里叶，首先为什么需要复数表示、

#### 傅里叶与海面模拟

1. 海面的高度信号定义为XY平面上的空域信号 $h(x,y)$，这个信号必定是大量正弦信号叠加，所以频谱可以表示为 $\tilde{h}(\omega_x,\omega_y)$，使用傅里叶求出$h(x,y)$就能得到高度，在计算机中肯定是计算离散傅里叶变换(Inverse Discrete Fourier Transform(IDFT))。

2. 海面的IDFT模型
    海面高度由该公式给出：$h(\vec{x},t)=\sum_{\vec{k}}\tilde{h}(\vec{k},t)e^{i\vec{k}\cdot\vec{x}}$

    $\vec{x}=(x,y)$是空域坐标   
    $\vec{k}=(k_x,k_y)$是频域坐标  
    $\tilde{h}(\vec{k},t)$就是频谱，频谱和时间相关，那么高度函数$h(\vec{x},t)$也和时间有关。

    $e^{i\vec{k}\cdot\vec{x}}$中$\vec{k}$和$\vec{x}$是点积，所以$e^{i\vec{k}\cdot\vec{x}}=e^{i(k_xx+k_yy)}$，$k_x$和$k_y$分别是x，y分量的频率。高度就是对所有频域坐标点 $\vec{k}$的求和。

    $\vec{k}$在频率平面上以原点为中心每隔$\frac{2\pi}{L}$（L为海面patch尺寸）取一个点，有NxN个点：

    $k_x=\frac{2\pi{n}}{L}$，$n\in\{-\frac{N}{2},-\frac{N}{2}+1,...,-\frac{N}{2}-1\}$

    $k_z=\frac{2\pi{m}}{L}$，$m\in\{-\frac{N}{2},-\frac{N}{2}+1,...,-\frac{N}{2}-1\}$

    $\vec{x}$在xz平面上以原点为中心每隔$\frac{L}{N}$取一个点，有NxN个点：

    $x=\frac{uL}{N}$，$u\in\{-\frac{N}{2},-\frac{N}{2}+1,...,-\frac{N}{2}-1\}$

    $z=\frac{vL}{N}$，$v\in\{-\frac{N}{2},-\frac{N}{2}+1,...,-\frac{N}{2}-1\}$

    从 $k=\frac{2\pi{n}}{L}$公式可以看出，频率是以$L$为周期（只要保证n为整数），那么每个连续的Patch可以无缝衔接。

3. 菲利普频谱（Phillips Speyioctrum）   
    上面可以看出海面高度由频谱计算得出，下面是通常采用的频谱的公式：

    $\tilde{h}(\vec{k},t)=\tilde{h_0}(\vec{k})e^{iw(k)t}+\tilde{h_0^*}(-\vec{k})e^{-iw(k)t}$

    $\tilde{h_0^*}$是$\tilde{h}$的共轭复数。  
    $k$为$\vec{k}$的模。  
    $w(k)=\sqrt{gk}$（$g$是重力加速度）。  
    $\tilde{h_0}(\vec{k})=\frac{1}{\sqrt{2}}(\xi_r+i\xi_i)\sqrt{P_h(\vec{k})}$，$\xi_r$和$\xi_i$是相互独立并且服从均值为0，标准差为1的正态分布。而其中的$P_h(\vec{k})$如下：  
    $P_h(\vec{k})=A\frac{e^{-1}/(kL)^2}{k^4}|\vec{k}\cdot\vec{w}|^2$，$w是风向， $L=\frac{V^2}{g}$（注意这与前面的海面Patch不是同一个）$V$是风速。

4. 法线  
    计算法线需要推出法线的解析式。  
    高度：   
    $h(\vec{x},t)=\sum_{\vec{k}}\tilde{h}(\vec{k},t)e^{i\vec{k}\cdot\vec{x}}$  
    空间梯度：  
    $\nabla h(\vec{x},t)=\sum_{\vec{k}}\tilde{h}(\vec{k},t)\nabla e^{i\vec{k}\cdot\vec{x}}$ ，$\tilde{h}(\vec{k},t)$ 不含由 $\vec{x}$ 所以不含空间变元，那么频谱函数就可以提出来，（$\nabla$是梯度符号，这里可以理解为偏导数，之前龙书dx11余弦波水就是计算偏导数来获取法线）。   
    $\nabla e^{i\vec{k}\cdot\vec{x}}=(\frac{\delta{e^{i(k_x*x+k_y*y)}}}{\delta{x}}, \frac{\delta{e^{i(k_x*x+k_y*y)}}}{\delta{y}})$    
    =$(e^{i(k_x*x+k_y*y)}*ik_x,e^{i(k_x*x+k_y*y)}*ik_y)$  
    =$e^{i(k_x*x+k_y*y)}*i(k_x,k_z)$  
    =$i\vec{k}e^{i\vec{k}\cdot\vec{x}}$    
    最终得出 $\nabla h(\vec{x},t)=\sum_{\vec{k}}i\vec{k}\tilde{h}(\vec{k},t)e^{i\vec{k}\cdot\vec{x}}$

    所以法线可以得出 $\vec{N}=normalize(-\nabla h_x(\vec{x},t),-\nabla h_y(\vec{x},t),1)$ 因为对$x,y$分量进行求导，可以求出这两个分量的切线的正切值（可以想象对一元二次函数的求导几何表现），这里还使用到了up向量$(0,0,1)$，以up向量为基准。

5. 尖浪
   在IDFT海面，需要挤压操作，公式如下：  
   $\vec{D}(\vec{x},t)=\sum_{\vec{k}}-i\frac{\vec{k}}{k}\tilde{h}(\vec{k},t)e^{i\vec{k}\cdot{\vec{x}}}$   
   $\vec{x'}=\vec{x}+\lambda\vec{D}(\vec{x},t)$

   实际上是$sin$波对$cos$波的挤压，$cos$对$sin$的挤压。挤压过头就会产生穿刺从而就会形成泡沫区域，在数学上就是面元有向面积变为负值，计算这个面积就需要二重积分换元法和雅可比行列式。  
   因为$x'$和$y'$均为以$x$,$y$为变元的二元函数，即$x'=x'(x,y),y'=y'(x,y)$，那么根据二重积分换元法得知变换后的面积为：$dA=\vec{dx'}\times\vec{dy'}=\left|\begin{array}{ccc}
     \frac{\delta{x'}}{\delta{x}}&\frac{\delta{x'}}{\delta{y}}\\
     \frac{\delta{y'}}{\delta{x}}&\frac{\delta{y'}}{\delta{y}}
 \end{array}\right|dxdy$  
    因为$dx,dy$必定为正数，那么面积的正负就取决于雅可比行列式。    
    $J(\vec{x})=\left|\begin{array}{ccc}
        J_{xx}&J_{xy}\\
        J_{yx}&J_{yy}
     \end{array}\right|=\left|\begin{array}{ccc}
     \frac{\delta{x'}}{\delta{x}}&\frac{\delta{x'}}{\delta{y}}\\
     \frac{\delta{y'}}{\delta{x}}&\frac{\delta{y'}}{\delta{y}}
    \end{array}\right|=J_{xx}J_{yy}-J_{xy}J_{yx}$   
    根据$\vec{x'}=\vec{x}+\lambda\vec{D}(\vec{x},t)$可知：  
    $J_{xx}=\frac{\delta{x'}}{\delta{x}}=1+\lambda\frac{\delta{D_x(\vec{x},t)}}{\delta{x}}$  
    $J_{yy}=\frac{\delta{y'}}{\delta{y}}=1+\lambda\frac{\delta{D_y(\vec{x},t)}}{\delta{y}}$  
     $J_{yx}=\frac{\delta{y'}}{\delta{x}}=\lambda\frac{\delta{D_y(\vec{x},t)}}{\delta{x}}$  
     $J_{xy}=\frac{\delta{x'}}{\delta{y}}=\lambda\frac{\delta{D_x(\vec{x},t)}}{\delta{y}}$

     由于$\vec{D}(\vec{x},t)$表达式已知，所以这些偏导数可以求出来，如下计算：  
     $D_y=\sum_{\vec{k}}-i\frac{k_y}{k}\tilde{h}(\vec{k},t)e^{i\vec{k}\cdot{\vec{x}}}$

     $\frac{\delta{D_y}}{\delta_x}=\sum_{\vec{k}}-i\frac{k_y}{k}\tilde{h}(\vec{k},t)e^{i\vec{k}\cdot{\vec{x}}}ik_x$  

     $D_x=\sum_{\vec{k}}-i\frac{k_x}{k}\tilde{h}(\vec{k},t)e^{i\vec{k}\cdot{\vec{x}}}$

     $\frac{\delta{D_x}}{\delta_y}=\sum_{\vec{k}}-i\frac{k_x}{k}\tilde{h}(\vec{k},t)e^{i\vec{k}\cdot{\vec{x}}}ik_y$

     可以看出$\frac{\delta{D_y}}{\delta_x}=\frac{\delta{D_x}}{\delta_y}$。

#### 快速傅里叶（FFT:Fast Fourier Transform）

1. FFT算法以及复杂度   
   标准DFT：  
   $X(k)=\sum_{n=0}^{N-1}x(n)e^{-i\frac{2\pi{k}}{N}n},k\in\{0,1,...,N-1\}$  
   如果直接暴力运算，那么复杂度就是O(N*N)，快速傅里叶使用的是分治思想，如用两个N/2 point DFT去构造一个N point DFT。可以按项数按序号奇偶分开，则有如下定义：  
    $G(k)=\sum_{n=0}^{\frac{N}{2}-1}g(n)e^{-i\frac{2\pi{k}}{\frac{N}{2}}n}=\sum_{n=0}^{\frac{N}{2}-1}x(2n)e^{-i\frac{2\pi{k}}{\frac{N}{2}}n},k\in\{0,1,...,\frac{N}{2}-1\}$  
     $H(k)=\sum_{n=0}^{\frac{N}{2}-1}h(n)e^{-i\frac{2\pi{k}}{\frac{N}{2}}n}=\sum_{n=0}^{\frac{N}{2}-1}x(2n+1)e^{-i\frac{2\pi{k}}{\frac{N}{2}}n},k\in\{0,1,...,\frac{N}{2}-1\}$  

     $X(k)$可以由$G(k)$和$H(k)$表示出来，如下推导：   
     当$k\in\{0,1,...,\frac{N}{2}-1\}$  
     $X(k)=\sum_{n=0}^{N-1}x(n)e^{-i\frac{2\pi{k}}{N}n}$    
     $=\sum_{n=0}^{\frac{N}{2}-1}x(2n)e^{-i\frac{2\pi{k}(2n)}{N}}+\sum_{n=0}^{\frac{N}{2}-1}x(2n+1)e^{-i\frac{2\pi{k}(2n+1)}{N}}$   
     $=\sum_{n=0}^{\frac{N}{2}-1}x(2n)e^{-i\frac{2\pi{k}}{\frac{N}{2}}n}+e^{-i\frac{2\pi{k}}{N}}\sum_{n=0}^{\frac{N}{2}-1}x(2n+1)e^{-i\frac{2\pi{kn}}{\frac{N}{2}}}$  
     $=G(k)+W_N^kH(k)$  

     当$k\in\{\frac{N}{2},\frac{N}{2}+1,...,N-1\}$，令$K=k-N/2$，则$K\in\{0,1,...,\frac{N}{2}-1\}$,如下推导：  
     $X(k)=\sum_{n=0}^{N-1}x(n)e^{-i\frac{2\pi{k}}{N}n}$   
     $=\sum_{n=0}^{\frac{N}{2}-1}x(2n)e^{-i\frac{2\pi{k}(2n)}{N}}+\sum_{n=0}^{\frac{N}{2}-1}x(2n+1)e^{-i\frac{2\pi{k}(2n+1)}{N}}$   
     $=\sum_{n=0}^{\frac{N}{2}-1}x(2n)e^{-i\frac{2\pi{(K+\frac{N}{2})}(2n)}{N}}+\sum_{n=0}^{\frac{N}{2}-1}x(2n+1)e^{-i\frac{2\pi{(K+\frac{N}{2})}(2n+1)}{N}}$   
     $=\sum_{n=0}^{\frac{N}{2}-1}x(2n)e^{-i\frac{2\pi{Kn}}{\frac{N}{2}}}+e^{-i\frac{2\pi{K}}{N}}\sum_{n=0}^{\frac{N}{2}-1}x(2n+1)e^{-i\frac{2\pi{Kn}}{\frac{N}{2}}}$  
     $=\sum_{n=0}^{\frac{N}{2}-1}x(2n)e^{-i\frac{2\pi{Kn}}{\frac{N}{2}}}-e^{-i\frac{2\pi{K}}{N}}\sum_{n=0}^{\frac{N}{2}-1}x(2n+1)e^{-i\frac{2\pi{Kn}}{\frac{N}{2}}}$  
     $=\sum_{n=0}^{\frac{N}{2}-1}x(2n)e^{-i\frac{2\pi{Kn}}{\frac{N}{2}}}+e^{-i\frac{2\pi{k}}{N}}\sum_{n=0}^{\frac{N}{2}-1}x(2n+1)e^{-i\frac{2\pi{Kn}}{\frac{N}{2}}}$  
     $=G(K))+W_N^kH(K)$   
     $=G(k-\frac{N}{2}))+W_N^kH(k-\frac{N}{2})$  

     上式可见$\frac{N}{2}$被约去，根据复平面单位圆以及欧拉恒等式可知：  
     $e^{-i\frac{2\pi{(K+\frac{N}{2})}(2n+1)}{N}}=cos(\frac{2\pi{(K+\frac{N}{2})}(2n+1)}{N})+i\cdot{sin(\frac{2\pi{(K+\frac{N}{2})}(2n+1)}{N})}$  
     $=cos(\frac{2\pi{(2n+1)K}}{N}+(2n+1)N\pi)+i\cdot{sin(\frac{2\pi{(2n+1)K}}{N}+(2n+1)N\pi)}$  
     $=-cos(\frac{2\pi{(2n+1)K}}{N})-i\cdot{sin(\frac{2\pi{(2n+1)K}}{N})}$ //如果这里$2n+1$为$2n$那么符号就不会变   
     $=-e^{i\frac{2\pi{(2n+1)K}}{N}}$  

     所以最终如下：  
     $$X(k)=\begin{cases}
     G(k)+W_N^kH(k),k\in\{0,1,...,\frac{N}{2}-1\}\\
     G(k-\frac{N}{2}))+W_N^kH(k-\frac{N}{2}), k\in\{\frac{N}{2}, \frac{N}{2}+1,...,N-1\}
     \end{cases}
     $$  
     最终复杂度为$O(N*\log_2N)$
2. 蝶形网络   
   由上面的推导可知，求FFT可以进行递归，但是效率不行，所以就需要蝶形网络展平。


3. bitreverse算法
   对于N point蝶形网络，求$x(k)$在几号位，只需要将k化为$log_2N$位二进制数，然后将bit反序，再转成十进制，结果即为$x(k)$所在位。如8 Point的蝶形网络，求$x(4)$所在的位数，那么4的$log_28$（3）位二进制表示为100，再反序得001，十进制为1，所以$x(4)$所在得位是1。

4. IFFT  
   然而海面算法不是DFT而是IDFT（离散傅里叶变换的逆变换），所以需要转换一下，如下表达式：   
   DFT：    
    $X(k)=\sum_{n=0}^{N-1}x(n)e^{-i\frac{2\pi{k}}{N}n},k\in\{0,1,...,N-1\}$    
   IDFT：  
    $x(n)=\frac{1}{N}\sum_{k=0}^{N-1}X(k)e^{i\frac{2\pi{k}}{N}n},n\in\{0,1,...,N-1\}$  
   两个式子很像，可以按照上面的思路推导，则有：  

   $G(n)=\frac{1}{N}\sum_{k=0}^{\frac{N}{2}-1}g(k)e^{i\frac{2\pi{k}}{\frac{N}{2}}n}=\frac{1}{N}\sum_{k=0}^{\frac{N}{2}-1}x(2k)e^{i\frac{2\pi{k}}{\frac{N}{2}}n},n\in\{0,1,...,\frac{N}{2}-1\}$  
    $H(n)=\frac{1}{N}\sum_{k=0}^{\frac{N}{2}-1}h(k)e^{i\frac{2\pi{k}}{\frac{N}{2}}n}=\frac{1}{N}\sum_{k=0}^{\frac{N}{2}-1}x(2k+1)e^{i\frac{2\pi{k}}{\frac{N}{2}}n},n\in\{0,1,...,\frac{N}{2}-1\}$   

    如下推导：
    当$n\in\{0,1,...,\frac{N}{2}-1\}$  
     $x(n)=\frac{1}{N}\sum_{k=0}^{N-1}X(k)e^{i\frac{2\pi{k}}{N}n}$    
     $=\frac{1}{N}\sum_{k=0}^{\frac{N}{2}-1}X(2k)e^{i\frac{2\pi{(2k)}n}{N}}+\frac{1}{N}\sum_{k=0}^{\frac{N}{2}-1}X(2k+1)e^{i\frac{2\pi{(2k+1)}n}{N}}$   
     $=\frac{1}{N}\sum_{k=0}^{\frac{N}{2}-1}X(2k)e^{i\frac{2\pi{k}}{\frac{N}{2}}n}+e^{i\frac{2\pi{n}}{N}}\frac{1}{N}\sum_{k=0}^{\frac{N}{2}-1}X(2k+1)e^{i\frac{2\pi{kn}}{\frac{N}{2}}}$  
     $=G(n)+W_N^{-n}H(n)$  
     
     当$n\in\{\frac{N}{2},\frac{N}{2}+1,...,N-1\}$，令$M=n-N/2$，则$M\in\{0,1,...,\frac{N}{2}-1\}$,如下推导：  
     $x(n)=\sum_{k=0}^{N-1}X(k)e^{i\frac{2\pi{k}}{N}n}$   
     $=\frac{1}{N}\sum_{k=0}^{\frac{N}{2}-1}X(2k)e^{i\frac{2\pi{(2k)}n}{N}}+\frac{1}{N}\sum_{k=0}^{\frac{N}{2}-1}X(2k+1)e^{i\frac{2\pi{n}(2k+1)}{N}}$   
     $=\frac{1}{N}\sum_{k=0}^{\frac{N}{2}-1}X(2k)e^{i\frac{2\pi{(M+\frac{N}{2})}(2k)}{N}}+\frac{1}{N}\sum_{k=0}^{\frac{N}{2}-1}X(2k+1)e^{i\frac{2\pi{(M+\frac{N}{2})}(2k+1)}{N}}$   
     $=\frac{1}{N}\sum_{k=0}^{\frac{N}{2}-1}X(2k)e^{i\frac{2\pi{Mk}}{\frac{N}{2}}}-\frac{1}{N}e^{i\frac{2\pi{M}}{N}}\sum_{k=0}^{\frac{N}{2}-1}X(2k+1)e^{i\frac{2\pi{Mk}}{\frac{N}{2}}}$  
     $=\frac{1}{N}\sum_{k=0}^{\frac{N}{2}-1}X(2k)e^{i\frac{2\pi{Mk}}{\frac{N}{2}}}+\frac{1}{N}e^{i\frac{2\pi{n}}{N}}\sum_{k=0}^{\frac{N}{2}-1}X(2k+1)e^{i\frac{2\pi{Mk}}{\frac{N}{2}}}$   
     $=G(M))+W_N^{-n}H(M)$   
     $=G(n-\frac{N}{2}))+W_N^{-n}H(n-\frac{N}{2})$    
     因为 $e^{i\frac{2\pi{n}}{N}}=W_N^{-n}$  
    所以最终如下：  
     $$x(n)=\begin{cases}
     G(n)+W_N^{-n}H(n),n\in\{0,1,...,\frac{N}{2}-1\}\\
     G(n-\frac{N}{2}))+W_N^{-n}H(n-\frac{N}{2}), n\in\{\frac{N}{2}, \frac{N}{2}+1,...,N-1\}
     \end{cases}
     $$  

     IFFT的bitreverse与FFT相同，同时由于DFT/IDFT是线性的所以常数因子不影响算法。