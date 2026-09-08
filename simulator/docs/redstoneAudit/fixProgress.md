# 红石审计问题修复进度

本文件按 issue 逐项记录已完成的修复、原版证据和明确排除项。审计快照本身留在
[auditReport.md](auditReport.md)、[implementationReview.md](implementationReview.md) 与
[coreReview.md](coreReview.md)；那些文档描述 2026-09-08 当时的状态，实际状态以本文件为准。

参考环境与审计当时一致：Java Edition 26.2 正式版，游戏 JAR SHA-256
`183c0499c5f855570ee487dd38e141a53f0121f83a0b07a3bac2d8b6698823e8`，JDK 25.0.4.1，
GameTest 功能开关 `minecraft:vanilla` + `minecraft:trade_rebalance`，`randomTickSpeed=0`，
实验红石关闭。仍然不能称为严格 vanilla-only 专用服务器验证。

## 四项假设的逐条结论（issue #10）

### R5 中继器 `locked` 的竖直形状刷新：已复现并修复

原版 `RepeaterBlock.updateShape` 的条件只是
`direction.getAxis() != state.getValue(FACING).getAxis()`；FACING 恒为水平，
所以 **Y 轴也满足**，竖直形状更新同样刷新 `LOCKED`。C++ 额外排除了 Y 轴。

旧的构造（靠 flag 2 跳过形状更新留下过时 `locked`）确实不成立：flag 2 仍会触发形状更新。
本次改用**直接写入方块状态**造出过时值——这正是编辑器的常规操作，
差分场景里也一直用原始 `stateId` 命令。新增
`tests/fixtures/java26_2RepeaterLockRefresh.json`
（SHA-256 `60ca2d59469f97fef1dfdd6980db7a6ca4bba35a9601116d431341022929b635`，
原点 `[-14438248, -58, 5822622]`，13 帧 × 7 点）三组：

| 组 | 布置 | 结果 |
|---|---|---|
| 竖直 | 写入 `repeater[locked=true]`（无侧向二极管），4 gt 在其上方放石头 | 原版 `locked=false`，旧实现仍 `locked=true`（**反例**） |
| 水平 | 同上但在侧面放石头 | 两侧都刷新为 `locked=false`（对照） |
| 真实锁定 | 侧向有真正供电的中继器，再从上方放石头 | 两侧都保持 `locked=true`（对照，证明刷新算的是正确值） |

修复：`shapeUpdated` 的中继器分支去掉 `axis(direction) != 0`。
**未排除的范围**：没有找到“只靠红石历史（不直接写状态）就能留下过时 `locked`”的路径——
侧向二极管的 `powered` 变化用 flag 2 写入，形状更新照常发出；
活塞销毁侧向二极管时活塞头会立刻落到同一格并补发水平形状更新。因此过时值的来源限于状态直写。

### R15 正弦表：已按 65,536 项逐项排除

新增 `tools/reference/ExportSineTable.java`，用反射读出原版 `Mth.SIN` 的全部 65,536 项，
连同 1,079 组日光临界角度（0–359 度各取本身与相邻 float，去掉负值）与 16 档天空亮度下的最终整数强度，
写入 `tests/fixtures/java26_2SineTable.json`
（SHA-256 `a7c7c314c28036cbc1219a674093818f936cd06123c9c46fbb976c7c00904fa7`）。

C++ 的表从 `daylightSineTable()` 暴露出来，新回归逐项比较 **float 位模式**：
**65,536 项全部相同**。原版用 `(float)Math.sin(i / 10430.378350470453)`，
C++ 用 `(float)std::sin(i * 2π / 65536)`；两者的 double 参数在 9,570 个索引上确实不同，
但 float 结果没有任何一项不同。同一回归还逐项验证了 1,079 × 16 组日光整数强度。
R15 到此为**已排除**，不是假设。

**未排除的范围**：这条结论绑定本机的 glibc `sin` 与固定 JAR 的 `Math.sin`；
换用不同 libm 的平台需要重跑该回归。表相同即索引算术相同（索引算术此前已核对），
因此不需要再单独证明“表项不同是否影响整数强度”。

### flag 128（`UPDATE_SKIP_SHAPE_UPDATE_ON_WIRE`）：已排除，非实验版不可达

对整棵反编译源码树做完整检索，设置该位的地方**只有一处**：
`ExperimentalRedstoneWireEvaluator.java:50` 的 `updateFlags |= 128`；
读取处只有 `NeighborUpdater.java:46`。
而 `RedStoneWireBlock.java:279` 只在 `level.enabledFeatures().contains(FeatureFlags.REDSTONE_EXPERIMENTS)`
（同文件 `:359`）时才使用实验求值器，`GameTestServer.java:85` 更是显式把
`REDSTONE_EXPERIMENTS` 从启用集合里减掉。
目标固定为非实验红石，因此该分支在本兼容目标内**不可达**，缺少它不构成缺陷。

### ☆R13 入队状态快照：未找到可达反例，保持假设

