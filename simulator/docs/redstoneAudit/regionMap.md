# 红石逻辑区域对照表（Minecraft Java 26.2 ↔ VeriMC simulator）

本文件把 Minecraft Java Edition 26.2 正式版中与红石有关的实现区域，逐块映射到本仓库 C++ 内核的文件与函数，并标注既有测试覆盖与未覆盖条件。

## 参考与工作条件

- 原版参考：`F:/学习和研究/VeriMC/simulator/.cache/reference/game.jar`，SHA256 `183c0499c5f855570ee487dd38e141a53f0121f83a0b07a3bac2d8b6698823e8`；保留原始类名的官方 JAR，不是官方源码仓库。
- 反编译产物：同目录 `sources/`（Vineflower 1.12.0）。本次审计读取的是该目录下的实际文件，行号引用均以这些文件为准。JAR、反编译源码、缓存世界均不进入 Git。
- Opus 静态委派没有运行游戏或测试；主代理已完成本次实际复跑，结果分别见 `differentialResults.json`（22 组通过）与 `confirmedCounterexamples.json`（3 个已复现差异）。本表 L1/L2 仍仅表示原始静态/历史证据，不把整行全部分支自动升级为动态通过。
- C++ 侧对应工作树 `cc6b156`，行号以该提交为准。

## 证据等级定义

| 等级 | 含义 |
| --- | --- |
| L0 仅定位 | 已确认原版类/方法与 C++ 对应位置，本次未逐句比对语义 |
| L1 静态核对 | 本次逐句读取两侧代码并比对，结论写在 [implementationReview.md](implementationReview.md) |
| L2 已有差分记录 | 仓库中已有原版 GameTest/导出 fixture 与逐刻对照记录（见 [referenceValidation.md](../referenceValidation.md)） |
| L3 本次实际复跑 | 本次由主代理运行的具体场景；见 `auditReport.md` 与两份结果 JSON，不代表整类器件等价 |

"注册表里有这个方块"不等于仿真支持。C++ 的 `BlockType::supportLevel`（`src/blockRegistry.cpp:92-110`）把方块分为 `implemented` / `partial` / `externalStimulus` / `unimplemented`，只有非 `unimplemented` 才允许 `Simulator::place`（`src/simulator.cpp:175`）。下表的"支持级别"列写的是这个值。

---

## 1. 世界写入与更新框架

| 原版类 / 关键方法 | C++ 对应 | 支持级别 | 证据 | 未覆盖条件 |
| --- | --- | --- | --- | --- |
| `Level.setBlock(pos,state,flags,limit)`（Level.java:222-268）：先 `LevelChunk.setBlockState`，再 flag1→`updateNeighborsAt`、`hasAnalogOutputSignal`→`updateNeighbourForOutputSignal`，再 flag16→三段形状更新 | `Simulator::setBlock`（src/simulator.cpp:109-154） | implemented | L1 | 无区块加载/卸载、无 `sendBlockUpdated`、无 POI、无光照 |
| `LevelChunk.setBlockState`（LevelChunk.java:296-353）：`blockChanged \|\| newBlock instanceof BaseRailBlock` + `((flags&1)!=0 \|\| movedByPiston)` → `affectNeighborsAfterRemoval`；`(flags&512)==0` → `onPlace` | src/simulator.cpp:133（onRemove 条件）、137（onPlace 条件） | implemented | L1 | **C++ 在 flag64（活塞移动）时完全不调用 onRemove**，见 review R2 |
| `Level.updateNeighborsAt(pos, sourceBlock)` → `ServerLevel.updateNeighborsAt`（ServerLevel.java:1165-1172）→ `CollectingNeighborUpdater.updateNeighborsAtExceptFromFacing` | `Simulator::updateNeighbors`（src/simulator.cpp:105） | implemented | L1 | **source 方块取值与原版不一致**，见 review R1 |
| `Level.updateNeighbourForOutputSignal`（Level.java:1004-1020）：四水平方向找比较器，隔一格导体再找一次 | `Simulator::updateComparatorNeighbors`（src/devices.cpp:30-39） | implemented | L1 | 原版走 `neighborChanged(state,…)` 携带快照状态；C++ 执行时重新读世界（见 review R13） |
| `CollectingNeighborUpdater`（CollectingNeighborUpdater.java:68-112）：`addAndRun` / `runUpdates` / `MultiNeighborUpdate.idx` | `Simulator::enqueue` + `updateStack`/`addedUpdates`（src/simulator.cpp:74-104） | implemented | L1 | 超预算行为不同：原版记录日志并**丢弃后续更新继续执行**，C++ 抛异常并标记 `faulted`（有意偏离，见 review R11） |
| `NeighborUpdater.UPDATE_ORDER = {W,E,D,U,N,S}`（NeighborUpdater.java:18） | `updateOrder`（include/simulator/types.hpp:14） | implemented | L1 | — |
| `BlockBehaviour.UPDATE_SHAPE_ORDER = {W,E,N,S,D,U}`（BlockBehaviour.java:86-88） | `shapeOrder`（include/simulator/types.hpp:15） | implemented | L1 | — |
| `Block.updateFromNeighbourShapes`（Block.java:200-210） | 仅在活塞落地时以 `survives()` 近似（src/pistons.cpp:170-175） | partial | L1 | **不做真实 6 向 updateShape**，见 review R8 |
| `Block.updateOrDestroy`（Block.java:212-235）：air→`destroyBlock(flags3)`，否则 `setBlock(flags & ~32)` | src/simulator.cpp:341、337 | implemented | L1 | 掉落物不建模 |
| `BlockStateBase.updateNeighbourShapes` / `updateIndirectNeighbourShapes` | src/simulator.cpp:141-146、`Simulator::indirectShapes`（310-320） | implemented | L1 | 仅红石粉重写了 indirect（与原版一致），其余方块无 indirect 行为 |
| `NeighborUpdater.executeShapeUpdate`（NeighborUpdater.java:36-59）含 `(flags&128)!=0 && is(REDSTONE_WIRE)` 跳过 | `Simulator::executeShape`（src/simulator.cpp:321-355） | implemented | L0 | **C++ 未实现 flag128 跳过分支**；该 flag 在 26.2 主线路径中未被使用，需主代理确认 |
| `ServerLevel.tick` 阶段序：blockTicks → fluidTicks → chunkSource → blockEvents → entities → blockEntities（ServerLevel.java:353-452） | `ScheduledEvent::key()` 的 phase 重排（include/simulator/blockTicks.hpp:19）、`Simulator::stepEvent`（src/simulator.cpp:497-537） | implemented | L2（tickScheduling.md、java26_2Scheduler.json、java26_2TickBatches.json） | 无流体刻、无 raid、无区块源刻 |

