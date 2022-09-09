# Fast Reflection Probe Filter
## 需求
在游戏中要实现TOD，就需要对Reflection Capture进行更新，或者预先计算多张不同时间点的Capture，但是不够灵活，最佳方案还是实时Relight和Filter，但是IBL的Filter一般只会预计算，因为运算量巨大，有大量的积分操作，所以要实时必须要有更好的近似方案。
本文内容引用自paper [Fast Filtering of Reflection Probes - Josiah Manson and Peter-Pike Sloan](https://www.ppsloan.org/publications/ggx_filtering.pdf)
## 流程
本方案主要有两个步骤，第一步，降采样，生成mip。第二步，Filter
### DownSampling
对输入Cubemap降采样，生成Mipmap

### Filter Approximation

### Precomputation: Coefficient Table Optiamzation
采样的参数存储在离线计算的查找表中
