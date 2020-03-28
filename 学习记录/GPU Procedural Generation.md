## 简介
General-Purpose Computing on Graphics Processing(GPGPU)技术越来越多的使用，逐渐把更多的计算任务放在GPU上来减轻CPU负担，并且提高计算速度。下面主要是介绍一个使用GPU自动生成洞穴的方法，参考自Paper [Procedural Generation of 3D Caves for Games on the GPU](http://julian.togelius.com/Mark2015Procedural.pdf)。

## 实现
洞穴的生成的Pipeline主要有下面几个部分：
1. 结构组件通过使用L-System来生成一系列的结构点
2. 隧道生成将在结构点周围构建实际的洞穴
3. 渲染器从体素数据中提取网格并且应用Material和Shaders


*L-System*在这之中是比较重要的，它主要有下面两个部分

a. The basic alphabet

|||
|:----:|:----:|
|F|Move forward|
|R|Yaw clockwise|
|L|Yaw counterclockwise|
|U|Pitch up|
|D|Pitch down|
|O|Increase the angle|
|A|Decrease the angle|
|B|Step increase|
|S|Step decrease|
|Z|The tip of a branch|
|0|Stop connecting other branches|
|[]|Start/End branch|
|||

b. The macros used

|||
|:----:|:----:|
|C|A curve|
|H|A verticle ascent that returns to horizontal|
|Q|A branching structure that generates a room|
|T|Similar to the H symbol, but splits into two curves|
|I|Represents a straight line|

这个管线会从体素体积（Voxel volume）里面获取结构点，这个体素体是由一些列Compute shader计算而来，包括体素雕刻着色器（voxel carving shader）和钟乳石生长（stalactite growing shader）着色器。

### 生成总体结构
生成总体结构就是需要前面所需要的两张表，可以看出a表就是结构的组织方式就是移动、控制之类的，而b表则是具体的组成部分，如曲线，直线，空间（room）等等。在实际使用中如：$Z->I[Q[C[T[TQ]]]]$，可以通过上述的符号来构建复杂的结构。       
使用L-System是为了提高随机生成洞穴的可靠性（配合一些权重、频率等等）。用户可以配置其中的各项数据如生产规则和对应的macros甚至dead ends的数量（死角的就是洞穴的尽头）。

### 扭曲元球（Wrapping Meta-Ball）方法
在正常的GPU实时程序化生成地形方法，是应用一些噪声方法到体素中。然而在我们的方法中体素体素的值是-1和1之间，并且是由噪声扰动的元球填充（metaballs）。然而球型元球不能较好的模拟出山洞壁。因此，创建了一个扭曲函数（wrapping function）来扰动元球以匹配从研究的自然现象中识别出的模式轮廓。