## 2. 信号读取

| 原版 | C++ | 支持级别 | 证据 | 未覆盖条件 |
| --- | --- | --- | --- | --- |
| `SignalGetter.getSignal`（SignalGetter.java:65-69）：`state.getSignal` + `isRedstoneConductor` 时叠加 `getDirectSignalTo` | `Simulator::signal`（src/simulator.cpp:27-34） | implemented | L1 | — |
| `SignalGetter.getDirectSignalTo`（17-46） | `Simulator::signal` 中的 conductor 循环（simulator.cpp:32） | implemented | L1 | — |
| `SignalGetter.getBestNeighborSignal`（90-105）、`hasNeighborSignal`（76-88） | `Simulator::bestSignal`（35-39） | implemented | L1 | — |
| `SignalGetter.getControlInputSignal`（48-59） | `Simulator::diodeSideInput`（356-366） | implemented | L1 | — |
| `BlockBehaviour.getSignal/getDirectSignal/ownSignal/isSignalSource` 的逐状态取值 | 由 `tools/reference/ExportReference.java:58-59` 用原版 API 导出成 `weak/strong` 数组（include/simulator/blockRegistry.hpp:32） | implemented | L2 | 依赖运行时的红石粉、比较器、陷阱箱及唱片机在 C++ 中单独特判（simulator.cpp:17-31）；`EmptyBlockGetter` 导出对其余方块成立，未逐一复核 |

## 3. 红石粉与 evaluator

