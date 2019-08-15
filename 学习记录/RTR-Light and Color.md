# Light Quantities
## Radiometry 辐射率
1. radiometry，辐射率，基础单位是radiant flux（辐射通量），符号是 $\Phi$ ,是辐射能量随时间的流动-能量-通过*watts*(W)来测量（瓦）,即单位是 W。
2. irradiance，辐照度，指区域内辐射率强度，如*d*$\Phi$/*d*A，通常用来表示物体表面，单位是W/m$^2$。
3. solid angle，立体角，可以看成是二维角的拓展，用于表示连续的角，球面度（sr）中用这个来测量，二维中2$\pi$表示整个圆，三维中4$\pi$表示整个单位球。
4. radiant intensity，辐射强度，符号为 *I*，单位是W/*sr*，表示单位球面度的辐射强度
5. radiance，辐射，符号为*L*，单位是W/(m$^2$sr)，是用来测量一条射线的电磁辐射，准确来说是与单位区域和立体角相关，辐射在环境中可以用5个变量来表示（或者包括波长），通常成为辐射分布。
6. spectral power distribution(SPD)

## Photometry 光度
1. radiometry是用衡量物理层面，而photometry是以认为角度进行衡量。
2. illuminance是照度，luminance是亮度，以下是对比表：
   | Radiometric Quantity:Units | Photometric Quantity:Units |
   |:--------------------------:| :-------------------------:|
   |   radiant flux: *watts*(W) | luminous flux: lumen(lm)   |
   |   irradiance: W/m$^2$      |   illuminance: lux(lx)     |
   |  radiant intensity: W/*sr* | luminous intensity: candela(ce)|
   |   radiance: W/(m$^2$sr)    | luminance: cd/m$^2$ = nit  |

## Colorimetry 比色法（色度学）
 1. Colorimetry用来处理光谱图能量分部和颜色感知之间的关系  
 2. Chormaticity（色度），是一个独立于亮度的特征
 3. gamut，色域，全色域是一个三维体，色度图只是这个立体的二维投影，如下图：
   ![image](http://www.realtimerendering.com/figures/RTR4.08.07.png)
   ![image](http://www.realtimerendering.com/figures/RTR4.08.08.png)

## Render with RGB Colors
1. 在计算反射颜色的时候，严格的方法是在每个波长将spectral reflectance（光谱反射率）与SPD相乘。当时通常不会这么计算，这样可能会造成*illuminant metamerism*（光源同色异谱）

# Scene to Screen
## High Dynamic Rang Display Encoding(高动态范围编码)
1. 就是常见的HDR，使用Rec.2020 和Rec.2100标准，正常的SDR是Rec.709和Rec.1886标准。
2. 有三种方法可以转换到HDR
   + HDR10标准，就是如今高端显示器HDR标准，每个像素32位，并且有10位无符号整形，2位给alpha 
   + scRGB，只支持windows操作系统，每个通道16位，兼容于HDR10
   + Dolby Vision，专有格式，没有大规模普及
## Tone Mapping
1. Tone Mapping的作用是把场景radiance转换到显示radiance，需要注意的是图片状态，主要有两个，分别是*scene-referred*和*display-referred*,其实际转换流程如下图：
![image](http://www.realtimerendering.com/figures/RTR4.08.13.png)
2. Tone Mapping可以理解为图片再制作，利用人眼的视觉系统，就是为了尽可能还原图片的原始色彩。
3. 因为Reprocuction会存在很多问题，比如低亮度下感知对比会下降；环境中的亮度范围很大，而显示设备有限，所以需要裁剪，然后提高对比度来抵消裁剪掉的值，但是这也会造成sigmoid(s-shaped)tone-reproduction(太专业不是很懂，类似于胶卷电影上出现的现象)。因此曝光度在tone-mapping里是个至关重要的概念。
### Tone Reproduction Transform
 + 通常描述为一维曲线把*scene-referred*映射到*display-referred*，计算结果会在显示器的色域范围内，除了会影响亮度，也会造成饱和度和色调的变化。通过给亮度运用tone curve可以解决或减少这些问题，但是会导致结果出显示器色域范围。同时使用非线性转换也会出现一些锯齿问题。
 + *Academy Color Encoding System(ACES)* 是为动态画面和影视工业推出的一个标准。该标准把*scene-screen*分成两部分，首先是通过*reference renderring transform(RRT)*转换到 *output color encoding specification(OCES)* 空间，然后就是*output device transform(ODT)* 转换到最终的显示颜色。ACES tone mapping被应用于*unreal engine4*。
 + 需要注意HDR的Tone Mapping的使用，有很多显示器会应用自身的Tone Mapping。例如寒霜引擎提供了SDR的tone production，和不太激进的HDR10的tone mapping
### Exposure
+ 曝光度分析一般是用上一帧的内容，取平均值有时会显得比较敏感（整体容易受一小块区域影响），所以使用亮度直方图

## Color Grading(颜色分级)
Color Grading可以发生在*scene-referrd*和*display-referred*。