带状态的 `neighborChanged(BlockState, …)` 在 26.2 的调用点只有：
`Level.updateNeighbourForOutputSignal`（比较器）、`BaseRailBlock.onPlace`、
`DetectorRailBlock.updatePowerToConnected`，以及仙人掌、霜冰、海绵（均未实现）。
接收侧 `DiodeBlock.neighborChanged` 先用**当前世界状态**做 `is(this)` 守卫，
再用快照做 `canSurvive` 与 `checkTickOnNeighbor`。

要构造差异，必须在入队与出栈之间改写该比较器/铁轨自身的方块状态。
在当前实现范围内，一次 `CollectingNeighborUpdater` 运行里能改写比较器方块状态的路径只有
「断支撑后被移除」（R3 新增的分支）；而移除后两侧都会跳过——
原版靠 `is(this)`，C++ 靠读到空气后落进 `Device::air` 分支。
方块计划刻、玩家命令、活塞方块事件都在邻居更新运行之外，无法插进这个窗口。

因此本次**没有取得可达反例**，也**没有证伪**。
唯一还有理论差异的情形是「比较器在同一次运行内被换成另一种二极管」——
当前实现范围里没有这样的路径。等将来有刻内事件跟踪（issue #14）后可以再查。

## R9 粉线点/十字切换的额外邻居通知（issue #9）已复现并修复

**根因**：原版 `RedStoneWireBlock.useWithoutItem` 只有在 `newState != state` 时才写入并调用
`updatesOnShapeChange`；后者又只在**该方向的 `RedstoneSide.isConnected()` 确实变化**
且邻居是导体时才 `updateNeighborsAtExceptFromFacing`。
C++ 在 `dot || cross` 成立时无条件对每个导体水平邻居发通知。

**改动**：`Simulator::interact` 的粉线分支先算出新状态，`next == id` 时直接返回；
否则只对连接性发生变化且邻居是导体的方向发通知，来源方块用新状态。

**原版证据**：新增 `tests/fixtures/java26_2WireShapeToggle.json`
（SHA-256 `4750ae509c596878a50e5aa7dd9434644d3db5d4b4c092438ecc117fceafb88c`，
原点 `[943864, -58, 10837183]`，21 帧 × 6 点 = 126 次位置/帧观测），
脚本 `tools/reference/captureWireShapeToggle.py`。读取方同样是准连接、未被通知过的活塞。

| 组 | 布置 | 原版 |
|---|---|---|
| A | 四个水平邻居都是导体且上方有粉线，粉线四面都是 `up`（`isCross` 用 `isConnected()`，`up` 也算连接） | 重算结果与原状态**相同** → 整个 `useWithoutItem` PASS，活塞全程缩回 |
| B | 孤立粉线（自然十字）旁放一个裸导体 | 十字→点，四面连接性都变 → 原版**确实**通知导体，活塞 9 gt 伸出 |

旧实现在 A 组 9 gt、相对坐标 `[12,2,8]` 让活塞伸出（`extended=true` 2258），原版为 `extended=false`（2264）。
B 组两侧一致，说明修复没有把该发的通知也砍掉。

配套改动：`tools/reference/CaptureRedstone.java` 的 `interact` 增加 `RedStoneWireBlock` 分支
（与音符盒、阳光探测器共用同一段反射调用 `useWithoutItem`），否则原版侧无法执行粉线右键。

Release `ctest` 3/3，核心检查 94/94；`tests/fixtures/` 下全部 **31 个场景重新从原版捕获**后全部 `match`
（这次也顺带验证了捕获器改动没有影响既有场景）。

**明确排除的范围**：

- **“连接性未变”这一半条件没有可达反例**。逐条推演后：自然十字的重算结果必然等于自身
  （四面都已连接，`getMissingConnections` 会填回同样的值），因此走 PASS；自然点只在
  完全没有真实连接时存在，切换成十字时四面连接性全部改变。人为用 `setBlock` 摆出
  “四面 side 但东侧真实连接是 up”的状态会被形状更新立刻修回（本次实测到了这一点，
  见脚本注释与 issue 说明）。所以 `SIDE→UP` 视为未变化的分支已按原版实现，但属于对齐，
  不是已复现故障。
- 只覆盖水平方向的通知条件；`updateNeighborsAtExceptFromFacing` 的跳过方向沿用既有实现。

## R8 活塞落地未完整重算邻居形状（issue #8）已复现并修复

**根因**：原版 `PistonMovingBlockEntity.finalTick` / `tick` 在放置被移动方块前调用
`Block.updateFromNeighbourShapes`，即按 `UPDATE_SHAPE_ORDER`（西、东、北、南、下、上）
折叠六次 `updateShape`，中途不写世界。C++ `finishMotion` 只做 `survives()` 近似，
外加红石粉重连和 `waterlogged=false`。

**改动**：

- 把 `executeShape` 的分支体抽成纯状态函数 `Simulator::shapeUpdated(pos, state, direction, neighborState)`，
  返回新状态（0 表示空气），副作用只保留原版同样会做的排刻。
  钟和箱子带方块实体、不可能被活塞移动，仍由 `executeShape` 用各自的辅助函数处理。
