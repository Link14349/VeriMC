# 刻内更新轨迹（Java / C++ 逐事件对照）

目标是把「逐刻末状态一致」升级为「刻内更新执行序列一致」。本文件描述已实现的设施、
它的证据强度和明确不覆盖的部分。

## 原版侧不改任何游戏代码

26.2 自己提供了钩子：`CollectingNeighborUpdater.setDebugListener(Consumer<BlockPos>)` 是 public 方法，
`runUpdates` 在**每次从更新栈取出栈顶对象时**调用 `nextUpdates.forEachUpdatedPos(debugListener)`。
各类更新对象上报的坐标是：

| 更新种类 | 上报坐标 |
|---|---|
| `MultiNeighborUpdate` | 源坐标沿 `UPDATE_ORDER`（西、东、下、上、北、南）除跳过方向外的六个邻居 |
| `ShapeUpdate` / `SimpleNeighborUpdate` / `FullNeighborUpdate` | 目标坐标本身 |

`ServerLevel.tick` 在每刻末尾把监听器清空（没有调试订阅者时），
所以捕获器在每个场景刻的回调里重新装上；这样它覆盖紧接着的那个服务器刻以及本刻的命令。

### 记录更新类型与来源（后续补足）

只有坐标不足以区分「同一格上不同种类的更新」。公开钩子给不出这些信息，
因此捕获器在监听器触发的那一刻**读一次** `CollectingNeighborUpdater.stack` 的栈顶对象，
把它的种类和参数一并记下来。这是纯观察：不写回任何东西，
而且下面的开关对照证明有无监听器的时间线完全相同。

每次「取栈顶」记一条，格式为紧凑数组（坐标相对捕获原点）：

| 条目 | 含义 |
|---|---|
| `["m", x,y,z, 跳过方向或 null, 已推进下标, 来源方块]` | `MultiNeighborUpdate` |
| `["s", x,y,z, nx,ny,nz, 方向, 更新标志]` | `ShapeUpdate` |
| `["n", x,y,z, 来源方块]` | `SimpleNeighborUpdate`：执行时才读目标状态 |
| `["f", x,y,z, 来源方块, 状态快照 id, movedByPiston]` | `FullNeighborUpdate`：带入队时的目标状态 |
| 裸整数 | 场景刻标记 |

多向更新的「已推进下标」把**新取出的**对象和**被重新取出的半消费**对象区分开，
这在只记坐标的旧格式里是看不出来的。条目数因此从「每个受影响坐标一条」变成
「每次取栈顶一条」，`java26_2UpdateTrace` 从 3,327 条变成 1,280 条，信息量反而增加。

## 内核侧记录同一个点

`Simulator::enqueue` 的更新循环在「取栈顶」这一步记录同样的坐标集合。
只有换了栈顶对象才算一次新的取出，与原版内层 `while (addedThisLayer.isEmpty())` 的语义对应：

- 子更新没有新增更新且对象未耗尽 → 同一个对象继续，不重复记录；
- 有新增更新 → 新栈顶，记录；
- 对象弹出 → 下一个栈顶，记录（原版同样会重新 peek 之前的多向更新对象）。

`updateTraceLimit` 为 0 时完全关闭，热路径只多一次分支判断。

## 刻边界与容量

两侧在每个场景刻开始处、执行该刻命令之前插入一个整数刻标记，因此轨迹的分段是：
`标记 T` → `该刻命令引发的更新` → `随后一个服务器刻内的更新` → `标记 T+1`。

容量耗尽时置 `updateTraceTruncated` 并停止记录。**截断不判为通过**：
`checkReference` 与核心差分测试在任一侧截断时直接失败，不比较截断前缀。

## 用轨迹发现并修正的实现差异

第一次跑机器矩阵时轨迹就报出差异：C++ 在多向更新的最后一个子更新之后**多留了一轮**才出栈，
于是当那个子更新新增了更新时，多向对象会被重新取出并**重复上报一次六个坐标**。
原版 `MultiNeighborUpdate.runNext` 在推进下标后立即返回 `idx < 6`，
外层 `while (addedThisLayer.isEmpty())` 收到 false 就当场出栈。

