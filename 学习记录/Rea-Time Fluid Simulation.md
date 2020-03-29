# 简介
本篇是介绍游戏中实时流体的实现，来自paper [Real-Time Fluid Dynamic for Games](https://pdfs.semanticscholar.org/847f/819a4ea14bd789aca8bc88e85e906cfc657c.pdf)。本文里的流体是依据基于物理的流体等式，也就是*Navier-Stokes equations*，下面就称为NS等式。NS等式很早就被提出了，正常被应用高精度，并且有很高的物理精准度的工程模拟，如飞机、桥梁等。但是游戏中不需要这么精准，仅仅只要看起来差不多就行。所以本文只为视觉效果，不需要考虑物理学。

首先看一下流体物理学的公式：    
$\frac{\partial u}{\partial t}=-(u\cdot\nabla)u+\nu{\nabla}^2u+f$

$\frac{\partial \rho}{\partial t}=-(u\cdot\nabla)\rho+\kappa{\nabla}^2\rho+S$

上式是针对速度场（以向量为单位）NS方程，下式是速度场中移动的密度方程。