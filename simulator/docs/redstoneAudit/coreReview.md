# Java 26.2 核心红石静态核对

核对日期：2026-09-08。C++ 基线为 `cc6b156`；本次审计工具不修改仿真行为。本文的“静态对应”表示读过两侧分支，不能替代任意电路的动态等价证明。本次复跑结果见 `auditReport.md`。

参考文件均相对于本机忽略目录 `.cache/reference/sources/net/minecraft/`。它们来自校验过的 26.2 正式版未混淆服务端 JAR，经 Vineflower 1.12.0 反编译，并非 Mojang 发布的原始 `.java` 文件。

| 机制 | Java 入口 | C++ 入口 | 本次静态判断与边界 |
|---|---|---|---|
| 弱充能、强充能、导体转发 | `world/level/SignalGetter.java`: `getSignal`, `getDirectSignalTo`, `getBestNeighborSignal` | `src/simulator.cpp`: `signal`, `directSignal`, `bestSignal` | 主分支对应：导体取自身弱信号与周围强信号的最大值。普通状态的信号值由官方注册表导出；比较器、粉、陷阱箱、唱片机由运行时补足。不能把空世界导出的静态属性理解为任意环境语义。 |
| 红石粉强度衰减、上下跨层 | `world/level/redstone/RedstoneWireEvaluator.java`: `getIncomingWireSignal` | `Simulator::updateWire` | 水平相邻粉、导体上方且本格上方无遮挡、非导体下方三条路径对应；每跳减一，先排除粉自身的供电回路。几何连通与强度更新必须分别检查。 |
| 粉的点/十字、爬升、间接形状更新 | `world/level/block/RedStoneWireBlock.java`: `getConnectingSide`, `getConnectionState`, `updateIndirectNeighbourShapes` | `wireSide`, `connectionSides`, `wireConnections`, `indirectShapes`, `executeShape` | 活板门、漏斗支撑例外和上下斜邻居路径有对应分支。已有直线差分不穷尽四个朝向、负坐标、半砖/楼梯/活板门组合。 |
| 非实验红石粉的邻居通知顺序 | `world/level/redstone/DefaultRedstoneWireEvaluator.java`: `updatePowerStrength` | `updateWire`, `javaPosBucket` | 原版七元素 `HashSet<BlockPos>` 的桶顺序以 Java 整数哈希、16 桶和稳定排序表达；不是任意集合的通用替代。此假设只适用于当前固定构造规模和目标 JVM 行为。实验 evaluator 不属于本配置。 |
| 嵌套邻居更新 | `world/level/redstone/CollectingNeighborUpdater.java`: `addAndRun`, `runUpdates`; `NeighborUpdater.UPDATE_ORDER` | `enqueue`, `updateNeighbors`; `include/simulator/types.hpp` | 新层逆序入栈、当前层分步恢复、WEST/EAST/DOWN/UP/NORTH/SOUTH 通知顺序对应。预算耗尽有明确产品差异，见下文。 |
| 分块计划刻收集、优先级与去重 | `world/ticks/LevelTicks.java`, `LevelChunkTicks.java`, `ScheduledTick.java` | `src/blockTicks.cpp`; `include/simulator/blockTicks.hpp` | 区块内按时间/优先级/序号，已到期区块间按优先级/序号；待调度与本刻已收集集合分开；位置+类型去重，65536 批次上限。负坐标整除显式向下取整。区块卸载/重载不在实现内。 |
| 中继器延迟、短脉冲、锁定与优先级 | `world/level/block/DiodeBlock.java`, `RepeaterBlock.java` | `diodeInput`, `diodeSideInput`, `prioritizeDiode`, `executeNeighbor`, `executeTick`, `place` | 1–4 档 ×2 gt、短输入消失后仍产生完整输出脉冲、仅二极管侧向锁定、三档 tick 优先级和放置后 1 gt 调度分支对应。已有测试不是全部竞争时序的枚举。 |
| 比较器比较/减法、直读与隔块读 | `world/level/block/ComparatorBlock.java` | `comparatorInput`, `refreshComparator`, `executeNeighbor` | 方块模拟量、侧输入、2 gt 更新、模式切换、相等时比较模式亮而减法模式不亮的分支对应。**缺少物品展示框分支**，见下文。 |
| 火把反相与烧毁 | `world/level/block/RedstoneTorchBlock.java`, `RedstoneWallTorchBlock.java` | `torchInput`, `executeNeighbor`, `executeTick` | 2 gt、60 gt 窗口内第 8 次熄灭、160 gt 恢复检查对应；墙面与落地火把分别检查支撑方向。原版粒子/声音不属于这份信号核对。 |
| 侦测器形状通知、2 gt 脉冲 | `world/level/block/ObserverBlock.java` | `executeShape`, `onPlace`, `onRemove`, `executeTick` | 观察面形状通知、已有计划刻不重复调度、2 gt 开/关、前方通知有对应。不能由方块状态变化测试推断所有容器/实体操作都发出正确观察事件。 |
| 红石灯、铜灯泡 | `world/level/block/RedstoneLampBlock.java`, `CopperBulbBlock.java` | `executeNeighbor`, `executeTick` | 灯立即亮/4 gt 后检查熄灭；铜灯泡只在上升沿翻转，`powered` 与 `lit` 分开。两者不能统一为单一布尔输出。 |