- 新增 `Simulator::updateFromNeighborShapes(pos, state)` 按 `shapeOrder` 折叠六次。
- `executeShape` 改为 `shapeUpdated` + 一次 `setBlock`；`finishMotion` 用折叠结果替代
  原来的 `survives`/红石粉近似。
- `Simulator::schedule` 增加显式 `type` 参数：折叠发生在世界仍是 `moving_piston` 的时刻，
  侦测器排刻必须按被移动方块的类型登记。

**原版证据**：新增 `tests/fixtures/java26_2PistonLandingShape.json`
（SHA-256 `00a229a59d462d6cdb0ca3a5bc149b56265c2d4f06b3c0020d08d64e14a0495e`，
原点 `[6086446, -58, 2762786]`，21 帧 × 12 点 = 252 次位置/帧观测），
脚本 `tools/reference/capturePistonLandingShape.py`。三组，按 watch 切片后逐组对照旧实现全部为差异：

| 组 | 布置 | 旧实现 |
|---|---|---|
| stairs | 活塞**向上**推楼梯，落点与既有楼梯构成内角 | 7 gt、`[10,3,8]`：`straight` vs `inner_left` |
| noteBlock | 音符盒被推到金块上方 | 7 gt、`[11,2,20]`：`instrument=harp` vs `bell` |
| observer | 侦测器被推动 | 9 gt、`[11,2,32]`：`powered=false` vs `powered=true` |

楼梯必须**向上**推：水平推动时活塞头落地会再给落点发一次水平形状更新，
把形状顺带修好，反而看不出差别；向上推时唯一变化的邻居在下方，
而竖直形状更新在原版本来就不重算楼梯形状。

Release `ctest` 3/3，核心检查 93/93；`tests/fixtures/` 下全部 **30 个场景重新从原版捕获**后全部 `match`。

**明确排除的范围**：

- `shapeUpdated` 不覆盖钟和箱子。两者都带方块实体，`PistonBaseBlock.isPushable`
  的末行 `!state.hasBlockEntity()` 已经排除它们，因此活塞落地路径不可能用到。
- 折叠里的中继器 `locked` 分支用 `diodeSideInput(p)` 读世界当前状态；
  中继器的推动反应是 DESTROY，不会被移动，这条路径在落地时不可达。
- 只覆盖楼梯、音符盒、侦测器三类落地形状。栅栏、墙、玻璃板等连接类方块尚未进入调色板。

## R4 侦测器移除缺少计划刻判断（issue #7）已复现并修复

**根因**：原版 `ObserverBlock.affectNeighborsAfterRemoval` 的条件是
`POWERED && level.getBlockTicks().hasScheduledTick(pos, this)`；C++ 只判断 `powered`。

**改动**：`onRemove` 的侦测器分支改为
`s.powered && blockTicks.hasScheduled(p, s.type)`。这里的 `s` 是**被移除的旧状态**，
所以 `s.type` 是侦测器类型；`onRemove` 运行时世界上已经写入新方块，
用 `Simulator::hasScheduled(p)`（按当前世界类型查询）是错的。

**原版证据**：新增 `tests/fixtures/java26_2ObserverRemoval.json`
（SHA-256 `fe86cd161a769c0165e45f9e58e7dd7c9bc1a33a90333bd0352ef3388b515d9b`，
原点 `[7838934, -58, -398945]`，21 帧 × 11 点 = 231 次位置/帧观测），
脚本 `tools/reference/captureObserverRemoval.py`。
读取方是一个**准连接但从未被通知过的活塞**（对角位置放红石块，放置时不会通知活塞），
它只有真的收到一次邻居通知才会伸出，因此可以直接检测“有没有发出这次通知”。

| 组 | 布置 | 作用 |
|---|---|---|
| `stale` | 用同类型不同状态的 `setBlock` 把侦测器改成 `powered=true`（`onPlace` 因同方块提前返回，不会复位，也从没排过计划刻），再移除 | 原版**不**通知，活塞保持缩回；缺条件的实现会通知 |
| `queued` | 正常触发的脉冲，探测器一开始就在 | 脉冲本身就会触发探测器，两侧一致 |
| `lateDetector` | 正常触发的脉冲，探测器在脉冲**之后**才放置，随后在熄灭刻仍排队时移除 | 原版会通知，活塞伸出；按当前世界类型查询的实现不会通知 |
| `replace` | 移除后立即在原位放一个新侦测器 | 排队中的计划刻按 (坐标, 方块类型) 保留，8 gt 时在新侦测器上触发一次完整脉冲 |

反向验证（三种实现分别跑同一份原版捕获）：

- 完全不判断计划刻 → 7 gt、`[8,2,8]`：`piston[extended=true]` vs `extended=false`。
- 按当前世界类型查询（`hasScheduled(p)`）→ 8 gt、`[8,2,32]`：`extended=false` vs `extended=true`。
- 按被移除的旧类型查询 → 全部 `match`。

