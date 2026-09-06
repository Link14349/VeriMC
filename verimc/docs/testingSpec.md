# VeriMC 0.1：测试、诊断与验收

本规范定义未来测试执行器的行为；当前只有规范和样例，没有 HDL 运行结果或物理综合结果。

## 1. 测试声明与可见范围

```text
test counterReset for Counter(width = 4) mode logical {
    drive reset = true;
    drive enable = false;
    cycle clk;
    expect count == u(4, 0);
    drive reset = false;
    drive enable = true;
    cycle clk;
    expect count == u(4, 1) message "使能后加一";
}
```

mode 必须写 logical 或 physical。logical 的目标是完整 module 实例；physical 的目标是无参数 build 名，不能直接拿抽象模块当已布线电路。

测试直接使用顶层端口名：drive 只能写 input，expect 只能读取 input/output；不能读写隐藏寄存器、修改结构或队列。允许读取确定的端口位/数组元素。watch 可以记录顶层端口并通过 sourceMap 对应到物理引脚；调试 UI 的内部探针属于独立调试能力。

测试 let 只能保存编译期值，不能隐式截取某时刻信号；复用检查应重复写 expect。测试表达式沿用语言类型与运算规则，并额外允许相同 level 类型的比较。clock 不能作为数值表达式；原始时间线中 drive clock 特许接收 bit，watch clock 记录其解码后的边沿。

drive 右侧必须为编译期确定值；完整 input 或互不重叠的位片段可以分别驱动。输入在首次读取/采样之前必须被完整初始化，不默认置零。重复覆盖同一批中的同一位报错；不同采样批之间的变化按显式顺序执行。

`expect expression [message "说明"];` 要求有效 bit 真值；无效/缺失采样不能折算为 false 后继续测试。`watch reference;` 只声明观测，不占用方块或注入游戏更新。它必须出现在任何时间推进之前，同一对象重复 watch 报错。

`repeat N { ... }` 顺序重复测试语句 N 次，N 为非负编译期整数，0 次合法；不生成 N 份电路。`let name = constant;` 只在其后与子块可见且不能重定义。测试没有任意 while、并发线程或运行时软件函数。

## 2. logical 模式

新实例的输入未驱动、reg 未复位，均处于显式无效状态。drive 先加入输入批，紧接的 drive 可补充其它输入；`sample;` 或 `cycle clk;` 提交整批并重新计算组合模型。批内顺序不表示硬件传播顺序。

- `sample;` 不产生时钟边沿，只对组合模型求值。组合模块用它检查当前输入结果。
- `cycle clk;` 提交批后按主规范处理一次上升沿；跨模块统一读取旧状态、提交下一状态，再求组合输出。测试逻辑时钟初始为低，cycle 概念上完成低→高→低；不具备 gt 时长。
- expect 必须出现在 sample/cycle 之后且当前没有待提交 drive；否则报 `ETestUncommittedDrive`，不暗中执行一次采样。
- reg 在复位之前可能无效，但高有效同步复位可以不读取旧状态而建立有效状态；未选中的更新表达式不因其中旧状态无效而阻止复位。
- 无效值在未使用/未选中路径可传播；影响 expect、被选择的控制或提交数据时必须报错并给出首个无效来源。语言静态错误不受此按需有效性规则豁免。

logical 模式禁止 at 原始时间线和手动 drive clock；不能产生刻内脉冲或声称模拟过真实延迟。包含无模型 cell 的设计报 `EModelMissing`，整个测试不得变成“已通过”。

## 3. physical 模式的周期测试

physical 模式有两种互斥正文：周期测试语句，或绝对 gt 时间线。同一测试不能混用 cycle 与 at。

周期测试只适用于有单时钟约束的 build。目标适配器先按固定计划初始化方块；完成后将测试时间原点设为 0，时钟物理接口处于已验证的低状态。若初始队列无法收敛到库所需起点，应报告初始化失败，不丢弃队列强行开始。