## 已确认缺口：比较器读取物品展示框

Java `ComparatorBlock.getInputSignal` 在直输入小于 15、第一格是导体时，会查第二格中朝向匹配且唯一的 `ItemFrame`，取展示框与该格方块模拟量的最大值。`getItemFrame` 还处理 0 个或多个展示框时不采纳的条件。

`Simulator::comparatorInput` 只有第一格方块模拟量和隔导体第二格方块模拟量；现有世界/刺激接口也没有展示框实体、朝向、旋转角度或唯一性查询。不能把“所有比较器功能已保留”作为当前结论。

最小后续验收场景：比较器背面为石头，在对应第二格挂上方向匹配的展示框，放入物品并逐档旋转；记录比较器输出，再增加重复展示框及背后同时存在模拟量方块的情况。需要先定义显式实体输入或受限展示框模型，然后运行 Java/C++ 相同时间线。**本次仅确认源码缺失，尚未运行这一新增实体场景。**

## 明确的产品差异：更新预算耗尽

原版 `CollectingNeighborUpdater.addAndRun` 达到 `maxChainedNeighborUpdates` 后跳过后续更新并记日志。当前 `Simulator::enqueue` 超过 `updateBudget` 后抛出异常、设置 `faulted` 并要求从快照恢复。默认预算为 1,000,000。

这是项目既定“不能丢事件后继续假装正确”的行为选择，不应为了兼容跳过更新而悄悄改掉。对应电路必须标注“预算内可比较；超预算暂停，不复现原版继续运行结果”。

## 后续差分优先级

1. 比较器展示框输入和明确模型；属于实际缺失功能。
2. 粉线跨层、导体/透明方块/活板门、四朝向与多个坐标平移。本次新增石头、玻璃、上半砖、上半活板门 × 四朝向共 16 个组合，37 gt / 80 点通过；多层回路、顶面遮挡及更多坐标平移仍未穷尽。
3. 同刻输入撤销/锁定变化/二极管优先级竞态，以及跨区块收集与负坐标。比较完整时间线，不能只比较最终灯亮灭。
4. 玩家放置/拆除与器件操作产生的振动事件；原始 `setBlock` 与玩家操作不是同一个接口。本次定向复跑已证实日光传感器右键切换缺少振动事件：13 gt 时原版感测体强度 10，C++ 为 0，见 `confirmedCounterexamples.json`。
5. 区块边界、加载状态、预算中止等明确边界，分别标记支持方式，不混入普通器件通过率。
