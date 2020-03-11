UE4中使用的3D UI主要就是依靠两个组件，分别是UWidgetInteractionComponent和UWidgetComponent，前者主要是场景中与3DUI交互的相关逻辑，后者就是3D UI渲染相关。所以主要是详细理解UWidgetComponent。

## UWidgetComponent

UE4中的3D 控件的渲染方式就是把Slate中内容渲染到一张RenderTarget上，然后在把这张RenderTarget渲染到场景的模型中，这里支持Plane和Cylinder两种。WidgetComponent继承于MeshComponent。稍微介绍一下这个组件：

### 初始化

* 调用CreateWidget创建要显示的控件,这里创建的是UserWidget
* 注册HitTester
* 创建 FWidgetRenderer，也就是控件渲染器

### 渲染
下面主要是围绕非屏幕空间渲染
* 从UserWidget中获取对应的SlateWidget
* 如果还没有VirtualWindow就创建一个VirtualWindow
  ```cpp
  //创建一个SVirtualWindow控件
  SlateWindow = SNew(SVirtualWindow).Size(CurrentDrawSize);
  ```
* 注册虚拟窗口
  ```cpp
  FSlateApplication::Get().RegisterVirtualWindow(SlateWindow.ToSharedRef());
  ```
* 向虚拟窗口填充内容，也就是之前获取到的SlateWidget
  ```cpp
  SlateWindow->SetContent(SlateWidget);
  ```
* 将SlateWidget渲染到RenderTarget，如下调用：
  ```cpp
  WidgetRenderer->DrawWindow(
      RenderTarget, 
      SlateWindow->GetHittestGrid(),
      SlateWindow.ToSharedRef(),
      DrawScale, 
      CurrentDrawSize,  //渲染的尺寸，也就是RenderTarget的大小
      DeltaTime)
  ```
  当然这个Renderer是SlateRenderer，与常见的DeferredRenderer还是有区别的，它没有正常渲染模型那么复杂，其对应的Shader也是GlobalShader，所以就单单渲染这张RenderTarget光照什么的不会考虑进去。
  不过需要注意的是渲染一张Widget的过程还是比较复杂的，渲染前需要对widget里所有Element（一个Box或一个Border都是一个Element）按层次（Layer）进行收集，合批并生成对应的模型，其中主要的部分如下图：


Widget大致渲染步骤:

a. FSlate3DRenderer下的FSlateElementBatcher调用AddToElements，传入的参数是FSlateWindowElementList，可以理解为前面创建的VirtualWindow 

b. 依次对Window中的每一个Layer（FSlateDrawLayer）进行处理，把FSlateDrawElement的内容加到FElementBatchMap中，会根据不同的Element类型进行添加，如Box，Border，Text，Line等等

c. 这里也是合批的关键，每次尝试添加FSlateDrawElement时都会向FElementBatchMap中检查是否已经有类型想同的Element，如果没有就为FSlateBatchData添加新的顶点索引数组（TArray<FSlateVertexArray>, TArray<FSlateIndexArray>），如果有就沿用之前的数组并继续添加

d. 顶点和索引已经收集完成，SlateBatchData调用CreateRenderBatches来生成渲染所需要的TArray<FRenderBatches>

e. FSlateRenderingPolicy调用DrawElements依次绘制FRenderBatche