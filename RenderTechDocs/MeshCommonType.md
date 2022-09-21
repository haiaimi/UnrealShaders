## Common
1. FScene，需要渲染的场景，里面包含了渲染所需要的所有信息
2. FSceneView，游戏场景空间投影到2维屏幕的类型，主要是管理其中所需的信息，如ViewMatrices、ViewFrustum等
3. FViewInfo，派生于FSceneView，拥有额外的渲染信息，如ViewMeshElements、ParallelMeshDrawCommandPasses（用于调度各个Pass的渲染），在UE4的渲染器类型（FSceneRenderer）中会有所有需要渲染的ViewInfo数组TArray<FViewInfo> Views;
4. FSceneViewFamily，是Scene中所有View的集合

## MeshDraw
1. FPrimitiveSceneProxy，图元渲染的代理，对应于UPrimitiveComponent，不同的Mesh类型都会对应一个FPrimitiveSceneProxy，如StaticMesh对应FStaticMeshSceneProxy。
2. FPrimitiveSceneInfo，用于管理图元渲染所需的信息，和FPrimitiveSceneProxy是一对一关系
3. FMeshBatchElement，用于存放网格绘制相关的信息，如FUniformBufferRHIParamRef、TUniformBuffer<FPrimitiveUniformShaderParameters>、索引缓冲、InstanceNum等等
4. FMeshBatch，用于存放具有相同材质和VertexBuffer的FMeshBatchElement，分别对应着VertexFactory和MaterialRenderProxy.
5. FStaticMeshBatch，StaticMesh所需的MeshBatch，专门为静态网格提供的一个Batch
6. FMeshDrawCommand，包含了渲染资源的绑定，如FMeshDrawShaderBinding、FVertexInputStreamArray、FIndexBufferRHIParamRef等资源
7. FMeshDrawShaderBindings，囊括了对一个MeshDrawCommand的Shader资源的绑定，其对应的资源存放在
     TArray<FMeshDrawShaderBindingsLayout, TInlineAllocator<2>> ShaderLayouts; 中 FMeshDrawShaderBindingsLayout 类型就是用来存放具体类型
     Shader的资源。
8. FMeshDrawSingleShaderBindings，该类型是FMeshDrawShaderBindingsLayout的子类，一般对资源写入操作都需要给类型，如Add各类型资源、AddTexture，在 FMeshDrawShaderBindings有对  应的获取方法：inline FMeshDrawSingleShaderBindings GetSingleShaderBindings(EShaderFrequency Frequency)，这绑定的过程分两步（这里的绑定不是渲染层面上的绑定，只是资源的获取），先绑定MeshBatch中的统一的Shader资源，然后再绑定MeshBatchElement
中的资源，如其中的 FUniformBufferRHIParamRef。这些过程其实就是给FMeshDrawCommand填充数据。见 void FMeshPassProcessor::BuildMeshDrawCommands。
9. FPrimitiveComponentId，对应于FPrimitiveSceneProxy，表示其对应组件的id。