| 原版 | C++ | 支持级别 | 证据 | 未覆盖条件 |
| --- | --- | --- | --- | --- |
| `DefaultRedstoneWireEvaluator.updatePowerStrength`（:18-38）含 7 元素 `Sets.newHashSet` 遍历顺序 | `Simulator::updateWire`（src/simulator.cpp:287-304），`javaPosBucket` 复刻 HashMap 桶序（simulator.cpp:10-13） | implemented | L1 + L2（java26_2Redstone.json） | — |
| `RedstoneWireEvaluator.getIncomingWireSignal`（:29-47） | src/simulator.cpp:289-297 | implemented | L1 | — |
| `RedStoneWireBlock.getConnectingSide / getMissingConnections / getConnectionState`（:124-167、236-258） | `Simulator::wireSide` / `connectionSides` / `wireConnections`（simulator.cpp:265-286） | implemented | L1 | — |
| `RedStoneWireBlock.updateShape`（:169-194） | `Simulator::executeShape` 的 wire 分支（simulator.cpp:344-354） | implemented | L1 | — |
| `RedStoneWireBlock.onPlace / affectNeighborsAfterRemoval / updateNeighborsOfNeighboringWires / checkCornerChangeAt`（:302-340） | `Simulator::onPlace`/`onRemove` wire 分支（220、239）、`wireCorners`（305-309） | implemented | L1 | source 方块不一致（review R1） |
| `RedStoneWireBlock.updateIndirectNeighbourShapes`（:210-234） | `Simulator::indirectShapes`（simulator.cpp:310-320） | implemented | L1 | — |
| `RedStoneWireBlock.useWithoutItem` + `updatesOnShapeChange`（:490-524） | `Simulator::interact` wire 分支（simulator.cpp:582-587） | implemented | L1 | **缺少"连接是否改变"判据**，见 review R9 |
| `ExperimentalRedstoneWireEvaluator` / `ExperimentalRedstoneUtils` / `Orientation` | 无 | **排除** | L0 | 目标为非实验版；`RedStoneWireBlock.useExperimentalEvaluator`（:358-360）在无 `REDSTONE_EXPERIMENTS` feature 时恒 false。C++ 完全不建模 `Orientation`，这也意味着一旦开启实验规则将全部失效 |

## 4. 计划刻与方块事件

| 原版 | C++ | 支持级别 | 证据 | 未覆盖条件 |
| --- | --- | --- | --- | --- |
| `LevelTicks` / `LevelChunkTicks` 的按区块收集、每刻 65536 上限、`hasScheduledTick` vs `willTickThisTick` | `BlockTicks`（include/simulator/blockTicks.hpp、src/blockTicks.cpp） | implemented | L2（java26_2Scheduler.json 206 次排期 / 21 刻 181 次回调；java26_2TickBatches.json） | 无跨区块加载/卸载导致的容器创建销毁 |
| `TickPriority`（EXTREMELY_HIGH=-3 … EXTREMELY_LOW=3） | `ScheduledEvent::priority`，`BlockTicks::schedule` 限定 [-3,3]（blockTicks.cpp:11-12） | implemented | L1 | — |
| `ServerLevel.blockEvent` / `runBlockEvents` / `BlockEventData`（ObjectLinkedOpenHashSet 去重） | phase=1 事件 + `scheduledKeys` 去重（src/pistons.cpp:72-75） | implemented | L2（活塞/音符/钟 fixture） | 只建模了活塞、音符盒、钟三类 `triggerEvent`；比较器 `triggerEvent`（ComparatorBlock.java:186-190）、箱子开合动画事件未建模 |
| 方块实体阶段（`tickBlockEntities`）与新建 ticker 加入 `pendingBlockEntityTickers` 的次刻延迟 | `entityOrders` / `HopperState::firstTick` / `JukeboxState::firstTick`（simulator.hpp:172、src/hoppers.cpp:10、src/jukeboxes.cpp:22） | implemented | L2（java26_2Hoppers.json、java26_2Jukeboxes.json） | — |
| 实体刻（介于 blockEvents 与 blockEntities 之间） | phase=3「外部接触」（按钮箭矢、绊线实体）（src/simulator.cpp:520-523） | externalStimulus | L2（java26_2Buttons.json、java26_2Tripwire.json） | 无真实实体运动；接触量由外部输入提供 |

## 5. 二极管

| 原版 | C++ | 支持级别 | 证据 | 未覆盖条件 |
| --- | --- | --- | --- | --- |
| `DiodeBlock.neighborChanged`（:69-86）含 `canSurvive` 失败时掉落+移除+6 向 `updateNeighborsAt` | `executeReactiveNeighbor` repeater/comparator 分支（src/simulator.cpp:408-414） | implemented | L1 | **缺 canSurvive 分支**，见 review R3 |
| `DiodeBlock.checkTickOnNeighbor` / `tick` / `getDelay` / `shouldPrioritize`（:53-103、200-204） | simulator.cpp:408、457-463、367 | implemented | L1 + L2（java26_2Redstone.json） | — |
| `DiodeBlock.getInputSignal` / `getAlternateSignal`（:113-134） | `diodeInput`（356）、`diodeSideInput`（357-366） | implemented | L1 | — |
| `DiodeBlock.updateNeighborsInFront`（:180-186） | `Simulator::notifyFront`（simulator.cpp:107） | implemented | L1 | — |
| `RepeaterBlock.updateShape` 的 LOCKED 重算（:62-80） | simulator.cpp:343 | implemented | L1 | **C++ 排除竖直方向**，见 review R5 |
| `ComparatorBlock.getInputSignal`（:96-118）含物品展示框 | `Simulator::comparatorInput`（simulator.cpp:369-374） | implemented | L1 | **物品展示框输入缺失**（无实体） |
| `ComparatorBlock.refreshOutputState` / `checkTickOnNeighbor`（:146-183） | `Simulator::refreshComparator`（375-384） | implemented | L1 + L2 | — |
| `ComparatorBlockEntity` 内部输出 | `RuntimeData::output`（simulator.hpp:16） | implemented | L2 | — |

