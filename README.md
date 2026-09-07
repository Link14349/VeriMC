# VeriMC

面向 Minecraft Java Edition 26.2 的红石硬件设计工具。

- [simulator](simulator/README.md)：C++ 独立仿真核心与 Three.js 浏览器电路工作台。
- [verimc](verimc/README.md)：VeriMC 语言与 C++ 编译前端；源码可输出 `.vmcl` 逻辑网表。
- [vmcl-visualize](vmcl-visualize/README.md)：纯前端逻辑图查看器，在浏览器中打开 `.vmcl`、探索连接并导出 SVG。
- [模块一设计](docs/module-1-design.md)：已批准的架构、目标范围和验收要求。
- HDL 物理后端、Litematic / Create 蓝图导出属于后续模块。

使用 Git 管理，每次有意义且已验证的更新独立提交；提交说明不超过 20 字。