Release `ctest` 3/3，核心检查 92/92；`tests/fixtures/` 下全部 **29 个场景重新从原版捕获**后全部 `match`。

**明确排除的范围**：

- **“熄灭刻已收集进本刻批次但尚未执行”这一窗口没有复现**。要命中它，必须有东西在
  方块计划刻阶段内、批次收集之后、侦测器自己的刻执行之前把侦测器移除。当前实现范围里
  没有这种执行体：编辑器命令与活塞方块事件都在方块计划刻阶段之后，
  红石更新也无法移除侦测器。因此本次用的是另一条同样满足
  `POWERED && !hasScheduledTick` 的可达路径（同类型改状态造出的过时供电状态）。
  该窗口的差异仍未证明可达，也未证伪。
- 侦测器被活塞移动时同样会走这条分支（见 R2），但本次没有为“活塞移动 + 计划刻窗口”单独建反例。

## R3 二极管断支撑的处理阶段（issue #6）已复现并修复

**根因**：原版 `DiodeBlock.neighborChanged` 在 `canSurvive` 失败时**当场**掉落并移除二极管，
然后对六个方向各发一次 `updateNeighborsAt(pos.relative(d), this)`（共 36 次通知，来源是二极管方块）。
C++ 只做 `checkTickOnNeighbor`，移除推迟到形状更新阶段的 `!survives` 分支，那 36 次通知完全没有发生。

**改动**：`Simulator::executeReactiveNeighbor` 在进入 device 分支前，
对二极管补上 `!survives` → `setBlock(p, 0, 3)` + 六向 `updateNeighbors(p.relative(d), -1, 被移除的二极管状态)`。

**原版证据**：新增 `tests/fixtures/java26_2DiodeSupportBreak.json`
（SHA-256 `62c6911205acdb818a817462a0fc3d7dfdea2f73d552d45f4db5ba6009f3d5f7`，
原点 `[2038544, -58, 459704]`，21 帧 × 34 点 = 714 次位置/帧观测），
脚本 `tools/reference/captureDiodeSupportBreak.py`。读取方仍是三向普通铁轨，
**放在二极管正南两格**——这个位置既不在移除 `setBlock` 的六邻居集合里，
也不在 `updateNeighborsInFront` 的集合里，只有那 36 次通知能够到。

六组：中继器/比较器 × {断支撑、不断支撑对照} × {不供电、供电并带下游粉线和红石灯}。
断支撑组在 10 gt 原版把枢纽改成 `south_east`，旧实现停在 `north_south`；
对照组两侧一致，说明差异来自断支撑那次通知本身。

反向验证：只还原这一处改动 → 10 gt、相对坐标 `[8,2,10]`，`north_south` vs `south_east`。
Release `ctest` 3/3，核心检查 91/91；`tests/fixtures/` 下全部 **28 个场景重新从原版捕获**后全部 `match`。

**明确排除的范围**：

- 本次证据是**通知集合**层面的：那 36 次通知只可能来自邻居通知阶段的移除，
  所以它同时证明了阶段和后续通知，比“最终变成空气”强。但**刻内逐事件轨迹仍未实现**，
  属于 quirkCompatibilityPlan 第 3 步与 issue #14 的范围；本次没有比较同刻内的事件序号与优先级。
- 同刻内多个二极管同时断支撑、计划刻排序竞争的场景没有覆盖。
- `DiodeBlock.updateShape` 在原版只对 DOWN 方向做 `canSurviveOn` 判断，C++ 的形状阶段
  仍是通用的 `!survives`。这一处差别本次没有构造反例，也没有改动。

## R2 活塞移动跳过移除回调（issue #5）已复现并修复

**根因**：原版 `LevelChunk.setBlockState` 的条件是
`(blockChanged || newBlock instanceof BaseRailBlock) && ((flags & 1) != 0 || movedByPiston)`，
`movedByPiston` 只是**传给各方块自行决定**。C++ 用 `(flags & 64u) == 0` 把整条路径关掉。

在 26.2 中，`affectNeighborsAfterRemoval` 里检查并跳过 `movedByPiston` 的只有：
压力板、`BaseRailBlock`、按钮、`DiodeBlock`、拉杆、红石火把、红石粉、绊线、绊线钩。
**侦测器、避雷针、讲台、容器、活塞头等不检查**，被活塞移动时照样发通知。

**改动**：

- `Simulator::onRemove` 增加 `movedByPiston` 参数，`setBlock` 改为原版条件并把 flag 64 传下去。
- 上面那九类按原版加 `!movedByPiston` 守卫，其余不加。
- **附带发现并修复**：`Simulator::onPlace` 缺少避雷针分支。原版
  `LightningRodBlock.onPlace` 会给仍 `POWERED` 且没有计划刻的避雷针补排 8 gt 熄灭刻。
  这个缺口是在构造本反例（把带电避雷针推走再落地）时由原版对照暴露出来的。

