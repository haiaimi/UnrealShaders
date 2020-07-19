## 简介
早期主机平台为了支持多光源并且解决贷款问题，使用了*Tiled Deferred Shading*，而*Cluster Shading*更高效。

### Tiled Shading
*Tiled Shading*在屏幕空间中进行采样，在每个tile里使用里面的MinZ、MaxZ来定义子视锥体。在深度不连续的情况下（MinZ，MaxZ相距太远）会有很多计算浪费，就是因为是基于二维的分块，剔除不够精确，才导致很多性能浪费。

### Cluster Shading
*Cluster Shading*使用更高维的分块，也就是三维，这样一些Empty Space可以被剔除，剔除的精度肯定会更高，这样在Shading的时候会节省很多性能。如果有法线信息那么还可以进行背面的光照计算剔除，但是Forward Shading中，最多只能获取深度信息，所以背面剔除可以暂时不考虑。

这里*Cluster Shading*算法主要包括下面几个部分：
* Render scene to G-Buffers
* Cluster assignment
* Find unique clusters
* Assign lights to clusters
* Shade samples

第一步可能根据实际情况有所不同，因为*Forward Shading*没有G-Buffer，所以第一步只是Pre-Z。

第二步就是根据位置（Deffered可能还会有Normal）为每个Pixel分配Cluster。

第三步就是简化内容为一个*unique cluster*的列表。

第四步为Cluster布置Lights，需要高效的查找每个Cluster受哪些Lights影响，并为每个Cluster生成一个Lights列表。

第五步就是最终着色阶段。

#### Cluster assignment
这一步的目的就是为每个View Sample生成一个整型的*cluster key*。这里尽量使cluster小，这样使其受更少的光影响，当然也要确保Cluster包含足够多的Sample来确保*Light assign*和*Shading*的效率，同时*cluster key*所占的位数也要足够小和准确。

在world space进行划分比较简单，但是这样就需要手动对不同的场景划分，并且划分出来的Cluster太多，这样key的位数也就需要更大，并且在远处的Cluster在屏幕上太小，这也导致性能变差。

所以这里在*View Frustum*中进行划分，不在NDC空间里划分是因为NDC空间是非线性的，划分出来的结果十分不均匀。均匀划分在近处和远处都会有不正常的效果，所以最终是以指数增长的方式划分，如下图：
![image](https://github.com/haiaimi/UnrealShaders/blob/master/RenderPictures/Cluster%20Forward%20Shading/ClusterSubdivision.png) 

根据其划分方式可以有如下推导：
$near_k=near_{k-1}+h_{k-1}$

第一个划分高度，其中$S_y$是y方向上划分的个数：

$h_0=\frac{2neartan\theta}{S_y}$

第k个所在的起始位置：

$h_{0}=near(1+\frac{2tan\theta}{S_y})^k$

反过来推k：
$k=\lfloor \frac{\log(\frac{-Z_{vs}}{near})}{\log(1+\frac{2\tan\theta}{S_y})}\rfloor$

根据上面的公式就可以计算cluster key元组$(i,j,k)$，$(i,j)$就是屏幕空间的tile坐标。

#### Finding Unique Clusters
这里有两个方法*Local Sorting*和*Page Tables*

* Local Sorting：在每个屏幕空间的tile里直接排序，可以直接在*on-chip shared memory*做排序，然后使用local indices连接对应的Pixel。
* Page Tables：这个技术和Virtual Texture思想差不多。但是当key值特别大的时候就不能直接映射到物理空间，所以这里需要做虚拟映射。

使用第一种方案可以直接还原3DBounds。

但是当使用Page Tables的时候很难高效的实现还原，因为有多对一的映射，需要使用原子操作，这样会导致很高比率的碰撞，这样太过昂贵。

#### Light Assignment
这个步骤的目的是为每个Cluster计算受影响的Lights列表，对低数量的光源可以直接进行所有light- cluster进行迭代相交判断。为了支持大规模动态光源，这里对所有的Light使用了空间树，每一帧都会构造一个*Bounding vloume hierarchy(BVH)*，Z-order排序，基于每个光源的离散中心位置。

#### Shading
在*Tile Shading*中只需要在2维屏幕空间中做查询，但是在*Cluster Shading*中就不会有这种直接的映射表。

在排序的方法中，为每个Pixel存放了Index，直接根据这个Index取得对应的Cluster即可。

在*Page Tables*中，会把Index存回之前存放key的物理地址，可以使用计算的key值来取得对应的Cluster Index。

### Implementation and Evaluation

#### Cluster Key Packing
这里为i，j分别使用8位表示，k用10位已经足够了，剩余的6位可以用于可选的*normal clustering*。在一些比较性能要求严格的环境下可以更激进的压缩。

#### Tile Sorting
对于*Cluster Key*这里附加了额外的10位*meta-data*，用来表示Sample相对于这个tile的位置。然后对*Cluster key*和*meta-data*进行排序，最高支持16位key的排序。*meta-data*排序后被用来连接Sample。使用*prefix*操作来进行Cluster的统计，并且为每个Cluster配置一个*Unique ID*，这个ID会被写到作为Cluster成员的每个像素，这个ID被用来当作偏移来取得对应的Cluster 数据。

在每个Cluster只需要存储*Cluster Key*的情况下，*Bounding Volumes*可以从*Cluster Keys*被重构。只需要min、max和Sample Position可以根据排序后的*Cluster Key*上的*meta-data*可以知道对应Cluster有哪些Samples。

#### Page Tables
这里走了两个Pass实现了单级页表，首先在表中标记所有需要的页面，然后使用[parallel prefix sum](https://www.cnblogs.com/biglucky/p/4283473.html)分配物理页，最后keys都被存在物理页里。

#### Light Assignment
前面提到，这里构造了一个查找树。