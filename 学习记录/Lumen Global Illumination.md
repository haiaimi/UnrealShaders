# Lumen

lumen是UE5的次世代动态全局光照方案，其效果非常优秀，质量不逊色于预烘焙的Lightmap，lumen对标的效果应该是传统离线PathTracing，如下对比：

* PathTracing
![image](../RenderPictures/Lumen/PathTracingSample.png)

* Lumen
![image](../RenderPictures/Lumen/LumenSample.png)

## 概况
为了接近PathTracing的效果，Lumen采用ScreenSpace Probe来达到细腻的效果，如果单纯使用WorldSpace Probe很难达到这种效果，当然使用ScreenProbe会有很多相关的优化，如Radiance Cache，Sptial/Temporal Filtering，Jitter，Importance Sampling等一系列优化，Tracing部分可以使用Software Tracing或者Hardware Tracing，Software Tracing借助于SDF不受平台硬件限制，Hardware Tracing就需要API支持RayTracing，同时硬件也要支持，所以这也就有了限制，Lumen也花了很大力气来实现Software Tracing。总之Lumen是一套非常复杂的GI系统，内容还有很多如MeshCard，SurfaceCache，Voxel Cone Traicng，WorldSpace Probe...

## Mesh Card
Mesh Card是Lumen里面Mesh的一种表示形式，主要用于后续的SurfaceCache的Capture以及场景Voxelization，Card分布如下

![image](../RenderPictures/Lumen/MeshCard.png)

MeshCard会有一个MeshProcessor 'FLumenCardMeshProcessor'，在Mesh加到场景中后会和普通的Mesh走一样额流程生成MeshDrawCommand(FLumenCardVS,FLumenCardPS)。这里会有一个LumenScene来Card和SurfaceCache，相当于FScene的简化版本，但是会有场景的Surface信息。

### Lumen Scene Update
Lumen每帧都会有LumenScene的更新及对应的Surface Cache更新，MeshCard在局部空间是AABB，所以对应Card的位置进行Capture（正交投影），一个MeshCard不会只对应一次Capture，会根据Card在视口中所占屏幕大小算出需要的Texel数量，会根据Texel数量切割多次进行Capture。所有的SurfaceCache都会Atlas到一张大图上这里类似于Virtual Texture。SurfaceCache主要有这几种信息：
* Albedo
* Normal
* Emissive
* DepthStencil

下面是大致的调用流程：
- FDeferredShadingSceneRenderer::BeginUpdateLumenSceneTasks 准备需要Capture的Card
    - UpdateSurfaceCachePrimitives 更新图元
    - UpdateSurfaceCacheMeshCards 更新图元对应的MeshCard
    - FLumenSceneData::ProcessLumenSurfaceCacheRequests 计算CardCapture需要的信息 FCardPageRenderData
        - FLumenSceneData::ReallocVirtualSurface
        - FCardPageRenderData::UpdateViewMatrices 相机会对着MeshCard所在平面拍摄，计算对应的View正交矩阵

- FDeferredShadingSceneRenderer::UpdateLumenScene 发起draw，每帧有最大的调用次数限制，目标RT是Capture Atlas

- FDeferredShadingSceneRenderer::UpdateLumenSurfaceCacheAtlas 把Capture Atlas拷贝到SurfaceCache对应的物理贴图上，并对应进行压缩（BC4/5/6）
    - CompressToSurfaceCacheDepth
    - CompressToSurfaceCacheAlbedo
    - CompressToSurfaceCacheOpacity
    - CompressToSurfaceCacheNormal
    - CompressToSurfaceCacheEmissive
    - CopyCardCaptureLightingToAtlas 根据前一帧的光照信息与当前的Card GBuffer信息计算当前surface上的光照，这个光照计算会在后面解释
        - Lumen.SceneDirectLighting
        - Lumen.SceneFinalLighting
        - Lumen.SceneIndirectLighting
    
经过上面的流程，我们就已经有了基本的场景信息，有了这个信息就可以进行间接光的计算，这也类似于Surfel，只不过Lumen是用新的方式实时计算，场景Surface信息可以动态变化也是纯动态GI的必要条件。通过SurfaceCache和SDF这时候已经可以表达出一个基本场景，如下：