已按原版对齐：先取当前方向、再跳过被排除方向、用推进后的下标判断是否还有剩余，
耗尽时在同一轮出栈；构造多向更新时也按原版先跳过首个被排除方向。
子更新的执行顺序本来就相同，这次改动只影响出栈时机、`statistics.updates` 计数与轨迹。

## 用补足后的轨迹发现并修正的实现差异

记上类型和来源之后，同一批场景立刻又报出四处差异，全部是真实的实现缺口：

1. **活塞头转发通知时丢了来源方块**。原版 `PistonHeadBlock.neighborChanged`
   把**收到的来源方块**原样转发给活塞本体；内核转发时用了默认值（空气）。
   `java26_2MachineMatrix` 轨迹第 5730 条：期望
   `["n", 7,2,4, "minecraft:piston_head"]`，实得 `["n", 7,2,4, "minecraft:air"]`。
2. **比较器输出通知本该是带快照的形式**。原版 `Level.updateNeighbourForOutputSignal`
   对每个命中的比较器发 `FullNeighborUpdate`，快照是**入队那一刻**的比较器状态；
   内核发的是简单形式，等于总是在执行时重新读世界。
   同一处还有一点：展示框路径原版传的 `changedBlock` 是 `Blocks.AIR`，不是挂靠方块。
3. **直线型铁轨的放置通知**。`BaseRailBlock.updateState` 只对
   `powered_rail` / `detector_rail` / `activator_rail` 发通知，而且是带快照的形式；
   可弯折的 `rail` 不发。`DetectorRailBlock.updatePowerToConnected` 同样带快照。
4. **`setItem` 是否自带 `setChanged` 因方块实体类而异**。
   `BaseContainerBlockEntity.setItem` 自己调用 `setChanged`，所以每写一格就通知一次比较器；
   `HopperBlockEntity` 重写了 `setItem` 且**不**调用，饰纹陶罐走
   `ContainerSingleItem.setItem` 也不调用。内核原先一律只在末尾通知一次。

前三条属于「通知来源方块」这一族（R1），第四条是容器写入的通知次数。
四条都由新增的 `java26_2UpdateSource` 场景覆盖。

## 已验证

- `captureUpdateTrace.py` → `java26_2UpdateTrace.json`：拉杆、四格粉线、中继器、红石灯、
  比较器读桶、以及一个准连接活塞，14 刻 / 9 点，轨迹 **3,327 条**，逐条相同。
- **跟踪不改变执行语义**：`vanillaReplay.py` 对每个带轨迹的场景在**同一绝对原点、同一时钟、
  同一功能开关**下各跑一次开启和关闭轨迹的捕获，`frames` 与 `commands` 完全相同、原点相同。
  这是同坐标同环境的开关对照，替代了早先原点不同的那次比较。
  它证明的是「这些场景下装监听器不改变时间线」，不是普遍无扰动定理。
- **负对照**：把轨迹里第 100 条改成 `[99,99,99]`，`checkReference` 报
  `firstTraceDifference` 并给出前后各若干条上下文；把 `updateTraceTruncated` 改成 true，
  直接报错而不是 match。
- `captureUpdateSource.py` → `java26_2UpdateSource.json`：14 刻 / 19 点，轨迹 3,831 条，
  其中 18 条是带快照的 `["f", ...]`。覆盖箱子/漏斗/饰纹陶罐直接相邻与隔一格导体两种命中路径、
  四种铁轨的放置（`rail` 作为不发通知的对照）、以及矿车到达探测铁轨时对相连铁轨的通知。
- `captureMachineMatrix.py` → `java26_2MachineMatrix.json`：同一台准连接活塞机器建 22 份，
  其中 16 份分布在四个 x 偏移 × 四个 z 偏移上（低位坐标不同，红石粉七元素集合的桶序不同），
  另 6 份覆盖活塞的六个朝向；一半先放活塞再放线路，另一半相反，覆盖两种放置历史。
  14 刻 / 88 点，轨迹 **31,740 条**逐条相同。捕获原点 `[-13580624, -58, -6554385]`，
  x 与 z 都是负值，负坐标因此是实测覆盖而不是推断。
