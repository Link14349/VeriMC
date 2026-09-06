# 入门教程配套源文件

建议按[从零入门教程](../docs/languageTutorial.md)逐步阅读。本目录源文件与教程中的完整代码一致；selector、counter 文件另外附有教程第 8 步的测试。

| 文件 | 对应步骤 | 学习目标 |
|---|---|---|
| [passThrough.vmc](passThrough.vmc) | 第 1 步 | 输入、输出、bit 和持续连接 |
| [inverter.vmc](inverter.vmc) | 第 2 步 | 取反 |
| [selector.vmc](selector.vmc) | 第 3、8 步 | comb、if/else 和组合测试 |
| [oneBitMemory.vmc](oneBitMemory.vmc) | 第 6 步 | 寄存器、同步复位、时钟边沿 |
| [counter.vmc](counter.vmc) | 第 5、7、8 步 | 位宽参数、使能、回绕和时序逻辑测试 |
| [doubleInverter.vmc](doubleInverter.vmc) | 第 9 步 | 导入、实例及端口连接；需要同目录 inverter.vmc |
| [adder.vmc](adder.vmc) | 第 10 步 | 中间信号、进位、取位 |

目前语言尚处于规范阶段。这些源文件用于教学和后续实现验收，尚未经过 VeriMC 编译器执行或真实红石验证；文法能解析不代表物理电路已经实现。