**原版证据**：新增 `tests/fixtures/java26_2PistonRemovalCallback.json`
（SHA-256 `77b17ea604a87dbda1b5377ebbff5c0f9cdcfd22eacbb061edca26fe0f446404`，
原点 `[6032598, -58, 4825698]`，31 帧 × 14 点 = 434 次位置/帧观测），
脚本 `tools/reference/capturePistonRemovalCallback.py`。两组对称布置：
朝东的避雷针在 4 gt 被刺激供电，8 gt 被活塞向南推走；避雷针的
`updateNeighbours` 通知的是它西侧一格的邻居集合，落在活塞自身通知集合之外，
那里放了一个三向普通铁轨枢纽作为读取方。

- 对照组：铁轨全程不供电，两次通知给出相同形状，两侧一致。
- 实验组：6 gt 在铁轨上方放红石块（该放置的来源方块是空气，不触发 `updateState`），
  于是只有 8 gt 移除时刻的那次通知能改变形状。原版 8 gt 得到 `north_west`，
  旧实现停在 `south_west`。

反向验证：只把 `setBlock` 的条件还原（保留避雷针 `onPlace`）→ 8 gt、`[6,2,20]` 报
`south_west` vs `north_west`；只缺避雷针 `onPlace`（保留移除回调）→ 18 gt、`[8,2,9]` 报
`powered=true` vs `powered=false`。两处改动各自都有独立反例。

Release `ctest` 3/3，核心检查 90/90；`tests/fixtures/` 下全部 **27 个场景重新从原版捕获**后全部 `match`。

**明确排除的范围**：

- 只用避雷针复现。侦测器同样不检查 `movedByPiston`，但它的条件还牵涉
  `hasScheduledTick`（R4 / issue #7），本次不动它的判定，也没有为侦测器建立反例。
- 讲台、容器、活塞头等不检查 `movedByPiston` 的方块要么带方块实体不可推动，要么没有构造反例。
- 九类加了守卫的方块没有逐个建立“加了守卫才正确”的反例；它们的守卫来自原版源码逐句核对，
  并由全量 27 个场景的原版复跑保证没有回归。

## R1 邻居通知的来源方块（issue #4）已复现并修复

**根因**：原版 `Level.updateNeighborsAt(pos, sourceBlock)` 把“发起更新的那个方块”传给所有被通知者。
C++ 的 `updateNeighbors(p)` 在不传 `source` 时取 `world.get(p)`，即“被通知位置自己的方块”。
读取该参数的是 `RailBlock.updateState`（要求 `block.defaultBlockState().isSignalSource()` 且潜在连接数为 3）
和 `DoorBlock.neighborChanged`（来源是同一种门时跳过）。

**改动**（全部改为按原版显式传递来源）：

| 位置 | 原版来源 |
|---|---|
| `onPlace` 红石火把 / `onRemove` 红石火把 | `RedstoneTorchBlock` 自身 |
| `onPlace` 红石粉的上下通知 / `onRemove` 红石粉的六向通知 | `RedStoneWireBlock` |
| `updateWire` 的七元素集合 | `DefaultRedstoneWireEvaluator` 的 `this.wireBlock` |
| `wireCorners`（`checkCornerChangeAt`） | `RedStoneWireBlock` |
| 红石粉点/十字右键（`updatesOnShapeChange`） | `newState.getBlock()` |
| `movePistonBlocks` 的被破坏格、被推走格、活塞臂 | 各自移动前的方块状态、`Blocks.PISTON_HEAD` |
| `finishMotion`（`PistonMovingBlockEntity.finalTick`） | 刚落地的方块 |

**原版证据**：新增 `tests/fixtures/java26_2RailNotificationSource.json`
（SHA-256 `dc495aeb22ec7c5a700a967e2aa483330449cd5101ed160c8ec7f553be6ca600`，
原点 `[7265194, -58, -9435984]`，41 帧 × 29 点 = 1,189 次位置/帧观测），
脚本 `tools/reference/captureRailNotificationSource.py`。把 watch 列表按组切片后逐组对照旧实现：

| 组 | 布置 | 旧实现 |
|---|---|---|
| wireGap1 | 红石粉紧邻三向铁轨 | **一致**（被通知位置本身就是粉线，默认值恰好正确） |
| wireGap2 | 红石粉隔一个导体 | **差异**：10 gt、`[11,2,16]`，原版 `east_west`，旧实现 `north_south` |
| wireGap3 | 红石粉隔两个导体 | **一致**（七元素集合本来就够不到，原版也不更新） |
| torch | 红石火把隔一个导体放置/移除 | **差异**：10 gt、`[31,2,6]` |
| piston | 活塞把铁轨上方的红石块推走 | **差异**：11 gt、`[31,2,16]` |
| door | 红石粉旁的双层门 | **一致**（见下） |

修复后整份 fixture `match`；Release `ctest` 3/3，核心检查 89/89。
并把 `tests/fixtures/` 下全部 26 个场景重新从原版捕获（各自新随机原点）逐帧对照，**全部 match**，
确认这次改动没有回归红石粉、铁轨、火把、刻批次、活塞等既有场景。

### 补充：移除回调里的来源方块（同属 R1，issue #4 关闭后追加）

