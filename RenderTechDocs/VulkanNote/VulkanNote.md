1. 对齐规则（Alignment requirements）
   Uniform Buffer在CPP中的定义到shader中需要有一定的对齐规则，主要有如下规则：
   * 标量必须是4字节对齐
   * 二维向量（vec2）必须是8字节对齐
   * 三维或四维（vec3, vec4）必须是16字节对齐
   * 一个内嵌结构体必须根据其成员的基本对齐大小四舍五入到16字节的倍数
   * 一个矩阵也必须是像vec4那样对齐
  当然可以定义 GLM_FORCE_DEFAULT_ALIGNED_GENTYPES 宏，在包含glm.hpp头文件前面，但是如果是内嵌结构体就会失效，需要加上16字节对齐关键字。
  如下声明就会出现错误：
  ```CPP
    struct UniformBufferObject 
    {
    glm::vec2 foo;
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
    };

    layout(binding = 0) uniform UniformBufferObject 
    {
        vec2 foo;
        mat4 model;
        mat4 view;
        mat4 proj;
    } ubo;
  ```
    在CPP中声明如下就没问题
  ```cpp
        struct UniformBufferObject 
        {
            glm::vec2 foo;
            alignas(16) glm::mat4 model; //强制16字节对齐，这样矩阵数据就不会出错
            glm::mat4 view;
            glm::mat4 proj;
        }
  ```

2. 多个描述符（Multiple descriptor sets）
    描述符会把给定的资源（uniform buffer，texture等）连接到着色器，帮助其通过布局绑定来读取和解释传入的资源数据，一般就是使用描述符来读取和解释传入的资源数据。vulkan中可以同时绑定多个descriptor sets。

3. VkSharingMode，这个标记在创建Buffer、Image的时候都会有这个属性，主要有下面两种：
   ```cpp
   VK_SHARING_MODE_EXCLUSIVE  //资源独占使用
   VK_SHARING_MODE_CONCURRENT //可以共享使用
   ```

4. 创建Image资源的步骤：
   * 创建Image对象，vkCreateImage()，其中VkImageCreateInfo中的initialLayout需要注意一下，这里也就涉及到了**Image layouts**，为了使Image在GPU中有更好的性能，所以在不同的阶段需要有不同的layout以获得最佳性能，一般初始为VK_IMAGE_LAYOUT_UNDEFINED，转为VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL。
   * 为Image分配显存，vkAllocateMemory
   * 绑定Image到显存上，vkBin dImageMemory