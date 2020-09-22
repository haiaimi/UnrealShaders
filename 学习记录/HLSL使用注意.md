# HLSL使用注意事项

1. 如果在Shader函数中修饰参数的*out*和*in out*的区别：
   ```hlsl
   void Func1(out float4 Color)
   {
       Color.a = 0;
   }

   void Func2(in out float4 Color)
   {
       Color.a = 0;
   }
   ```
   第一种函数传进来的值会被替换成初始值，如下：
   ```
   float4 Color = 1.f;
   Func1(Color);
   ```
   这时Color的值为0，Color在调用函数之前的值被覆盖了。

    第二种就是相当于C++中的引用。

2. 在for循环或者branch类型的分支中对贴图进行采样要使用SampleLevel的方法，否则就要固定循环次数也就是unroll。这是因为GPU是对四个像素的Chunk一起着色的，fragment需要通过像素之间的UV差距来判断要使用的mipmap，而动态分支对于不同的像素计算方式无法确定，所以就禁止使用Sample。

3. 在OpenGL ES的Compute Shader中不可以对uint8，uint16这种类型的buffer进行单次写入。

4. Compute Shader不能进行采样。

5. 在一些设备上进行位运算及进行整数运算时会出现错误，这个在开发时需要注意。

6. VS传到PS的数据布局需要保持一致如下：
   ```
   void VS(out float4 Color : COLOR, out float2 UV : TEXCOORD)
   {

   }

   void PS(float4 Color : COLOR, float2 UV : TEXCOORD) //正确
   {

   }

   void PS(float2 UV : TEXCOORD, float4 Color : COLOR) //错误
   {

   }
   ```
7. unroll, flatten是静态的，loop, branch偏动态。