# simulator

VeriMC 的 Minecraft 红石电路模拟子项目。C++20 原生执行，Three.js 浏览器界面；全部实现位于本目录。

当前正在实现基础版本。完整进度与已验证范围见 [实施记录](docs/implementationStatus.md)，目标范围见 [设计文档](../docs/module-1-design.md)。尚未完成的机制不能视为已兼容 Java Edition 26.2。

## 开发约定

- lowerCamelCase：函数、变量、成员、命名空间和项目文件。
- UpperCamelCase：类型。Minecraft 原始方块 ID、第三方代码与工具规定文件名保持原样。
- CMake 管理 C++，前端构建为本地静态资源；正常使用不依赖远程页面。
- 提交说明不超过 20 字，并点明新增功能或修复问题。