在做 R2/R3/R4 时发现 `onRemove` 里的 `notifyFront` / `notifyAttached` 仍用
`world.get(p)` 作为来源，而此刻世界上已经写入新方块（通常是空气）。
原版 `LeverBlock.updateNeighbours`、`ButtonBlock`、`DiodeBlock.updateNeighborsInFront`、
`ObserverBlock.affectNeighborsAfterRemoval` 传的都是 `this`（被移除的方块）。

两个函数增加可选 `source` 参数，`onRemove` 的拉杆/按钮、中继器/比较器、侦测器分支显式传 `old`。
`notifyAttached` 的第一次通知原本是 `updateNeighbors(p)`（默认来源），现在也走同一个来源。

新增 `tests/fixtures/java26_2RemovalNotifySource.json`
（SHA-256 `0cf0ddc55c2d0845d742498d6b0c4588c457a011c3fe5c5b00e0f5f30bb90898`，
原点 `[2135833, -58, -10975893]`，17 帧 × 20 点 = 340 次位置/帧观测），
脚本 `tools/reference/captureRemovalNotifySource.py`。四组，读取方是三向普通铁轨，
放在被通知位置再外一格，只有这两个调用能够到：

| 组 | 旧实现 |
|---|---|
| 带电墙拉杆被移除 | **差异**：8 gt、`[9,2,8]`，`north_south` vs `east_west` |
| 不带电拉杆被移除（原版根本不发通知） | 一致 |
| 中继器被移除 | **差异**：8 gt、`[10,2,28]`，`south_west` vs `north_west` |
| 比较器被移除 | **差异**：8 gt、`[10,2,38]`，`south_west` vs `north_west` |

修复后整份 fixture `match`；全部 **32 个场景重新从原版捕获**后全部 `match`；
`ctest` 3/3，核心检查 95/95。

**明确排除的范围**：

- **门这一路径没有找到可达反例**。原版七元素集合里几乎总还有另一个与目标门半扇相邻、
  且在旧实现里来源不是门的位置（例如粉线正上方那格），所以上半扇仍会被正确更新。
  代码已按原版传递来源，但这一条属于对齐，不是已复现故障；场景保留该组作为“两侧一致”的记录。
- 只验证了 `RailBlock.updateState` 和门这两个读取方。其他潜在读取方（如未实现器件）没有覆盖。
- `finishMotion` 的来源修正没有单独反例，它与活塞组同属一次改动。

## R7 楼梯连接形状（issue #2）已修复

**根因**：`Simulator::executeShape` 与 `Simulator::place` 都没有楼梯分支，`shape` 永远是放置时写入的值。
`shape` 决定 `supportMask`/`rigidMask`/`centerMask`，所以这不是外观差异：内角楼梯的侧面在原版是 sturdy，
可以挂红石墙火把，仿真里永远不能。

**改动**：

- `tools/reference/ExportReference.java` 增加逐方块导出 `stairs`（即原版 `StairBlock.isStairs` 的
  `block instanceof StairBlock`），`BlockType` 新增同名字段。这样 `WeatheringCopperStairBlock`
  等子类自动包含在内，不靠名字后缀猜测。重新生成 `data/blockStates.json`。
- 新增 `Simulator::stairsShape`，逐句对应 `StairBlock.getStairsShape`：先看朝向前方
  （`pos.relative(facing)`）的楼梯给出 `outer_left`/`outer_right`，再看背后
  （`pos.relative(facing.getOpposite())`）的楼梯给出 `inner_left`/`inner_right`，
  两者都要求同 `half`、轴不同，并通过 `canTakeShape`（对侧不是同朝向同 half 的楼梯）。
  `types.hpp` 增加 `clockWise` / `counterClockWise`。
- `executeShape` 对楼梯只在水平方向的形状更新中重算 `shape`（竖直方向落到原版基类的空实现），
  `place` 按 `getStateForPlacement` 在放置时算出 `shape`。
- `tools/reference/CaptureRedstone.java` 的 `playerPlace` 增加楼梯分支：楼梯的
  `getStateForPlacement` 直接使用玩家水平朝向（箱子用其反向），`half` 来自点击面，
  因此对楼梯把偏航角设为目标 `facing`，并按目标 `half` 点击目标格自身的上/下面。
  箱子路径逐字不变，`java26_2CopperChests` 重新捕获后仍然一致。

**原版证据**：

1. 原反例 `tests/scenarios/stairShape.json` 重跑通过。捕获 SHA-256
   `fb849305efdd7723c0e0035627e26f4991d1eb1f54ec7d2f5876db8acf75c787`，
   原点 `[10884563, -58, 13377240]`，5 帧 × 2 点。
