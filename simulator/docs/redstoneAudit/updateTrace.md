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

## 已验证

- `captureUpdateTrace.py` → `java26_2UpdateTrace.json`：拉杆、四格粉线、中继器、红石灯、
  比较器读桶、以及一个准连接活塞，14 刻 / 9 点，轨迹 **3,327 条**，逐条相同。
- **跟踪不改变执行语义**：同一时间线用 `python3 captureUpdateTrace.py --no-trace` 再捕获一次，
  两次的 `frames` 与 `commands` 完全相同（原点不同，是 GameTest 的随机原点）。
- **负对照**：把轨迹里第 100 条改成 `[99,99,99]`，`checkReference` 报
  `firstTraceDifference` 并给出前后各若干条上下文；把 `updateTraceTruncated` 改成 true，
  直接报错而不是 match。
- 全量 35 个场景重新从原版捕获时，该场景在**新的随机原点**上重新采集轨迹并逐条通过。

## 明确不覆盖

- 轨迹只包含**邻居更新与形状更新**的坐标序列。方块计划刻的执行顺序、方块事件阶段、
  方块实体阶段、随机源消耗都**不在**这条轨迹里；原版没有对应的公开钩子。
- 上报的是坐标，不是更新类型或来源方块。同一坐标上不同类型的更新在轨迹里无法区分，
  但顺序和数量会被区分。
- 轨迹只在开启的场景里采集；其余场景仍然只比较逐刻末状态。
- 预算耗尽（R11）的行为差异不受此设施影响：内核仍然暂停并保留状态。