- 全量场景重新从原版捕获时，两个带轨迹的场景都在**新的随机原点**上重新采集轨迹并逐条通过。

## 待执行队列逐项对照（`java26_2ScheduledQueue`）

`LevelTicks` 没有公开的列举接口，因此捕获器**只读地**反射 `allContainers` 里每个
`LevelChunkTicks` 的 `tickQueue`，按原版自己的 `ScheduledTick.DRAIN_ORDER`
（触发刻 → 优先级 → 子刻序号）排序后，逐帧记录整份待执行集合：
相对坐标、方块名、相对当前刻的触发时刻、优先级。
子刻序号本身是原版内部流水号，**不记录**，只记录它产生的顺序。
`ServerLevel.blockEvents` 用同样的方式逐帧记录（正常情况下它在同一刻内就被
`runBlockEvents` 排空，因此多数帧为空；留着这一项是为了让「被重排」的情况不会被静默忽略）。

内核侧对应 `pendingBlockTicksJson()`（未来队列，不含本刻已收集的批次）与
`pendingBlockEventsJson()`（阶段 1 事件，按插入序号排序）。

`captureScheduledQueue.py` → `java26_2ScheduledQueue`：24 刻 / 21 点，
逐帧比较 **108 条**队列条目（单帧最多 13 条）。覆盖四种延迟的中继器、火把、比较器、侦测器、
活塞的方块事件，以及一条粉线同刻给八个中继器排刻的「爆发」组——
后者的相对顺序只来自优先级与插入序号，实测原版的插入顺序不是坐标单调的
（z = 9, 8, 7, 6, 10, 11, 12），内核逐项复现。

## 随机源消耗逐帧对照（`java26_2RandomState`）

其余对照比的都是**结果**，这一项比的是**随机源本身**：每帧记录 `Level.random` 的 48 位内部状态。
只有两侧从同一算法抽了同样多的值，这个数才可能相同，因此它能抓住
「某个器件多抽或少抽了一次」——这种差异在结果上往往要很久之后才显现。

`Level.random` 是 `LegacyRandomSource`，`seed` 字段是私有的，捕获器**只读地**反射它；
唯一的写入是场景显式要求的那一次 `setSeed`。内核侧对应 `Simulator::randomState()`。

为了把器件随机与世界随机分开，**只在这类场景里**关掉六条生成规则与 `advance_weather`：
生物生成与天气循环每刻都从同一个随机源抽值，而这两者都不在模型内。
`random_tick_speed` 本来就是 0。

`captureRandomState.py` → `java26_2RandomState`：20 刻 / 7 点，随机状态在时间线上变化 7 次，
逐帧完全一致。覆盖投掷器的蓄水池选槽（单个非空槽也抽）、堆肥桶每次投入的概率判定，
以及不抽值的音符盒与中继器链作为对照。

**注意**：投掷器必须朝向容器。向空气抛出会产生外部动作（`itemEjected`），
内核按约定暂停并保留状态，重放无法继续——这一点在设计这类场景时必须避开。

## 明确不覆盖

- 轨迹只包含**邻居更新与形状更新**的序列。方块计划刻与方块事件、随机源消耗各有独立对照
  （见上面两节）；**方块实体阶段的执行顺序仍然没有对照**——原版没有可列举的队列，
  顺序来自区块的方块实体列表。
- 现在记录了更新类型、来源方块、形状更新的方向与标志、多向更新的下标、
  以及带快照更新的快照状态；**仍然不记录** `Orientation`（实验红石专用，非实验目标下恒为 null）
  与更新的调用栈来源。
- 快照状态只在 `FullNeighborUpdate` 条目里；简单更新执行时读到的实际状态不在轨迹中。
- 轨迹只在开启的场景里采集；其余场景仍然只比较逐刻末状态。
- 预算耗尽（R11）的行为差异不受此设施影响：内核仍然暂停并保留状态。