2. 新增常规回归 `tests/fixtures/java26_2StairShapes.json`
   （SHA-256 `946d8b9330009112b11f0ec4a6d9057ac547d53d7dc960bc2bfbdf6e49c2ddbc`，
   原点 `[4903637, -58, 8864154]`，17 帧 × 153 点 = 2,601 次位置/帧观测），
   脚本 `tools/reference/captureStairShapes.py`。67 个互不相邻的 5×5 格子：

   - 32 组：4 朝向 × 上下半 × `outer_left`/`outer_right`/`inner_left`/`inner_right`，
     先放主楼梯，再放搭档触发形状更新，第 10 gt 拆除搭档后回到 `straight`。
   - 16 组：同样布置但在 `canTakeShape` 探测位放一段同朝向同 half 的楼梯，原版保持 `straight`。
   - 16 组：搭档先放，主楼梯用原版 `getStateForPlacement` 放置，放置当刻即得内/外角；
     捕获确认原版给出的 `facing`/`half` 与请求一致。
   - 3 组：内角把侧面变 sturdy，第 4 gt 在该面挂红石墙火把；第 10 gt 拆除搭档后楼梯回到
     `straight`，火把随形状更新一起消失。这是 `shape → supportMask → survives` 的实际链路证据。

   第 5 gt 的形状分布为 bottom/top 各含 `outer_left` 4、`outer_right` 8、`inner_left` 5、
   `inner_right` 8/9，第 15 gt 全部回到 `straight`，三个火把位置变回空气。
3. 反向验证（两条路径分别验证）：
   - 去掉 `executeShape` 的楼梯分支 → 第 2 gt、相对坐标 `[4,2,4]` 报 `straight` vs `outer_left`。
   - 只去掉 `place` 的楼梯分支 → 第 2 gt、相对坐标 `[19,2,29]`（第一个玩家放置格）报
     `straight` vs `outer_right`。
4. Release `ctest` 3/3 通过，核心检查 88/88。

**明确排除的范围**：

- **活塞推动后的楼梯仍保持旧 `shape`**。`Simulator::finishMotion` 用 `survives()` 近似原版的
  `Block.updateFromNeighbourShapes`，这是 R8（issue #8）的范围，本次没有修，也没有为它建证据。
- 只覆盖 `oak_stairs` 一种材质。`stairs` 标志来自注册表 `instanceof StairBlock`，
  64 种楼梯方块全部命中，但差分场景没有逐材质重复。
- 楼梯的 `waterlogged` 与流体计划刻不在实现范围内，场景中全部为 `false`。
- 没有覆盖“外角楼梯失去正面 sturdy”对应的挂接实体：外角搭档必然占据该面所在的格子，
  在这套只放方块的场景里无法同时放置见证方块。内角的获得/失去已经双向验证。

## R6 日光传感器切换的振动事件（issue #1）已修复

**根因**：`DaylightDetectorBlock.useWithoutItem` 在 `setBlock(pos, newState, 2)` 之后、
`updateSignalStrength` 之前发出 `GameEvent.BLOCK_CHANGE`，上下文为 `Context.of(player, newState)`。
`Simulator::interactDevice` 的日光分支只切换属性并刷新强度，完全没有这次事件。

**改动**：`src/devices.cpp` 日光分支在写入新状态后、`updateDaylight` 之前调用
`emitGameEvent("block_change", pos, {false, false, false, inverted})`，
上下文的受影响方块状态取**切换后**的状态，与原版顺序一致。
`block_change` 在 26.2 的频率表中是 11。

**原版证据**：

1. 原反例 `tests/scenarios/daylightVibration.json` 重跑通过。捕获 SHA-256
   `1564e661c2a2db74dcff3804d85276eb15918b5dffc12bbe6d1717a986a4c691`，
   原点 `[7217173, -58, -2219360]`，26 帧 × 2 点，`nativeComparison.status = match`。
2. 新增常规回归 `tests/fixtures/java26_2DaylightVibration.json`
   （SHA-256 `9eb5055ee13461a71024332c6c393f50e43b4171eb96437d64ab991f7635b5dd`，
   原点 `[11940600, -58, -11279207]`，81 帧 × 6 点 = 486 次位置/帧观测），
   脚本 `tools/reference/captureDaylightVibration.py`。四组互不干扰（间距超过 8 / 16 格监听半径）：

   | 组 | 布置 | 原版结果 |
   |---|---|---|
   | A | 幽匿感测体距 3 格，第 10 gt 与第 60 gt 各切换一次 | 13 gt 与 63 gt 均 `active`、`power=10`、比较器读数 11，红石灯随之亮 |
   | B | 校频感测体背面滤波强度 11 | 13 gt `active`、`power=13`、读数 11 |
   | C | 校频感测体背面滤波强度 9 | 全程 `inactive`，事件被频率过滤拒绝 |
   | D | 幽匿感测体距 5 格 | 15 gt 才 `active`、`power=6`，行进刻数与事件原点一致 |

   A 组第二次切换是 `inverted=true → false` 方向，证明两个方向都发事件；
   B/C 组一起证明事件频率确实是 11，不只是“有事件到达”。
3. 反向验证：只删掉这一行 `emitGameEvent` 重新编译，该 fixture 在 13 gt、
   相对坐标 `[6,2,3]` 报出 `inactive/power=0` vs `active/power=10`。
