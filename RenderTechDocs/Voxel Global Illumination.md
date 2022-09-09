## Voxel GI
第一眼看到Voxel GI就觉得Voxel GI是很有前途的GI，该方案很巧妙，理论上收敛效果会比其他直接Trace的GI要好，在不借助sdf的情况下，raymarch比普通的raymarch速度更快，可以有比较细腻的indirect light和visibility信息，这一点要比LPV好上不少。SVOGI应该是第一个实时VoxelGI方案，VXGI是Nvidia最终的产品。

* 不成熟的想法，正常来说Voxel GI需要实时体素化，这在PC上都是一个很耗的操作，在移动平台更不可能。