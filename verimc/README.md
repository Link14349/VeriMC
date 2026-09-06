# VeriMC 语言

VeriMC 的硬件描述语言与编译工具子项目，面向 Minecraft Java Edition 26.2 红石电路。

**当前阶段：主要设计方向待审阅。** 本轮只提出语言定位、抽象边界、编译路线和首版范围；待用户拍板后，再设计完整语法与语义。尚无编译器、已验证器件库或自动布局布线实现。

- [主要设计思路 v0.1](docs/languageDirection.md)：本轮审阅稿，建议从第 1、3、6、12 节阅读。
- [simulator 总设计](../docs/module-1-design.md)：方块仿真、工程文件与后续模块边界。

本子项目的语言文档、未来源码、器件库、测试、样例和构建配置统一放在 `verimc/`。`simulator/` 继续负责真实方块机制与仿真；Litematic / Create 蓝图导出仍属于后续模块。
