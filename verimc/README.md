# VeriMC 语言

VeriMC 的硬件描述语言与编译工具子项目，面向 Minecraft Java Edition 26.2 红石电路。

**当前阶段：主要方向已获批准，完整语言规范 0.1 草案已形成，供用户审阅。** 尚无编译器、已验证器件库或自动布局布线实现。

- [语言规范 0.1](docs/languageSpec.md)：建议先读第 2 节计数器示例，再读类型、连接和状态语义。
- [物理实现规范](docs/physicalSpec.md)：结构组件、布局、时序、组件库和 simulator 边界。
- [测试与诊断规范](docs/testingSpec.md)：逻辑测试、真实 gt 时间线、诊断和验收要求。
- [成套示例](examples/README.md)：7 份核心正例、2 份物理接口审阅例和 13 份反例。
- [机器可读文法](grammar/verimcGrammar.lark)与[设计核查记录](docs/specReview.md)：本轮实际验证范围。
- [已批准的主要设计思路](docs/languageDirection.md)：保留第一轮方案及取舍背景。
- [simulator 总设计](../docs/module-1-design.md)：方块仿真、工程文件与后续模块边界。

本子项目的语言文档、未来源码、器件库、测试、样例和构建配置统一放在 `verimc/`。`simulator/` 继续负责真实方块机制与仿真；Litematic / Create 蓝图导出仍属于后续模块。
