# 严格 vanilla-only 参考服务器

对应 issue #14 的第六条验收：**严格 vanilla-only 专用服务器复跑，区分当前 GameTest 的
trade_rebalance 开关**，以及第二条里「同坐标同环境的开关对照」那一半。

## 为什么必须另起一个服务器

`GameTestServer` 把启用的功能开关写死在

```java
private static final FeatureFlagSet ENABLED_FEATURES = FeatureFlags.REGISTRY
   .allFlags()
   .subtract(FeatureFlagSet.of(FeatureFlags.REDSTONE_EXPERIMENTS, FeatureFlags.MINECART_IMPROVEMENTS));
```

26.2 的 `FeatureFlags` 只有四个：`vanilla`、`trade_rebalance`、`redstone_experiments`、
`minecart_improvements`。所以 GameTest 环境恒等于 `{vanilla, trade_rebalance}`，
而 `FeatureFlags.VANILLA_SET` 只有 `{vanilla}`。该字段是 `private static final`，
不能改写，`GameTestServer` 的构造函数也是 private，无法子类化。

实测还发现一点源码检索看不出来的事实：`GameTestServer.create` 会启用**全部可用数据包**，
捕获记录里的 `dataPacks` 是

```
["file/simulator", "minecart_improvements", "redstone_experiments", "trade_rebalance", "vanilla"]
```

也就是说两个实验数据包虽然功能开关关着，包本身仍被选中。
只靠「Java 源码里 `TRADE_REBALANCE` 字段只在某处被引用」这种检索，
**不构成不同开关环境整体等价的证明**——数据包内容、标签、配方都可能参与其中。

因此这里自写 `CaptureRedstoneVanilla`：它继承 `MinecraftServer`（构造函数是 public），
用与 `GameTestServer.create` **完全相同**的 `WorldLoader` 公开路径建世界，只把
`WorldDataConfiguration` 换成 `(DataPackConfig(["vanilla"]), FeatureFlags.VANILLA_SET)`。
不修改任何游戏代码，不打补丁，不反射改常量。

## 怎么保证两边可比

时间线本身完全复用 `CaptureRedstone.runTimeline`，两个入口只在「一刻怎么到来」上不同：
GameTest 用 `helper.runAtTickTime`，这里用自己的 `tickServer` 回调。两者都在
**level tick 之后**执行，也就是 `GameTestTicker.tick()` 在 `tickChildren` 里的同一相位。

原点和时钟不再随机：

- 原点直接取被复跑 fixture 记录的绝对坐标，所以这是**固定坐标**的对照，
  不是两次各自随机原点的比较。
- `gameTime` 与世界时钟从 fixture 的 `referenceEnvironment` 读回。
  26.2 已经没有 `dayTime`，白天进度在 `WorldClock` 里，用
  `level.dimensionType().defaultClock()` + `ServerClockManager.setTotalTicks` 对齐；
  `gameTime` 决定日光传感器 20 gt 轮询的相位，用 `ServerLevelData.setGameTime` 对齐。

区域准备逐条复刻 GameTest 给测试函数的初始世界：

- `TestInstanceBlockEntity.forceLoadChunks`：强加载结构包围盒相交的区块；
- `StructureUtils.clearSpaceForStructure`：48×6×48 全部置空气，清区域内计划刻与方块事件，清实体；
- `TestInstanceBlockEntity.encaseStructure`：`sky_access` 默认 `false`，
  因此四壁、地板（y = 原点 y−1）和天花板（y = 原点 y+6）都是屏障。

**一个必须记录的坑**：强加载票要几刻才让区块进入方块 ticking 范围。
最初没有预热就直接跑时间线，38 个 fixture 里有 15 个在 `frames[1..5]` 出现差异——
全部是「最初几刻的计划刻没有执行」，与功能开关无关。加 20 刻预热后差异消失。
GameTest 那边天然有这段间隔（结构先放置，若干刻后测试函数才启动）。

## 结果

`simulator/tools/reference/vanillaReplay.py` 对每个 timeline fixture 做三件事：

1. 在 fixture 记录的绝对原点与时钟上，用 vanilla-only 服务器复跑同一条时间线，
   与 GameTest 捕获**逐字段**比较（帧状态、模拟量、库存、钟、唱片机，以及刻内更新轨迹），
   环境块本身按预期不同，不参与比较；
2. 把 vanilla-only 捕获交给 `checkReference`，让 C++ 内核直接对严格 vanilla-only 输出负责，
   而不只是对 GameTest 环境负责；
3. 对带轨迹的 fixture 再跑一次**关掉轨迹**的同原点同时钟同开关捕获，
   验证调试监听器不改变执行语义。

最新一轮（`vanillaAll5`）的结论记在
[vanillaOnlyReplayResults.json](vanillaOnlyReplayResults.json)：
**40/40** 与 GameTest 捕获逐字段一致（含刻内更新轨迹与掉落物观测），40/40 通过 `checkReference`，
4 个带轨迹的场景在关掉轨迹后帧序列完全相同且原点相同。

## 这一轮**没有**证明什么

- 只覆盖这 40 条时间线记录的观测字段。开关等价是**在这些场景上**成立，不是全局证明。
- 复跑的世界仍是超平坦、`randomTickSpeed=0`、无玩家；
  自然随机刻、天气、生物生成、维度差异都不在范围内。
- 轨迹现在含更新类型、来源方块、形状更新方向与标志、多向更新下标与快照状态，
  但仍**不含**全部计划刻、方块事件阶段与 RNG 消耗；开关对照只说明
  「有无监听器不改变结果」，不说明轨迹字段已经充分。
- 预热 20 刻是本工具的实现细节；它让两边的区块 ticking 状态一致，
  但没有验证区块加载/卸载本身的语义，那仍是 #14 第五条。