设构建周期为 P，高相为 H。第 k 次 cycle 采用固定计划：

1. 周期起点 kP：提交当前 drive 批，在目标 inputAdapterId 规定的输入注入点操作物理输入。
2. kP + P−H：注入该周期的上升沿。
3. (k+1)P：注入下降沿，并继续处理该 gt 的全部已到期事件，直到规定观测阶段完成。
4. 返回给测试脚本；expect 此时读取输出，必须满足该输出约定的 settle 上界。下一批 drive 在下一个 cycle 的起点注入，不提前改变刚被比较的值。

同一 gt 内上周期下降沿/观测与下周期输入批的相对次序由适配器明确记录；不能让两个独立事件队列争夺偶然先后。内部顺序号只表示顺序，没有可任意编程的亚刻时间。

第一次 cycle 前必须完整驱动全部非 clock 输入；后续未重写的输入保持上次值。cycle 负责唯一 clock；手动 drive 它报错。sample 在物理周期测试中禁止，因为它不能表示“等待真实电路稳定”。

时钟/输入驱动必须操作真实外部接口适配器，例如按已验证方法改变输入供电组件。不得直接写内部 reg、粉强度或输出值。没有可用输入适配器时测试失败，不能回退为逻辑模型。

setup、hold、settle 及脉宽/最小延迟约束在物理事件轨迹上检查。周期测试记录所有必要边沿；即使期望值在采样点正确，中间违规也必须使测试失败。

## 4. physical 模式的原始时间线

原始时间线用于组合电路、短脉冲、复位边界和协议违规测试：

```text
test pulseProbe for Counter4 mode physical {
    watch count;
    at 0gt {
        drive clk = false;
        drive reset = true;
        drive enable = false;
    }
    at 40gt { drive clk = true; }
    at 80gt {
        drive clk = false;
        expect count == u(4, 0);
    }
}
```

这些时长仍是示范值，不是组件适用性证据。at 时刻相对于完成初始化的测试原点，以整数 gt 规范化；必须非负并严格递增，等价的 `2rt` 与 `4gt` 视为相同时刻，重复报错。

每个 at 块只能含 drive 和 expect；先收集全部 drive，按端口声明顺序、叶子索引递增注入，再运行该 gt 的目标阶段和到期事件，最后按源码顺序检查 expect。文本上 drive/expect 交错也不改变这三步，建议风格检查提示交错写法。

不同 at 之间 simulator 按真实事件推进，不仅在 at 时刻计算。允许记录同刻多次变化；输入适配器所不能提供的同刻精细刺激必须报能力不足。若要依赖某个具体放置或玩家动作顺序，应由初始化/适配器证据表达，不能把同一 at 中的普通信号赋值当作任意更新指令。

原始时间线仍受 build 和组件的接口协议检查。专门研究违规行为的场景可以预期 `ETimingViolation`，但不能由此变成通过协议验证的电路。0.1 不提供在正常测试中忽略时序错误的语句。

## 5. 失败、预算与结果

一个测试首次出现静态错误、协议违规、无效采样或断言失败即失败。逻辑失败保存输入批、实例与状态来源；物理失败暂停并保留方块、待执行事件、输入游标与相关轨迹，以便重放第一个差异。

所有测试有展开、事件数、gt、记录量和墙钟预算。预算耗尽结果为 incomplete，不是 pass/fail 的逻辑结论；保留状态，不能丢事件后继续。历史截断必须可见，所需证据不完整时不得标记差分通过。

结果最少包含：语言/工具/规范版本、源/配置/库哈希、目标游戏构建、规则、初始化与输入时间线、测试模式、通过/失败/不完整状态、首个诊断及相关轨迹、覆盖条件。

以下状态分别报告，不互相替代：