## 6. 火把

| 原版 | C++ | 支持级别 | 证据 | 未覆盖条件 |
| --- | --- | --- | --- | --- |
| `RedstoneTorchBlock.tick` 的 60gt 窗口 / 8 次阈值 / 160gt 恢复（:67-87、133-150），`RECENT_TOGGLES` 为世界级列表 | `executeTick` torch 分支（src/simulator.cpp:468-484）、`recentTorchToggles` + `torchToggleCounts`（simulator.hpp:168-169） | implemented | L1 + L2（java26_2Torches.json，195 刻 / 5 点） | — |
| `RedstoneTorchBlock.neighborChanged`（:89-96） | simulator.cpp:407 | implemented | L1 | — |
| `RedstoneTorchBlock.onPlace / affectNeighborsAfterRemoval / notifyNeighbors`（:43-61） | `onPlace`（212）/`onRemove`（240） | implemented | L1 | source 方块不一致（review R1） |
| `RedstoneWallTorchBlock.hasNeighborSignal` | `Simulator::torchInput`（simulator.cpp:368） | implemented | L0 | 本次未打开 `RedstoneWallTorchBlock.java` 逐句核对 |

## 7. 手动输入器件

| 原版 | C++ | 支持级别 | 证据 | 未覆盖条件 |
| --- | --- | --- | --- | --- |
| `LeverBlock.pull / updateNeighbours` | `Simulator::interact` lever 分支（simulator.cpp:578）、`notifyAttached`（108） | implemented | L0 | 未逐句核对 `LeverBlock.java`；`affectNeighborsAfterRemoval` 无 movedByPiston 保护但拉杆推动反应为 DESTROY |
| `ButtonBlock.press / checkPressed / updateNeighbours / tick / entityInside`（:108-187） | `interact` button（simulator.cpp:579）、`updateButton`/`buttonContact`（src/devices.cpp:74-95） | externalStimulus | L1 + L2（java26_2Buttons.json，101 刻 / 16 点） | 箭矢数量由外部输入提供；`ticksToStayPressed` 在 `updateButton` 中硬编码 30（对可被箭触发的木质按钮成立，`Blocks.java:2724-2733/4752-4753`） |
| `BasePressurePlateBlock.checkPressed / updateNeighbours / getPressedTime`（:75-129），`PressurePlateBlock.getSignalStrength`（:42-49），`WeightedPressurePlateBlock` | `Simulator::updatePressurePlate`（devices.cpp:50-72） | externalStimulus | L1 + L2（java26_2Devices.json） | 生物/非生物区分用方块名（`minecraft:stone_pressure_plate`、`minecraft:polished_blackstone_pressure_plate`）代替 `BlockSetType.pressurePlateSensitivity`，与 26.2 的 `BlockSetType.java:85-111` 一致 |

## 8. 侦测器 / 灯 / 铜灯

| 原版 | C++ | 支持级别 | 证据 | 未覆盖条件 |
| --- | --- | --- | --- | --- |
| `ObserverBlock.tick / updateShape / startSignal / onPlace / affectNeighborsAfterRemoval`（:50-130） | `executeTick`（simulator.cpp:466）、`executeShape`（342）、`onPlace`（221）、`onRemove`（243） | implemented | L1 + L2 | `onRemove` **缺 `hasScheduledTick` 条件**，见 review R4 |
| `RedstoneLampBlock.neighborChanged / tick`（:35-56） | simulator.cpp:415、467 | implemented | L1 + L2 | — |
| `CopperBulbBlock.checkAndFlip / onPlace / getAnalogOutputSignal`（:33-75） | simulator.cpp:416、222、44 | implemented | L1 + L2 | 氧化/上蜡随机刻不建模（`WeatheringCopper`） |

## 9. 活塞

