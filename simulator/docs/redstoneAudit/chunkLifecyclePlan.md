# 区块生命周期模型设计（未实现）

本文件是**设计说明**，不是实现记录。当前内核把整个世界当作永远加载、永远可 ticking 的
单一区域；任何依赖区块加载/卸载或 ticking 边界的机器目前**不受支持**，
也不能因为普通电路通过就宣称支持。

## 原版里真正决定行为的判据

| 机制 | 26.2 判据 | 位置 |
|---|---|---|
| 方块计划刻是否执行 | `LevelTicks` 的 `tickCheck` = `ServerLevel::isPositionTickingWithEntitiesLoaded`，即 `areEntitiesLoaded(chunk) && chunkSource.isPositionTicking(chunk)` | `ServerLevel.java:209`、`:1799` |
| 计划刻的存储 | 每个区块一个 `LevelChunkTicks`，随区块加载/卸载 `addContainer` / `removeContainer`，并随区块存档持久化 | `LevelTicks.java:50`、`:61` |
| 方块事件（活塞等） | `runBlockEvents` 只在 `shouldTickBlocksAt(pos)` 时执行，否则**改排到下一刻**而不是丢弃 | `ServerLevel.java:1254-1276` |
| `shouldTickBlocksAt` | `chunkMap.getDistanceManager().inBlockTickingRange(chunkPos)` | `ServerLevel.java:470` |
| 实体阶段 | `isPositionEntityTicking` 另有 `inEntityTickingRange` 判据 | `ServerLevel.java:1803` |
| 随机刻 | 只在可 ticking 的区块内发生（本项目已把自然随机刻关掉，见参考验证说明） | — |
| 跨区块读取 | `Level.updateNeighbourForOutputSignal` 用 `hasChunkAt` 守卫；未加载区块的 `getBlockState` 返回 void air | `Level.java:1004-1020` |

要点：**方块事件是重排而不是丢弃**，而**计划刻是留在容器里等区块重新可 ticking**。
两者都不是“丢事件继续”，这一点与项目既有的 R11 约定不冲突。

## 建议的模型

1. **显式区域状态**。为每个区块坐标记录一个状态：
   `unloaded` / `loaded`（可读写但不 ticking）/ `blockTicking` / `entityTicking`，
   由工程文件显式给出，不做自动加载。默认整张图 `entityTicking`，与当前行为一致。
2. **判据接入点**（与上表一一对应）：
   - `BlockTicks::collect` 只收集处于 `blockTicking` 及以上的区块容器；
   - 方块事件阶段对不满足 `blockTicking` 的事件**改排到下一刻**；
   - 方块实体/运动阶段要求 `entityTicking`；
   - `updateComparatorNeighbors` 对 `unloaded` 位置跳过（对应 `hasChunkAt`）；
   - 读取 `unloaded` 区块的方块一律返回空气，写入直接拒绝并报错，不静默成功。
3. **队列持久化**。计划刻已经按区块分桶（`src/blockTicks.cpp` 的 `chunks`），
   卸载时把该桶序列化进工程文件、从活动结构里移除；加载时反序列化并合并。
   `scheduledKeys`、`heads` 需要同步维护，`nextOrder` 保持全局单调。
4. **存档格式**。`.vmcb` 需要新增区块状态表与按区块的计划刻表，属于
   `checkpointAbi` 变更，必须走显式迁移，不能沿用旧文件。
5. **界面与文档**。区域状态必须在编辑器里可见，否则用户无法解释为什么电路停住。

## 未实现之前的表述要求

- 现状必须写成「单一永远加载区域」，不能说“支持区块边界”。
- 依赖区块卸载的机器（例如利用卸载暂停计划刻的电路）标为**未支持**，
  不能用永远加载的世界跑出来的结果冒充通过。
- 本文件本身不构成实现承诺；实现前需要重新评估范围与验收。

## 实现后的验收条件（供将来分单）

1. 最小加载/卸载场景：一个跨区块的电路，卸载一侧后计划刻停住、方块事件改排，
   重新加载后从原状态继续；与原版逐刻对照。
2. 队列持久化：卸载→存档→读档→加载，计划刻的时间、优先级与插入序号完全恢复。
3. ticking 边界：`blockTicking` 与 `entityTicking` 两档分别验证，
   覆盖活塞方块事件、方块实体、运动实体三类。
4. `SculkShriekerBlock.requiresAdjacentChunksToBeTicking` 这类跨区块条件单独验证。
5. 负坐标与区块边界（`x = -1`、`x = 15` 两侧）都要覆盖，
   负坐标整除已经在 `BlockTicks::chunkAt` 显式向下取整。
