# UnrealShaders
虚幻4内Shader编写

### 1、FFT海水模拟
  * 计算FFT的尺寸是1024x1024，网格也是1024x1024，但是材质还是有些问题

![image](https://github.com/haiaimi/UnrealShaders/blob/master/RenderPictures/FFTWave.gif)

### 2、RayMarching 海水，仅限于平面，没有模型（参考自[Alexander](https://www.shadertoy.com/view/Ms2SD1)）
![image](https://github.com/haiaimi/UnrealShaders/blob/master/RenderPictures/raymarchwave.gif)

### 3、ShadowFakery假阴影优化
  #### 假阴影优化的核心就是构建4个模型剪影的符号距离场（SDF），可以CPU创建也可以GPU，GPU的构建效率会高出很多，GPU构建会有三个步骤，如下：
  * 构建正向距离场，如下图：
  ![image](https://github.com/haiaimi/UnrealShaders/blob/master/RenderPictures/ShadowFakery/NormalDistacefield.png)
  
  * 构建反向距离场，如下图：
  ![image](https://github.com/haiaimi/UnrealShaders/blob/master/RenderPictures/ShadowFakery/ReversedDistanceField.png)
   
  * 合并两张距离场，如下图：
  ![image](https://github.com/haiaimi/UnrealShaders/blob/master/RenderPictures/ShadowFakery/MergedDistanceField.png)

  这个距离场包含4通道，每个通道代表每个方向剪影的距离场，有了这个距离场就可以进形平滑插值，所以可以模拟阴影的变化，原理和字体距离场相似。

### 4、Cluster Lighting多光源方案
  对视锥空间进行分块并计算每一块所包含的光照信息，用于后面着色阶段的使用，可以CPU计算也可以GPU计算，GPU计算并不会消耗太多时间，会节省很多CPU消耗，下面是835机型上的测试，同屏50+个光源：
  
  ![image](https://github.com/haiaimi/UnrealShaders/blob/master/RenderPictures/Cluster%20Forward%20Shading/ClusterLighting_835.gif)
  
  ![image](https://github.com/haiaimi/UnrealShaders/blob/master/RenderPictures/Cluster%20Forward%20Shading/ClusterLighting_SceneRendering.jpg)

### 5、Fluid Simulation 2D  
  UE4中实现2维流体的模拟，使用CS解算N-S方程，效果如下：
  
  ![image](https://github.com/haiaimi/UnrealShaders/blob/master/RenderPictures/Real-Time%20Fluid%20Simulation/FluidSimulation2D.gif)