| 原版 | C++ | 支持级别 | 证据 | 未覆盖条件 |
| --- | --- | --- | --- | --- |
| `PistonBaseBlock.checkIfExtend / getNeighborSignal`（:104-147） | `Simulator::checkPiston` / `pistonPowered`（src/pistons.cpp:81-92、6-13） | implemented | L1 + L2 | — |
| `PistonBaseBlock.triggerEvent`（:149-222） | `Simulator::pistonEvent`（pistons.cpp:134-162） | implemented | L1 + L2 | — |
| `PistonBaseBlock.moveBlocks`（:272-372），含 `Maps.newHashMap` 清理顺序 | `Simulator::movePistonBlocks`（pistons.cpp:100-133），`bucket` 复刻 HashMap 桶序（122-123） | implemented | L1 + L2 | 通知时携带的 source 方块与原版不同（review R1）；被推方块的原状态未用于通知 |
| `PistonBaseBlock.isPushable`（:224-270） | `Simulator::pushable`（pistons.cpp:14-28） | implemented | L1 | 用方块名白名单代替 `getDestroySpeed()==-1`；`PUSH_ONLY` 分支多了 `!blockEntity` 判定，见 review R10 |
| `PistonStructureResolver`（全类） | `Simulator::resolvePiston`（pistons.cpp:29-71） | implemented | L1 + L2（黏液分支、蜂蜜隔离、13 方块阻塞） | — |
| `PistonMovingBlockEntity.tick / finalTick`（:275-340） | `tickMotion` / `finishMotion`（pistons.cpp:163-183） | implemented | L1 + L2 | 落地状态用 `survives()` 近似（review R8）；实体推动、`getCollisionShape`、NOCLIP 全部不建模 |
| `MovingPistonBlock` / `PistonHeadBlock` | `Device::movingPiston` / `pistonHead`，`survives`（simulator.cpp:170）、`onRemove`（252） | implemented | L0 | 未逐句核对这两个类 |
| `PistonMath` | 无 | 排除 | L0 | 只影响实体碰撞盒 |

## 10. 容器与传输

| 原版 | C++ | 支持级别 | 证据 | 未覆盖条件 |
| --- | --- | --- | --- | --- |
| `HopperBlockEntity.pushItemsTick / tryMoveItems / ejectItems / suckInItems / tryMoveInItem`（:97-348） | `tickHopper` / `transferItem` / `writeStack`（src/hoppers.cpp:52-146） | implemented | L1 + L2（java26_2Hoppers.json 65 刻 / 25 点、java26_2JukeboxHoppers.json） | **物品实体吸取、矿车容器、`getEntityContainer` 的 `nextInt` 抽取全部缺失** |
| `HopperBlock.checkPoweredState / onPlace / getAnalogOutputSignal`（:99-145） | simulator.cpp:438-441、224 | implemented | L1 | — |
| `AbstractContainerMenu.getRedstoneSignalFromContainer` | `Simulator::containerAnalog`（src/containers.cpp:134-146） | implemented | L1 + L2（java26_2Containers.json） | — |
| `ChestBlock` 双箱合并 / `isChestBlockedAt` / `DoubleBlockCombiner` | `containerSlots`（containers.cpp:102-126）、`chestConnection`/`placedChest`/`updateChestShape`（56-100） | implemented | L2（java26_2CopperChests.json 25 刻 / 132 点） | 猫坐箱子不建模 |
| `TrappedChestBlock.getSignal` + `ContainerOpenersCounter` | simulator.cpp:22-24、30；`Simulator::setViewers`（containers.cpp:201-218） | implemented | L2 | 无真实玩家，viewers 由外部输入 |
| `BarrelBlock`（open 属性 + 容器） | `Device::container` + `setViewers` 中的 BarrelBlock 分支（containers.cpp:212） | implemented | L0 | — |
| `DropperBlock.dispenseFrom` + `DispenserBlockEntity.getRandomSlot`（DropperBlock.java:47-79） | `Simulator::dispenseDropper`（src/droppers.cpp:4-48） | partial | L1 + L2（java26_2Droppers.json、java26_2DropperSlots.json 600 次选槽、java26_2DropperMotion.json 36 组） | 只实现 `DefaultDispenseItemBehavior`，抛出物变成外部动作 `itemEjected` |
| `DispenserBlock`（发射器本体行为） | 有 `Device::dispenser` 与 9 槽容量（containers.cpp:21），但 `supportLevel` 仍是 unimplemented | **未实现** | L0 | 见 review R14 |
| `CrafterBlock` | 有 `Device::crafter` 与 9 槽容量，`supportLevel` unimplemented | **未实现** | L0 | 合成触发、`CRAFTING`/`TRIGGERED` 状态、槽位禁用全部缺失 |
| `AbstractFurnaceBlock` / `BrewingStandBlock` / `ShulkerBoxBlock` | 熔炉为 `Device::furnace`、潜影盒为 `container`、酿造台为 unsupported；均 unimplemented | **未实现** | L0 | — |
| `ChiseledBookShelfBlock` + BE（:34-197） | `isBookshelf`/`updateBookshelfSlot`/`canExtractStack`（containers.cpp:26-54、37-46） | partial | L1 + L2（java26_2Bookshelves.json 57 刻 / 11 点） | — |
| `DecoratedPotBlock` + `ContainerSingleItem` | `isDecoratedPot`（containers.cpp:29-31）、`writeStack` 的 setChanged 抑制（hoppers.cpp:65） | partial | L2（java26_2Pots.json 73 刻 / 26 点） | 破罐、掉落图案不建模 |
| `ComposterBlock` + Input/Output/EmptyContainer（:44-480） | src/composters.cpp（全文件） | partial | L1 + L2（java26_2Composters.json、java26_2Composting.json 4176 次隔离插入） | 骨粉产物变成容器写入，不生成实体 |
| `JukeboxBlockEntity` + `JukeboxSongPlayer`（:64-88） | src/jukeboxes.cpp（全文件） | partial | L1 + L2（java26_2Jukeboxes.json、java26_2JukeboxPlayback.json 4265 样本） | 弹出唱片不生成实体 |

