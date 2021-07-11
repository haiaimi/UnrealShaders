1. 在metal平台上动态加载Texture的时候（调用LoadObject），如果加载3DTexture，并且在后面直接当作SRV使用，可能会出现效果错误的问题，这应该是在CreateTexture和UpdateTexture的时候是异步进行的（UpdateTexture2D和UpdateTexture3D是不同的实现），也有可能是CPU资源是异步加载导致资源没有加载完，可以在LoadTexture后直接Flush来保证资源正确，但是如果Load的操作比较频繁就要慎用，因为频繁flush会造成阻塞，影响性能。

2. 在绘制图元的时侯会设置一个*Primitive*的UniformBuffer，这个UniformBuffer是逐MeshBatch，在函数FMeshMaterialShader::GetElementShaderBindings中可以看到，这个UniformBuffer主要是存储模型空间信息以及一些BoundingBox信息。

3. 在UE中由于是Z轴朝上，这与DX11 API不一致，所以View Matrix会乘一个变换矩阵，比如在绘制阴影深度时的ViewMatrix就会乘以一个矩阵，如下代码：
``` cpp 
// ShadowSetup.cpp
bool FProjectedShadowInfo::SetupPerObjectProjection()
{
	...
	// Store the view matrix
	// Reorder the vectors to match the main view, since ShadowViewMatrix will be used to override the main view's view matrix during shadow depth rendering
	ShadowViewMatrix = Initializer.WorldToLight *
		FMatrix(
		FPlane(0, 0, 1, 0),
		FPlane(1, 0, 0, 0),
		FPlane(0, 1, 0, 0),
		FPlane(0, 0, 0, 1));
}
```

4. FSceneRenderer::SetupMeshPass 就是设置每个MeshPass的内容，填充*FParallelMeshDrawCommandPass*。
5. 在添加自定义Pass的时候，编辑器下材质编译可能会出问题，会出现一些比如材质找不到的错误，这个可能是材质未被标记为可Cache，需要改一下这个文件*PreviewMaterial.cpp*，如下，添加了一个自定义Pass，在编辑器下改材质的时候保证编译：
```cpp
virtual bool ShouldCache(EShaderPlatform Platform, const FShaderType* ShaderType, const FVertexFactoryType* VertexFactoryType) const
{
    // look for any of the needed type
	bool bShaderTypeMatches = false;
    ...
    else if (FCString::Stristr(ShaderType->GetName(), TEXT("MyCustomPass")))
    {
        bShaderTypeMatches = true;
    }
    ...
}
```

6. UE4中的FPlane中，Normal（也就是x，y，z）表示的是平面朝外的方向，在几何体求交的时候，就是通过这种规则检测，也就是物体完全在一个平面的另一面（也就是-Normal方向）才是在几何体内。

7. UE4在运行DX12 API时写自定义Shader时，一定要注意VS和PS的参数顺序要一致，否则运行期会发生编译错误。