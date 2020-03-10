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
  当然这个Renderer是SlateRenderer，与常见的DeferredRenderer还是有区别的，它没有正常渲染模型那么复杂，其对应的Shader也是GlobalShader，所以光照什么的不会考虑进去。