## 11. 门 / 活板门 / 栅栏门

| 原版 | C++ | 支持级别 | 证据 | 未覆盖条件 |
| --- | --- | --- | --- | --- |
| `DoorBlock.neighborChanged`（:224-238）、`updateShape`（:87-108）、`canSurvive`（:240-245） | simulator.cpp:417-432、334-339、169 | implemented | L1 | 铰链侧（HINGE）由放置时的玩家朝向决定，`Simulator::place`（177-186）不建模 `getHinge` |
| `TrapDoorBlock.neighborChanged` + `playSound`（含 gameEvent）（:118-143） | simulator.cpp:417-432（emit 在 setBlock 前） | implemented | L1 | — |
| `FenceGateBlock.neighborChanged`（:181-202）：setBlock 在前，声音+事件在后 | simulator.cpp:427-429 的 fenceGate 分支 | implemented | L1 | `IN_WALL` 属性不随邻居墙更新 |
| `DoorBlock/TrapDoorBlock/FenceGateBlock.useWithoutItem` | `Simulator::interactDevice`（devices.cpp:119-124） | implemented | L1 | 铁门/铁活板门显式拒绝手动开合（与 `canOpenByHand` 一致） |

## 12. 铁轨系

| 原版 | C++ | 支持级别 | 证据 | 未覆盖条件 |
| --- | --- | --- | --- | --- |
| `BaseRailBlock.neighborChanged / shouldBeRemoved / updateDir / affectNeighborsAfterRemoval / onPlace`（:63-132） | `updateRail` / `removeRail` / `placeRail`（src/rails.cpp:142-188） | implemented | L1 + L2（java26_2Rails.json 45 刻 / 93 点，rails.md） | — |
| `RailState`（连接、软移除、坡道、place）+ `RailBlock.updateState` | `RailConnection`（rails.cpp:49-139） | implemented | L1（对照 `BaseRailBlock`；`RailState.java` 本次仅定位） | 需主代理核对 `RailState.countPotentialConnections` 与 `place` 的完全对应 |
| `PoweredRailBlock.updateState / findPoweredRailSignal` | `poweredRailPath` + `updateRail`（rails.cpp:153-188） | implemented | L1 | — |
| `DetectorRailBlock.checkPressed / updatePowerToConnected / getAnalogOutputSignal`（:55-159） | `updateDetectorRail` / `setCartInput` / `cartAnalog`（rails.cpp:190-263） | externalStimulus | L1 + L2 | **命令方块矿车的 `getSuccessCount` 分支不建模**；矿车接触由外部输入提供，没有真实矿车运动 |
| `ActivatorRailBlock` | 与 poweredRail 共用 `updateRail` 分支（rails.cpp:180） | implemented | L0 | 激活轨对矿车的作用（TNT 矿车引爆、漏斗矿车禁用）不建模 |

## 13. 绊线

| 原版 | C++ | 支持级别 | 证据 | 未覆盖条件 |
| --- | --- | --- | --- | --- |
| `TripWireHookBlock.calculateState / affectNeighborsAfterRemoval` | `calculateTripwire` / `removeTripwireHook`（src/tripwire.cpp:25-97） | implemented | L2（java26_2Tripwire.json 70 刻 / 121 点，tripwire.md） | 本次仅定位，未逐句核对 `TripWireHookBlock.java` |
| `TripWireBlock.updateSource / checkPressed / entityInside`（南、西单向扫描） | `updateTripwireSource` / `updateTripwire` / `tripwireContact`（tripwire.cpp:9-23、99-119） | externalStimulus | L2 | 实体数量由外部输入；剪刀走 `stimulate({shear:true})` |

