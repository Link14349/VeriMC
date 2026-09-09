# 区块生命周期模型（已实现，判据为显式输入）

本文件原先只是设计说明。区块状态、ticking 判据与队列持久化现已实现并有原版差分，
下面区分**已实现**与**明确不实现**两部分。

默认行为完全没变：不声明任何区块状态时，整张图等价于 `entityTicking`，
热路径只多一次 `chunkStates.empty()` 判断。

## 原版里真正决定行为的判据

| 机制 | 26.2 判据 | 位置 |
|---|---|---|
| 方块计划刻是否执行 | `LevelTicks` 的 `tickCheck` = `ServerLevel::isPositionTickingWithEntitiesLoaded` | `ServerLevel.java:209`、`:1799` |
| 计划刻的存储 | 每个区块一个 `LevelChunkTicks`，随区块加载/卸载 `addContainer` / `removeContainer` | `LevelTicks.java:50`、`:61` |
| 方块事件（活塞等） | `runBlockEvents` 只在 `shouldTickBlocksAt(pos)` 时执行，否则**改排到下一刻**而不是丢弃 | `ServerLevel.java:1254-1276` |
| `shouldTickBlocksAt` | `chunkMap.getDistanceManager().inBlockTickingRange(chunkPos)` | `ServerLevel.java:470` |
| 实体阶段 | `isPositionEntityTicking` 另有 `inEntityTickingRange` 判据 | `ServerLevel.java:1803` |
| 跨区块读取 | `Level.updateNeighbourForOutputSignal` 用 `hasChunkAt` 守卫 | `Level.java:1004-1020` |

要点：**方块事件是重排而不是丢弃**，**计划刻是留在容器里等区块重新可 ticking**。
两者都不是「丢事件继续」，与 R11 的约定不冲突。

`LevelTicks.sortContainersToTick` 对 `tickCheck` 为假的容器**什么都不做**：既不取出也不清理，
下一刻再试。因此恢复后所有过期的计划刻会按原有触发时刻与插入序号一次收齐。

## 已实现

1. **显式区块状态**。每个区块一个状态：
   `unloaded` / `loaded`（可读写但不 ticking）/ `blockTicking` / `entityTicking`。
   由工程文件或 `setChunkState` 显式给出，**不做自动加载，也不模拟票据传播时序**。
   映射到原版的记录里另存 `stalledSince`，即该区块停止执行方块实体的时刻。
2. **判据接入点**：
   - `BlockTicks::collect` / `nextTick` 带 `TickCheck` 谓词，跳过不可 ticking 的区块容器头，
     触发时刻与插入序号保持不变（与 `sortContainersToTick` 一致）；
   - 方块事件（阶段 1）在区块不可 blockTicking 时**改排到下一刻**；
   - 方块实体（阶段 2）要求 `blockTicking`；实体接触（阶段 3）要求 `entityTicking`，否则顺延；
     原版 `LevelChunk.isTicking` 还要求实体数据已加载，当前显式状态输入假定该条件已满足，
     不模拟实体数据异步加载窗口。之前把阶段 2 也要求 entityTicking 是错误的；
   - `updateComparatorNeighbors` 对 `unloaded` 位置跳过（对应 `hasChunkAt`）；
   - 写入 `unloaded` 区块直接报错，不静默成功。
3. **倒计时冻结**。原版里方块实体的冷却是每次 tick 递减一次，区块不 tick 就不递减。
   内核把冷却存成绝对时刻，因此在区块恢复时把 `readyAt` / `firstTick` / `candidateTick`
   整体后移「停摆时长」，等价于冻结。恢复到 blockTicking 即恢复方块实体，
   blockTicking 与 entityTicking 之间切换不冻结、不额外平移计时。计划刻**不**后移。
4. **加载环不变量**。原版可 ticking 的区块，其周围八个区块的票据等级必然至少是已加载。
   `setChunkState` 把它作为输入约束强制执行：可 ticking 的区块旁边不能有 `unloaded`。
   因此 ticking 的逻辑永远读不到未加载的方块，**读路径不需要任何额外判断**。
   代价是要卸载一个区块必须先把它周围一圈降级成 `loaded`，这正对应原版的加载环。
5. **队列持久化**。计划刻本来就按区块分桶且随工程文件保存；
   区块状态与 `stalledSince` 作为可选的 `chunkStates` 表写入，
   默认（无非默认区块）时**完全不写**，旧工程文件逐字节不变。
   `profile.loadedRegionOnly` 仍为 true：世界不会自动加载区块。

## 原版差分

2026-09-09 收尾补充：`java26_2BlockTicking`（`captureBlockTicking.py`）单独覆盖
blockTicking=true、entityTicking=false 的中间状态。21 帧 / 2 点，漏斗在第 9 刻继续搬运，
恢复实体 ticking 后第 17 刻再次搬运，严格 vanilla-only 捕获与内核匹配。
这证实方块实体门禁不能使用 entityTicking。详见 [收尾审计](layeredAudit.md)。

`captureChunkLifecycle.py` 用严格 vanilla-only 服务器在**选定的、按区块对齐的原点**上捕获
（GameTest 自选随机原点，做不到这一点；这两个 fixture 因此带 `requiresAlignedOrigin`，
`auditReference.py` 会跳过它们）。