* Lumen Scene
![image](../RenderPictures/Lumen/LumenScene.png)

* Final Lit Scene
![image](../RenderPictures/Lumen/LitScene.png)


## Lumen Scene Lighting
有了GI所需的场景信息，下一步就是注入光照信息，这一步就是给SurfaceCache注入光照，同时计算Voxelize场景及Voxel Light。
函数入口：
- FDeferredShadingSceneRenderer::RenderLumenSceneLighting
    - BuildCardUpdateContext
    - RenderDirectLightingForLumenScene
    - RenderRadiosityForLumenScene
    - ComputeLumenSceneVoxelLighting
    - ComputeLumenTranslucencyGIVolume

### BuildCardUpdateContext
GPU上收集需要更新的Card及Tiles

- BuildPageUpdatePriorityHistogramCS 根据Card与相机的关系建立一个直方图
- SelectMaxUpdateBucketCS 对上面的直方图计算前缀和，计算最更新Tiles的最大Index和数量
- BuildCardsUpdateListCS 构建Card更新表，并记录对应信息
- SetCardPageIndexIndirectArgsCS

### RenderDirectLightingForLumenScene
这个步骤是注入直接光和阴影

- Cull Tiles 
    光源对Card切分tile，类似于ClusterLighting
    - SpliceCardPagesIntoTilesCS 对Card进行Light分块，输出需要更新的tile索引表，存储Tile坐标及CardPageIndex相关信息
    - BuildLightTilesCS 对每一个tile进行光源相交检测
    - ComputeLightTileOffsetsPerLightCS 对每个光源对应数量做前缀和
    - CompactLightTilesCS Compact光源数据用于后面的计算
- Shadow map
    借助Virtual Shadow Map给LightTiles计算阴影
- Offscreen shadows
    上面的步骤只是计算了在视口中阴影的影响，但是不在视口中的阴影就需要单独计算，借助SDF进行Trace，从而计算阴影
    - CullDistanceFieldObjectsForLight
        - CullObjectsForShadowCS 以相机为中心200mx200m范围进行剔除
        - ScatterObjectsToShadowTiles 计算每个ShadowTile对应的SDF Objects 
        - ComputeCulledObjectStartOffsetCS 计算每个Tile SDF Objects数据的偏移量
    - DistanceFieldShadowPass
        - DistanceFieldShadowingCS 为每个Tile计算阴影
- Lights
    - FLumenCardDirectLightingPS 为LumenCard注入直接光
- CombineLighting
    结合Albedo，Emissive，DirectLighting及IndirectLighting，最终结果如下：
    ![image](../RenderPictures/Lumen/SurfaceCacheFinalLight.png)


### RenderRadiosityForLumenScene
相当于对SurfaceCache的Radiance收集，以便进行多次反弹（radiance cache）

- BuildRadiosityTilesCS 这个步骤与之前的DirectLight分Tile类似
- LumenRadiosityDistanceFieldTracingCS 这里会为每个Tile Trace一个Probe默认4x4 Texels，根据计算出的方向、ConeHalfAngle并利用SDF进行ConeTrace（采样VoxelLighting），这里的ConeHalfAngle计算：ConeHalfAngle = acosFast(1.0f - 1.0f / (float)(NumTracesPerProbe));
    设半球采样数为$n$，设ConeHalfAngle为$\alpha$，则每个采样Cone的立体角为$\frac{2\pi}{n}$，如下

    $$\int^{2\pi}_{0}\int^{\alpha}_{0}sin\theta d\theta d\phi=\frac{2\pi}{n}$$

    可以求出$\alpha=acos(1-1/n)$。Probe采样如下：
    ![image](../RenderPictures/Lumen/RadiosityProbes.png)

- LumenRadiositySpatialFilterProbeRadiance 为了减少noise，对Atlas Probe进行空间上的Filter，对周围的Probe数据采样并考虑可见性权重
- LumenRadiosityConvertToSH 把当前的Probe（八面体表示）转换为2阶球谐
- LumenRadiosityIntegrateCS 根据上面的球谐数据计算SurfaceCache的间接光
- CombineLighting 把当前计算出的间接光与之前的直接光结合计算出FinalLight

### ComputeLumenSceneVoxelLighting
场景体素化并计算Voxel Lighting