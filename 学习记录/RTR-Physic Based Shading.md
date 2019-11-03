## Physic of Light
1. 辐照度与振幅的平方是成比例的  $E = ka^2$，a就是振幅

![image](http://www.realtimerendering.com/figures/RTR4.09.03.png)

上图是3个不同场景波长相加下的情况constructive interference, destructive interference（破坏性的）,incoherent addition（不相干）

2. Surfaces，当光照射到一个平面，就需要考虑到折射、反射

3. 常见名词、公式
    + GGX 就是即Trowbridge-Reitz分布其分布公式：
  
    ![image](https://latex.codecogs.com/gif.latex?D_{GGX}(m)=\frac{a^2}{\pi(n\cdot&space;m)^2(a^2-1)&plus;1)^2})

    在UE4中有相对应的实现：
    ```cpp
    // GGX / Trowbridge-Reitz
    // [Walter et al. 2007, "Microfacet models for refraction through rough surfaces"]
    float D_GGX( float a2, float NoH )
    {
        float d = ( NoH * a2 - NoH ) * NoH + 1;	// 2 mad
        return a2 / ( PI*d*d );					// 4 mul, 1 rcp
    }
    ```



在UE4中有相关实现

## The Camera

 