## 14. 环境输入类器件

| 原版 | C++ | 支持级别 | 证据 | 未覆盖条件 |
| --- | --- | --- | --- | --- |
| `DaylightDetectorBlock.updateSignalStrength / tickEntity / useWithoutItem`（:54-116） | `Simulator::updateDaylight`（devices.cpp:97-113）、`daylightCos`（18-27） | externalStimulus | L1 + L2（java26_2Daylight.json 11536 样本） | **右键切换 inverted 时缺 BLOCK_CHANGE 游戏事件**，见 review R6；天空亮度与太阳角由外部输入，`EnvironmentAttributes.SUN_ANGLE` 不建模 |
| `TargetBlock.updateRedstoneOutput / getRedstoneStrength / tick / onPlace`（:42-113） | `stimulateDevice` target 分支（devices.cpp:167-189）、`executeTick`（simulator.cpp:487） | externalStimulus | L1 + L2（java26_2Targets.json 53 刻 / 26 点、java26_2TargetStrength.json 4272 样本） | 命中由外部输入，不模拟投射物轨迹 |
| `LightningRodBlock.onLightningStrike / tick / affectNeighborsAfterRemoval`（:84-116） | `stimulateDevice`（devices.cpp:227-232）、`executeTick`（simulator.cpp:489）、`onRemove`（245） | externalStimulus | L1 | 雷击、天气、`RANGE=128` 引雷不建模 |
| `LecternBlock.signalPageChange / changePowered / updateBelow / tick`（:155-181）+ `LecternBlockEntity.getRedstoneSignal`（:172-175） | `stimulateDevice` lectern 分支（devices.cpp:242-265）、`analogOutput`（simulator.cpp:51-56）、`executeTick`（490）、`onRemove`（246） | externalStimulus | L1 | 书本内容/玩家翻页界面不建模 |

## 15. 振动

| 原版 | C++ | 支持级别 | 证据 | 未覆盖条件 |
| --- | --- | --- | --- | --- |
| `GameEvent` 注册表 + `GameEventTags.VIBRATIONS` / `IGNORE_VIBRATIONS_SNEAKING`，`VibrationSystem.VIBRATION_FREQUENCY_FOR_EVENT`（:53-100） | `data/vibrationRules.json`（61 事件）+ `GameEventInfo`（blockRegistry.hpp:11） | implemented | L2（exportVibrations.py） | 本次核对：61 个事件中不存在"可监听但频率 0"或"有频率但不可监听"的条目，C++ 的双条件过滤（vibrations.cpp:75）与原版等价 |
| `EuclideanGameEventListenerRegistry.visitInRangeListeners / getPostableListenerPosition`（:76-124） | `Simulator::emitGameEvent`（src/vibrations.cpp:73-102），按 section 索引 + 注册顺序 | partial | L1 | 只有方块监听器；实体监听器（潜声守卫、蛙卵等）不存在 |
| `VibrationSystem.Listener.handleGameEvent / isOccluded`（:209-275） | vibrations.cpp:82-99、`vibrationOccluded`（39-67） | partial | L1 + L2（java26_2Vibrations.json 161 刻 / 13 点） | — |
| `VibrationSelector.addCandidate / shouldReplaceVibration / chosenCandidate`（全类） | `SensorState::candidate` + vibrations.cpp:89-93、128-134 | partial | L1 | — |
| `VibrationSystem.Ticker.tick / receiveVibration`（:278-361） | `Simulator::tickVibration`（vibrations.cpp:128-147） | partial | L1 | 原版每游戏刻都跑 ticker；C++ 只在有候选/行进时排期（空闲刻无可观测副作用，静态核对一致） |
| `VibrationSystem.User.isValidVibration / calculateTravelTimeInTicks / getRedstoneStrengthForDistance`（:118-121、401-430） | vibrations.cpp:75-76、132、153 | partial | L1 | 潜行/旁观/`dampensVibrations` 由外部 `source` 字段提供 |
| `SculkSensorBlock.activate / deactivate / tick / tryResonateVibration`（:182-243） | `activateSensor` / `tickSensor`（vibrations.cpp:149-176） | partial | L1 + L2（java26_2DeviceVibrations.json 155 刻 / 31 点，vibrations.md） | `affectNeighborsAfterRemoval` 无 movedByPiston 保护，但方块实体不可被推 |
| `CalibratedSculkSensorBlock`（背面频率过滤、半径 16） | vibrations.cpp:83-86、151 | partial | L1 | — |
| `SculkShriekerBlock` / `SculkCatalystBlock` / `SculkSpreader` | 无 | **未实现** | L0 | 尖啸体也是振动监听器且有 `requiresAdjacentChunksToBeTicking`，26.2 注册表里存在但仿真不支持 |