| 结果 | 能说明什么 |
|---|---|
| 句法检查通过 | 文件能按文法解析 |
| 静态语义通过 | 名称、类型、驱动、展开和时钟规则成立 |
| 逻辑测试通过 | 给定输入下的组合/状态结果符合断言 |
| 物理构建成功 | 组件、布局和约束满足该次构建检查 |
| 方块测试通过 | 对应 simulator 场景和观测通过 |
| 26.2 差分通过 | 固定原版构建中的对应场景和观测一致 |

## 6. 诊断规范

每个诊断包含稳定 code、严重性、中文说明、源文件哈希及范围、展开实例路径和相关来源。类型冲突显示两种类型；驱动冲突列出所有重叠来源；时序错误显示要求窗口、实际事件与所用证据。附修复建议不得自动改变电路周期或机制。

| code | 条件 |
|---|---|
| `ESyntax` / `ELanguageVersion` | 不符合文法 / 语言版本不支持 |
| `EName` / `EResourceMissing` / `EImportCycle` | 名称无效或不可见 / 资源缺失 / 导入成环 |
| `EConstantRequired` / `ERequire` / `EElaborationLimit` | 必须常量 / require 假 / 展开超限 |
| `ETypeMismatch` / `EWidthRange` / `ELiteralRange` | 类型不一致 / 位宽或索引越界 / 常量越界 |
| `EOperatorDomain` / `ELevelConversion` | 运算不在允许类型域 / level 与数字信号隐式混用 |
| `EDriveDirection` / `EMultipleDriver` / `EUndriven` | 驱动方向非法 / 驱动重叠 / 组合路径或连接缺失 |
| `ECombinationalCycle` / `ERecursiveDesign` | 组合依赖环 / 调用或实例化递归 |
| `ENextTarget` / `ENextConflict` / `EUnownedRegister` | next 目标非法 / 同路径重复更新 / reg 无更新所属块 |
| `EClockDomain` / `EInvalidValue` | 时钟规则违反 / 无效状态参与必要控制、数据或观测 |
| `EModelMissing` / `ECellUnverified` / `EUnmapped` | 无逻辑模型 / 缺少对应条件证据 / 无适用物理映射 |
| `EPlacement` / `ERoute` / `ETimingUnknown` / `ETimingViolation` | 放置冲突 / 布线失败 / 时间界未知 / 约束被违反 |
| `EInitialization` / `ECapability` / `EArtifactVersion` | 初始化失败 / 必需能力缺失 / 产物版本不支持 |
| `ETestUncommittedDrive` / `ETestClockDrive` / `ETestTimeline` | 未提交输入即观测 / 时钟被非法驱动 / 时间线非法 |
| `EExpectation` | 有效断言结果为 false |
| `WUnused` / `WStyle` | 未消费的资源或输出 / 命名或排版建议 |

诊断阶段顺序为词法句法 → 导入/名称/展开 → 类型 → 驱动与时钟 → 模型/映射 → 物理约束 → 测试执行。多个错误按源文件规范路径和字节位置排序；后阶段只在前置数据有效时执行，避免虚构级联错误。反例目录为每个用例记录一个主要预期错误，不要求实现输出完全相同的全部附属错误。

## 7. 规范落地后的验收顺序

先用本轮文法和正反例检查前端，再补所有静态语义反例。算术测试要覆盖最大/最小值、进位、负值、符号扩展和超宽移位；状态测试覆盖复位优先、保持、回绕、跨模块共同提交与无效状态。

物理阶段以已验证组件完成四位加法器、四位计数器和模块复用：比较约定窗口值、所有相关瞬态和初始化结果。四位加法器穷举输入值只覆盖稳态函数，不覆盖全部转换顺序；转换、负载、脉宽、方向与位置另列场景。

26.2 差分使用同一构建、规则、初始布局/历史和输入时间线，定位第一个差异事件。没有参考游戏或缺少刻内观测能力时，明确保留缺口，不以每 gt 最终输出相同替代完整时序结论。
