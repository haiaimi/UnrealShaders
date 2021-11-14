# DDGI
## 前提知识
### Octahedral Map
DDGI里直接使用八面体存储Probe的Irradiance数据，比如一个Probe是8x8，这样可以很好的还原光照信息，要比HL2的Ambient Cube效果更好，详细内容如下：[Octahedral Map](https://jcgt.org/published/0003/02/01/)。

### 切比雪夫不等式
DDGI里使用切比雪夫不等式进行可见性测试，一定程度上防止漏光的出现。

## SDF DDGI

# Lumen