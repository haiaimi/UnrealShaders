# Ambient Occlusion Fields
环境光遮蔽近似可以用如下的函数表示：

$A(x,n)=\frac{1}{\pi}\int_{\Omega}V(x,\omega)\lfloor\omega\cdot n\rfloor d\omega$

$x$：接收点的位置

$n$：接收点位置的法线

$V(x,n)$：可见性函数，当在$\omega$方向没有几何体遮挡就为0，否则为1

本文为了实时计算环境光遮蔽，使用预计算*Ambient Occlusion Fields*，大致思路如下：
* 在计算接受面上的AO时，用半球形（Spherical Cap）代表遮挡物
* Spherical Cap由一个方向和一个固体角表示
* 为了快速确定附近任意位置的Spherical Cap，我们在遮挡体周围预计算固体角和平均遮挡方向
* 同时为了保持低内存需求，这些Fields作为径向函数（Radial functions）存在遮挡体周围的Cubemap中