每个区块里同时放这些待办：中继器链（方块计划刻）、活塞（方块事件）、漏斗接箱子（方块实体冷却）、
正在播放的唱片机（方块实体计时）、一个刚被振动事件激活的幽匿感测体（传播/冷却倒计时），
以及**两台在区块停摆时仍在运动中的活塞**（`moving_piston` 方块实体的进度计数）。
两台分别在 4 gt 与 5 gt 通电，停摆时进度停在 1.0 与 0.5，因此恢复后相差一刻先后落地。
第一个区块及其区内邻居的票据在 6 gt 撤掉，使它离最近的强加载区块有两格——
**已加载但既不 block ticking 也不 entity ticking**；40 gt 再还回去。
第三个区块全程保留票据作为对照。

捕获逐帧记录原版自己的 `shouldTickBlocksAt`、`isPositionEntityTicking` 与 `hasChunkAt`，
检查器把它们与内核的区块状态按**一帧偏移**比较（帧 T 的命令决定推进到帧 T+1 的那一刻，
也正是原版报告状态变化的那一刻）。因此「状态是输入」不等于「随便填」：填错会同时被
状态比较和行为比较抓住。

`java26_2ChunkLifecycle`（原点 `[16,-59,32]`）与
`java26_2ChunkLifecycleNegative`（原点 `[-64,-59,-80]`，负区块下标覆盖
`BlockTicks::chunkAt` 的向下取整）两个场景，60 刻 / 40 点，全部逐帧一致。

实测到的原版行为，内核现在逐项复现：

- 停摆期间中继器链、活塞、漏斗全部不动；
- 恢复后过期的计划刻按原触发顺序一次跑完（先前排下的通电与断电两次都保留）；
- 漏斗的 8 gt 冷却在停摆期间**冻结**：停摆时剩 3 gt，恢复后第 3 刻才搬运，
  而不是恢复当刻立即搬运；
- 唱片机的播放计时同样冻结：停摆时 `elapsed` 停在 5，对照区块一路数到 50 以上，
  恢复后才继续；
- 幽匿感测体的激活/冷却倒计时也冻结：对照区块在 46 gt 就回到 inactive，
  停摆区块要到恢复之后才走完同样的过程；
- **运动中的活塞进度也冻结**：两台活塞在 5 gt / 6 gt 进入运动（两格都是 `moving_piston`），
  对照区块分别在 7 gt / 8 gt 落地并点亮灯；停摆区块一直停在 `moving_piston`，
  直到 41 gt 恢复后才分别在 41 gt / 42 gt 落地——先后差一刻，正好对应冻结时各自的进度。
  用的是「`moving_piston` 不导电，灯亮就说明运动真的结束了」这一读数，不是间接推断。

核心测试另有「停摆中保存/读取后冷却原样恢复」「未加载区块拒绝写入」
「可 ticking 区块旁不能有未加载区块」三项。

## 明确不实现

- **不模拟票据传播时序**。区块状态是显式输入；本轮 fixture 里的切换时刻是从原版实测读出来的，
  不是内核算出来的。依赖「什么时候会卸载」的机器仍然不受支持。
- **不做自动加载**。写入未加载区块报错，而不是像原版那样把区块加载进来。
- **随机刻**仍然关闭（见参考验证说明），本模型不改变这一点。
- 漏斗冷却、唱片机播放计时、幽匿感测体倒计时、**运动中的活塞进度**都有原版差分。
- `requiresAdjacentChunksToBeTicking` 已实现，见下节；先前把它只归给未实现的幽匿尖啸体是**错的**，
  `SculkSensorBlockEntity.VibrationUser`（含校准感测体继承的那个）同样返回 `true`。
  仍未实现的是尖啸体本身这个器件。
- 编辑器界面尚未显示区块状态；目前只能通过工程文件与接口设置，用户在 UI 上看不到
  「电路为什么停住」。这一条仍然开着。

## 已实现：振动投递要求相邻区块 ticking

`VibrationSystem.Ticker.receiveVibration` 第一句就是
`if (user.requiresAdjacentChunksToBeTicking() && !areAdjacentChunksTicking(level, destination)) return false;`
（`VibrationSystem.java:347`），而 `SculkSensorBlockEntity.java:136` 返回 `true`。
`areAdjacentChunksTicking`（`VibrationSystem.java:363`）要求监听者所在区块的 **3×3 全部**
`shouldTickBlocksAt` 且已加载——是 **blockTicking**，不是 entityTicking。

返回 `false` 时原版**既不投递也不清 `currentVibration`**，`hasChanged` 保持 `false` 因此
连 `onDataChanged` 都不触发；`travelTime` 已经减到 0，下一刻 `decrementTravelTime`
仍是 0，于是**每刻重试**直到相邻区块恢复。内核 `tickVibration` 在
`remaining==0` 的投递点做同样的判断：不投递、不清 `current`、把自己排到下一刻。
`chunkStates` 为空（缺省整图 entityTicking）时 `adjacentChunksTicking` 直接返回 true，
热路径没有额外开销。卡住期间 `current` 存在而 `remaining==0`，运行快照的
感测体一致性校验为这一种状态开了口子。

**证据边界**：这一条目前只有核心单元回归（缺省行为不变、被挡住时不投递也不丢弃、
停摆中存读往返、校准感测体走同一条路径），**还没有原版差分**。
它要求区块边界落在已知的相对坐标上，只能走
`captureChunkLifecycle.py` 那条按区块对齐原点的严格 vanilla-only 捕获，
与本轮正在进行的其他捕获串行排队。在拿到差分之前，
上面这段是**从原版源码推出的期望**，不是实测到的原版行为。