4. 共用 `interact` 调用路径的既有回归重新从原版捕获后仍然一致：
   `java26_2Notes`（100 帧 × 32 点）、`java26_2Vibrations`（161 × 13）、
   `java26_2DeviceVibrations`（155 × 31）全部 `match`，各自使用新的随机原点。
5. Release `ctest` 3/3 通过，核心检查 87/87。

**明确排除的范围**：

- **不观察日光传感器自身的 `power`**。它由 `getEffectiveSkyBrightness` 决定，而 C++ 把天空亮度
  建模为显式外部刺激（默认 0）。该差别属于世界环境输入建模，不属于本 issue；场景刻意让
  传感器与感测体相隔空气，使传感器的红石输出不进入被测链路。
- 事件来源实体上下文只覆盖“非旁观、非潜行的创造模式玩家”这一种。GameTest 用
  `helper.makeMockPlayer(GameType.CREATIVE)`，C++ 用默认上下文；旁观者与潜行过滤
  由既有 `captureVibrations.py` 的显式刺激场景覆盖，本次没有为日光传感器单独重测。
- 只修了 `interact` 这一条路径。其他玩家操作/放置产生的振动来源是否齐全，仍属 issue #14 的范围。

## R10 活塞推动判据（issue #3）已修复

**根因**：`Simulator::pushable` 用方块名白名单代替原版
`PistonBaseBlock.isPushable` 的 `state.getDestroySpeed(level, pos) == -1.0F` 判断，
因此末地传送门框（`destroySpeed = -1`、`pushReaction = NORMAL`、无方块实体）被错误允许推动。
`PUSH_ONLY` 分支在原版是 `return direction == connectionDirection` 直接返回，
C++ 却在其后又执行了一次 `!hasBlockEntity()`。

**改动**：

- `tools/reference/ExportReference.java` 增加导出每个方块状态的 `destroySpeed`
  （`BlockBehaviour.BlockStateBase.getDestroySpeed` 返回的是与位置无关的常量字段，逐状态导出即精确）。
- 重新生成 `data/blockStates.json`（提交 `9f401bb` 时 SHA-256
  `a7f075994561df20f9be27071933ced23164f34612327ecc8bbec5c41bf19a16`；R7 又新增 `stairs` 列后变为
  `9a44e583bbe9bfd08827fe364a51fee356b20cbd5b0122a99bf36578e30e81c2`）。
  状态 ID 与既有字段未变，只新增一列；该文件参与 `rulesDigest`，旧 `.vmcb` 需要显式迁移。
- `BlockState` 新增 `indestructible`（`destroySpeed == -1.0f`），`pushable()` 按原版顺序判断，
  并删除硬编码的 `minecraft:bedrock`；黑曜石四件套仍按原版的显式 `state.is(...)` 名字分支保留。
- `PUSH_ONLY` 改为直接返回 `movement == connection`。

**原版证据**：

1. 原反例 `tests/scenarios/endPortalFramePush.json` 重跑通过。
   捕获 SHA-256 `37cd9a570cd131a0686c9a894b4e2dcb7e313252512036a5bb7a8bf00a3083b0`，
   原点 `[10765322, -58, 13086365]`，9 帧 × 3 点，`nativeComparison.status = match`。
2. 新增常规回归 `tests/fixtures/java26_2PistonPushability.json`
   （SHA-256 `c7f91713ba9466656e9b9779caa2cf2b818a8eefb5a705472885f548fa2e33de`，
   原点 `[13430672, -58, 3159306]`，25 帧 × 36 点 = 900 次位置/帧观测），
   由原创脚本 `tools/reference/capturePistonPushability.py` 从原版捕获，预期值未经手工修改。
   八行分别覆盖：普通石头推出/缩回、基岩阻挡、末地传送门框阻挡、
   釉面陶瓦沿连接方向被推出、黏性活塞缩回**不能**拉回釉面陶瓦（对照行用石头证明拉回本身有效）、
   黏液块侧向分支中的釉面陶瓦留在原地（对照行用石头证明侧向拖带有效）。
3. 反向验证：仅把 `pushable()` 还原为旧实现后，该 fixture 在 3 gt、相对坐标 `[3,2,11]`
   报出 `piston[extended=false]` vs `piston[extended=true]`，证明这组回归确实有区分能力。
4. `ctest`（Release）3/3 通过，核心检查 86/86。

**明确排除的范围**：

- `PUSH_ONLY` 之后多做的方块实体判断在 26.2 **没有**可观察差异：注册表中 `PUSH_ONLY` 的
  16 种方块全部是釉面陶瓦，且没有任何一种带方块实体。代码已改为与原版同序，但这一条属于
  与原版对齐，不是已复现的故障。
- 本次只覆盖 `pushable()` 的判据。活塞移动路径上的 R1（通知源方块）、R2（跳过移除回调）、
  R8（落地形状重算）仍未修复，见对应 issue。
- 硬度为 −1 的方块里，屏障、下界传送门、末地传送门/折跃门、各类命令方块与结构方块在当前
  调色板仍不可放置；本次只证明了基岩与末地传送门框这两种可达实例。
