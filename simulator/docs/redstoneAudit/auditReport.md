# Java 26.2 红石实现审计（2026-09-08）

本次先把上游与 fork 的 `main` 同步到 `cc6b156`。上游已经合并 PR #1，并增加 15 项交互改动，包括物品栏、器件材质、碰撞/重力/飞行、键位及编辑时继续仿真。本次审计在该提交的 C++ 内核上进行，不改变仿真规则。

**不能认定全部原版特性都已保留。** 既有场景的逐刻对照已经通过，但实体输入、自动事件来源、完整世界状态和组合时序仍有缺口。完整机制清单见 [区域索引](regionMap.md)、[器件核对](implementationReview.md) 和 [核心核对](coreReview.md)。

后续补测六朝向准连接与 24 个黏性活塞脉冲布置均通过，范围与下一步方法见 [原版特性保留方案](quirkCompatibilityPlan.md)。已在 fork 建立 [14 项任务及汇总 issue #15](https://github.com/Zhen-WushuiLingchun/VeriMC/issues/15)，逐项证据等级见 [问题清单](compatibilityIssueList.md)。这些新增通过场景不改变下述 3 个真实反例的失败状态。

本次额外定向测试复现了 **3 个真实差异**：日光传感器切换漏发振动事件、楼梯连接形状不更新，以及 C++ 错误允许活塞推动末地传送门框。下文的 22 组通过数不包含这 3 个失败场景，也不构成“本次所有检查通过”的结论。

## 参考代码与环境

- 固定目标是 Minecraft **Java 26.2 正式版**，没有升级到快照或其他版本。
- [Mojang 官方说明](https://www.minecraft.net/en-us/article/removing-obfuscation-in-java-edition)发布的是保留命名的未混淆 JAR；本次使用本地反编译参考，不称为官方原始源码仓库。
- 官方服务端 SHA-1：`823e2250d24b3ddac457a60c92a6a941943fcd6a`。
- 内层游戏 JAR SHA-256：`183c0499c5f855570ee487dd38e141a53f0121f83a0b07a3bac2d8b6698823e8`，与官方 bundler 的 `versions.list` 相符。依赖 JAR 也按 bundler 列表逐个校验。
- Vineflower 1.12.0 的 Maven SHA-256 已核验；得到 4,849 个 Java 文件。日志有一项 `CarvingMask.lambda$new$0 processed twice` 警告，不能由退出成功推断每个反编译方法都可靠；疑点仍应回到字节码或实际游戏验证。
- 运行参考游戏使用 Linux Temurin **25.0.4.1+1**；生成可读源码时使用 Windows Temurin 24，二者用途不同。
- 参考文件、JDK、反编译输出、GameTest 世界和逐帧结果均位于 `.cache/` 或 `testResults/`，已确认被 Git 忽略。版本、来源和校验信息保留在 [referenceProvenance.json](referenceProvenance.json)。

实际委派使用 Claude CLI 2.1.263，日志模型为 `claude-opus-5`。Opus 负责静态区域索引和器件审阅；原版运行、C++ 比较、证据复核由 Codex 执行。静态审阅不是委派模型执行过 GameTest 的证明。

## 动态证据的解释

第一次重新运行了全部 21 组既有 GameTest 场景，随后用新工具 `checkReference` 在各自**实际绝对坐标**重放 C++。21 组均一致：3,068 个采样帧、各场景共 790 个观察位置、70,654 次位置/帧观测。观察字段包括方块状态、模拟量、库存，以及存在时的钟摆动和唱片机播放状态。

GameTest 每次随机选择测试原点；不能把新原点删除后声称是完全相同的环境。本次保留原点，在 C++ 使用相同坐标运行。原始 `vanillaRun1/report.json` 的旧版比较器因 `origin` 不同返回 `difference`；那是与旧 JSON 的严格比较，不是 Java/C++ 结果。独立的 `nativeComparison.jsonl` 记录实际 Java/C++ 对照。新工具已把这两种结果分开。

负对照把一个已供电粉线的预期状态改为空气，检查器以非零退出码报告 `tick=0`、相对坐标 `[3,2,2]`、预期空气、实际强度 15 粉线。这验证了检查器会发现观察值错误，不只是检查程序正常退出。

### 本次发现并修正的参考工具问题

GameTest 默认 `randomTickSpeed=3`，与项目 `naturalRandomTicks=false` 的意图不一致。`CaptureRedstone` 现在在场景开始前显式设置为 **0**，并把实际 Java 版本、随机刻速率与功能开关写入 `referenceEnvironment`。这不修改 C++，也不禁止场景显式调用器件的随机行为输入。

官方 GameTest 的实际功能集合是 `minecraft:vanilla` 加 `minecraft:trade_rebalance`；`redstone_experiments` 与 `minecart_improvements` 都关闭。当前电路场景不涉及村民交易，但此环境**不能称为只有 vanilla 开关的专用服务器**。需要将来在严格 vanilla-only 专用服务器补测，才能消除这一环境差别；不把“无实验红石”写成“所有实验开关均关闭”。

关闭自然随机刻后的最终复跑及新增几何场景结果见 [differentialResults.json](differentialResults.json)。此文件中的每组记录绑定实际捕获文件 SHA-256。完整原始证据留在本机 `testResults/redstoneAudit/`。

最终关闭自然随机刻的 21 组复跑全部通过。另新增 `java26_2WireGeometry`：石头、玻璃、上半砖、上半活板门四种支撑 × 四个方向，共 16 个爬升连线组合，检查两轮通断，37 gt / 80 点全部匹配，并加入核心回归测试。两者合计 **22 组、3,105 帧、870 个观察位置、73,614 次位置/帧观测**。观察位置数是各独立场景之和，不是同一世界规模。

更新后的 Release 构建与 CTest **3/3** 通过：核心检查 **85/85**、8 位加法器、VMCB **11/11**。上游更新后的原生服务单独编译通过；前端交互/碰撞 **50/50**、TypeScript 检查与 Vite 生产构建通过。构建仍有既有 C++ 警告和 Vite 大包提示。本次没有重新宣称 ASan/UBSan 或浏览器端到端检查通过。

## 确认的差异与复跑方法

| 反例 | 首个差异 | 当前状态与影响 |
|---|---|---|
| R6：日光传感器切换 | 13 gt：原版感测体 active / 10，C++ inactive / 0 | 缺少 `BLOCK_CHANGE` 事件；会改变下游红石行为 |
| R7：两段楼梯形成内角 | 0 gt：原版 `inner_right`，C++ `straight` | 连接形状没有计算；导出的支撑掩码依赖该形状，影响三维器件搭建的正确性 |
| R10：活塞前放末地传送门框 | 2 gt：原版活塞保持缩回，C++ 伸出 | 当前调色板已支持传送门框，不能当成未来扩容风险；C++ 未按原版不可破坏属性拒绝推动 |

三个原创命令场景保留在 `tests/scenarios/`，捕获哈希与首差异保留在 [confirmedCounterexamples.json](confirmedCounterexamples.json)。这三个场景尚未修复，不纳入“通过的 CTest”来冒充一致性；可用下方工具分别复跑，当前预期退出 1。

Opus 索引整理了 35 组机制条目（其中明确标有仅定位项），主代理复核了关键差异并修正不成立的反例假设。例如，flag 2 **仍会**触发形状更新，不能据此构造 R5 的过时锁定状态；R4 若修复要查被移除方块的旧类型；R10 已被真实反例证明在当前范围可达。其余静态发现应先补原版定向证据，不能视为全部已经复现。

建议下一轮先修复 R10 的不可破坏检查、R7 的形状更新与 R6 的事件，再处理 R1 通知来源、R2 活塞移除回调、R3 二极管断支撑顺序等需要事件级验证的问题。比较器展示框、发射器/合成器/Shelf 等缺失功能另列实现任务。

### 已证实缺陷：日光传感器切换没有发出振动事件

最小命令历史保存于 [`tests/scenarios/daylightVibration.json`](../../tests/scenarios/daylightVibration.json)。第 0 gt 放置支撑、日光传感器、相距三格的感测体及灯；第 10 gt 右键切换日光传感器的反向模式。只观察感测体和灯，避免把天空亮度输入差别混入待测信号链路。

- Java `DaylightDetectorBlock.useWithoutItem` 在写入 `inverted` 后发出 `GameEvent.BLOCK_CHANGE`，然后更新日光强度。
- C++ `Simulator::interactDevice`（`src/devices.cpp` 日光分支）只切换属性并调用 `updateDaylight`，缺少 `emitGameEvent`。
- 原版第 **13 gt**：感测体 `phase=active, power=10`（stateId 27226）；C++：`phase=inactive, power=0`（stateId 27164）。真实坐标、首差异和捕获哈希见 [confirmedCounterexamples.json](confirmedCounterexamples.json)。
- 检查器返回 1，这是待修复的真实行为差异，**不是刻意改错预期的负对照**。目前只提交复现场景和证据，未悄悄修改内核规则。

后续应补齐事件的发生时机、源上下文与相应振动回归；修复后才把该场景升级为通过的常规回归。参考捕获器新增了日光传感器原生 `useWithoutItem` 调用；与之共用调用代码的音符盒场景也重新捕获并通过比较。

```sh
python3 simulator/tools/reference/auditReference.py \
  --output simulator/testResults/redstoneAudit/daylightCounterexample \
  --checker simulator/buildAudit/checkReference \
  simulator/tests/scenarios/daylightVibration.json
# 当前基线预期退出 1；报告定位第 13 gt 的感测体差异。
```

### 常规场景

先按 [参考验证说明](../referenceValidation.md)准备固定 JAR，并把 JDK 25 的 `java` 和 `javac` 放到 PATH。原版只用于开发验证，不成为日常 C++ 仿真的依赖。

```sh
cmake -S simulator -B simulator/buildAudit -DCMAKE_BUILD_TYPE=Release \
  -DsimulatorBuildServer=OFF -DBUILD_TESTING=ON
cmake --build simulator/buildAudit --target checkReference simulatorTests

# 输出目录必须尚不存在；不会覆盖已有 fixture 的预期值。
python3 simulator/tools/reference/auditReference.py \
  --output simulator/testResults/redstoneAudit/newRun \
  --checker simulator/buildAudit/checkReference

# 单独比较某个已捕获的原版结果。
simulator/buildAudit/checkReference path/to/actual.json
```

Windows 可在 WSL 内运行这些命令。CMake 依赖仍按项目构建要求提供；本机验证使用 GCC 11.4、CMake 3.30.5、nlohmann/json 3.12、zstd 1.5.7、OpenSSL 3。多个 Java 捕获脚本共用 fixture pack，应**顺序运行**。每个审计场景都使用新建输出目录下的独立世界，不能传用户游戏存档给 GameTest 的 `--universe`。

## 证据边界

- 测试单位是具体命令历史和观察时间线，不是“已验证的全部器件种类数”。不能用 21/21 计算原版功能完成率。
- 每 gt 的观察不保证捕获刻内所有边沿、邻居通知、随机消耗及实体轨迹；静态调用顺序核对和专项事件级差分仍必要。
- 外部刺激中的实体接触、容器查看人数、天气/天空等输入，不等于 C++ 已实现完整 Minecraft 实体与世界环境。
- 首个差异检查器在一组场景中遇错即报告；后续观察没有被验证，不能拿总帧数当成失败场景已通过数量。
- 本次没有声称完整物理后端、自动布局布线或蓝图导出完成，也没有改变固定组件实验的交付边界。
