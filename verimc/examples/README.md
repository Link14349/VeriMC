# VeriMC 0.1 规范示例

这些文件使用本轮设计的语法，**没有经过 VeriMC 编译器执行**。文法检查、人工语义核对和独立数学核算的结果见[设计核查记录](../docs/specReview.md)。

## 正例

| 文件 | 展示内容 |
|---|---|
| [adder.vmc](valid/adder.vmc) | 自动保留加法进位、显式截位、组合测试 |
| [counter.vmc](valid/counter.vmc) | 参数化计数器、同步复位、使能、保持与回绕 |
| [rippleAdder.vmc](valid/rippleAdder.vmc) | FullAdder 组合复用、有限展开、位级连接 |
| [counterPair.vmc](valid/counterPair.vmc) | 文件导入、两个计数器共享时钟、连接加法模块 |
| [stateMachine.vmc](valid/stateMachine.vmc) | 枚举状态、match、同步转移与完整组合赋值 |
| [valueOperations.vmc](valid/valueOperations.vmc) | 纯函数、不可变 let、数组、符号转换和移位边界 |
| [registerSwap.vmc](valid/registerSwap.vmc) | 同一次边沿读取旧值、同时提交，交换两个寄存器 |

## 只供结构与构建语法审阅

- [physicalCounter.vmc](reviewOnly/physicalCounter.vmc)：布局与时序约束、周期测试和绝对 gt 时间线。
- [structuralBuffer.vmc](reviewOnly/structuralBuffer.vmc)：声明外部 cell，并用 module 组合。

这两份文件**有意引用尚未交付的目标/红石库资产**。文法应接受，实际资源解析应报告 EResourceMissing；不能当成已验证红石库或可运行工程。示例时长与尺寸只是待满足的约束值。

## 反例

| 文件 | 主要预期错误 |
|---|---|
| [implicitNarrowing.vmc](invalid/implicitNarrowing.vmc) | uint<5> 结果隐式接入 uint<4> |
| [implicitLevel.vmc](invalid/implicitLevel.vmc) | 强度通道被误认为四位总线 |
| [incompleteComb.vmc](invalid/incompleteComb.vmc) | 组合赋值缺少分支 |
| [nextConflict.vmc](invalid/nextConflict.vmc) | 两个可同时成立的条件重复更新寄存器 |
| [twoClocks.vmc](invalid/twoClocks.vmc) | 两个独立逻辑时钟域 |
| [missingReset.vmc](invalid/missingReset.vmc) | 复位前读取无效状态 |
| [overlappingDrive.vmc](invalid/overlappingDrive.vmc) | 整总线与单个位重叠驱动 |
| [combCycle.vmc](invalid/combCycle.vmc) | 无寄存器边界的组合环 |
| [uncommittedDrive.vmc](invalid/uncommittedDrive.vmc) | 输入未 sample/cycle 就检查结果 |
| [levelArithmetic.vmc](invalid/levelArithmetic.vmc) | 对强度信号使用数字加法 |
| [missingSemicolon.vmc](invalid/missingSemicolon.vmc) | 缺少语句分号 |
| [chainedComparison.vmc](invalid/chainedComparison.vmc) | 使用禁止的链式关系比较 |
| [reservedName.vmc](invalid/reservedName.vmc) | 用保留字命名模块 |

[exampleCases.json](exampleCases.json) 记录每个文件的文法结果及未来语义执行的预期。文法接受一个静态错误反例是正常的；真正的类型/驱动诊断需要未来前端。该清单是验收用例，不是已完成的编译器测试报告。