## 16. 声音 / 状态类附属器件

| 原版 | C++ | 支持级别 | 证据 | 未覆盖条件 |
| --- | --- | --- | --- | --- |
| `NoteBlock`（乐器选择、方块事件、`Level.random.nextLong`） | src/notes.cpp（全文件） | partial | L2（java26_2Notes.json 100 刻 / 34 点、java26_2NoteRandom.json 112 次） | 玩家头颅自定义声音由外部输入 |
| `BellBlock` + `BellBlockEntity`（摆动 50 刻） | src/bells.cpp（全文件） | partial | L2（java26_2Bells.json 123 刻 / 34 点、java26_2BellHits.json 2688 次） | 无生物 AI（袭击者高亮） |
| `JukeboxBlock`（HAS_RECORD、ticker 装卸） | src/jukeboxes.cpp | partial | L1 + L2 | — |
| `ComposterBlock` | src/composters.cpp | partial | L1 + L2 | — |
| `CopperGolemStatueBlock` / `RespawnAnchorBlock` / `BeehiveBlock` / `Cauldron` 系 / `EndPortalFrameBlock` | `Device::analog` + `staticAnalog`（blockRegistry.cpp:143-152） | externalStimulus | L0 | 只提供静态比较器读数，本体行为（充能、蜂蜜量、水位变化）不建模 |

## 17. 26.2 注册表中的未实现或部分支持项

以下列出尚未实现的行为。部分器件归为 `Device::unsupported`，另一些已有专用枚举但 `supportLevel=unimplemented`，放置会拒绝；头颅等例外为 partial，不应混称全部不能放置。具体以 `src/blockRegistry.cpp:9-41、92-110` 的两阶段判定为准。

| 原版类 | 红石相关点 | 备注 |
| --- | --- | --- |
| `ShelfBlock`（26.2 中存在） | `POWERED` 随 `hasNeighborSignal` 变化（ShelfBlock.java:108-124）、`SelectableSlotContainer` 比较器输出、`SIDE_CHAIN_PART` 链式连接 | 与雕纹书架同族但独立实现，本次未建模 |
| `CrafterBlock` | 红石触发合成、`TRIGGERED`/`CRAFTING` 状态、槽位禁用、比较器输出 | 已在 `Device` 枚举中占位，见 review R14 |
| `DispenserBlock` | `TRIGGERED` + 分发行为表 | 同上 |
| `CreakingHeartBlock` | `hasAnalogOutputSignal` | — |
| `CommandBlock` / `StructureBlock` / `JigsawBlock` / `TestBlock` / `TestInstanceBlock` | 红石激活 | 超出电路设计范围 |
| `TntBlock` | `neighborChanged` 时被红石点燃 | 用户已排除 TNT 复制机，但 TNT 本体作为受红石驱动器件仍未建模 |
| `BigDripleafBlock` | `neighborChanged` 时按红石信号控制倾斜 | — |
| `AbstractSkullBlock` | `neighborChanged`（凋灵头颅） | 头颅方块本体在 C++ 中被归为 `Device::solid` 以支持音符盒自定义声音 |
| `SpongeBlock` / `LiquidBlock` / `FrostedIceBlock` | 流体相关 `neighborChanged` | 流体已明确排除 |
| `SculkShriekerBlock` | 振动监听器 | 见第 15 节 |
| `CakeBlock` / `CandleCakeBlock` | 虽有 `Device::analog` 分类，但不在 externalStimulus 覆盖表内 | 当前 `supportLevel=unimplemented`，不能只按静态读数宣称支持 |
| `RedStoneOreBlock` | 实体踩踏点亮（非信号源） | 不影响信号 |

## 18. 明确排除的范围

用户明确排除完整生物 AI 机器、TNT 复制机、流体农场和矿车计算机；实验性红石规则不属于固定参考配置。区块加载/卸载、光照、掉落物实体、玩家背包/容器界面和随机刻作物生长则是当前未实现或抽象边界，不据此宣布永久排除。这些边界**不**免除对应器件本体的建模责任——上表第 17 节列出的正是"器件本体仍缺"的部分。

## 19. 机器可读索引

同目录 [regionIndex.json](regionIndex.json) 是本表的机器可读版本，字段与列一一对应，便于后续脚本核对覆盖率。
