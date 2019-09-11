# Volumetric Fogging（体积雾）
普通的雾如深度雾和高度雾是比较固定的，不能是动态的（可以通过使用Billboard来解决，也被称作soft particles），并且不能考虑光照的影响，因此需要体积雾。

体积雾需要考虑到光的transmission（透射），absorption（吸收），scattering（散射，同时有Out-scattering,In-scattering），模拟图如下:
![image](https://github.com/haiaimi/PictureRepository/blob/master/PictureRepository/Rendering%20Learning/UnrealRendering_VolumeFog_1.png)
$$𝐿_{𝑖𝑛𝑐𝑜𝑚𝑖𝑛𝑔} = 𝐿_{𝑡𝑟𝑎𝑛𝑠𝑚𝑖𝑡𝑡𝑒𝑑} + 𝐿_{𝑎𝑏𝑠𝑜𝑟𝑏𝑒𝑑} + 𝐿_{𝑠𝑐𝑎𝑡𝑡𝑒𝑟𝑒d}$$