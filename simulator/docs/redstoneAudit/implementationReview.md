# 实现核对报告：Minecraft Java 26.2 原版反编译源码 ↔ VeriMC C++ 内核

## 0. 本次审计的方法与边界

- 阅读对象：`F:/学习和研究/VeriMC/simulator/.cache/reference/sources/` 下 Vineflower 1.12.0 反编译的实际 Java 文件（来源 JAR SHA256 `183c0499c5f855570ee487dd38e141a53f0121f83a0b07a3bac2d8b6698823e8`），以及本工作树 `cc6b156` 的全部 C++ 源码（`simulator/src/*.cpp`、`simulator/include/simulator/*.hpp`，共约 5.4k 行）。
- **Opus 静态委派阶段没有运行 Minecraft、GameTest、ctest 或 Python 测试。主代理随后实际复跑：22 组通过，并复现 R6、R7、R10 三个反例，见 `auditReport.md` 和 `confirmedCounterexamples.json`。** 所有"差异"结论均为静态阅读结果。带 ★ 的条目是**确认的静态差异**（两侧代码都读过并逐句比对）；带 ☆ 的是**待实验假设**（静态上可疑，但触发条件依赖运行时时序，必须用原版差分确认）。原版差分由主代理实际运行。
- 覆盖优先级按授权：容器/漏斗/投掷与外部事件、活塞、振动、附属器件为本报告重点；核心信号/粉线/计划刻/二极管只做交叉核对，主判归主代理。
- 区域映射与覆盖缺口清单见 [regionMap.md](regionMap.md)。

---

## 1. 确认的静态差异

### ★R1 邻居通知携带的"触发方块"取值与原版不一致（已复现并修复，见 [fixProgress.md](fixProgress.md)）

| | 位置 |
| --- | --- |
| 原版 | `DefaultRedstoneWireEvaluator.java:34-36` `level.updateNeighborsAt(blockPos, this.wireBlock)`；`RedStoneWireBlock.java:294`、`:297`、`:308`、`:319`（onPlace / affectNeighborsAfterRemoval / checkCornerChangeAt）；`RedstoneTorchBlock.java:48-53`；`PistonBaseBlock.java:352-365`（`updateNeighborsAt(pos, toUpdate[i].getBlock())`）；接收侧 `DoorBlock.java:230`、`RailBlock.java:29-33` |
| 仿真 | `Simulator::updateNeighbors`（`src/simulator.cpp:105`）默认 `source = world.get(p)`；调用点 `simulator.cpp:220`、`:239`、`:240`、`:303`、`:306-308`；`src/pistons.cpp:129-131` |

**机制**：原版 `Level.updateNeighborsAt(pos, sourceBlock)` 把"发起更新的那个方块"作为 `sourceBlock` 传给**所有**被通知的邻居；`ServerLevel.java:1170-1172` → `CollectingNeighborUpdater.MultiNeighborUpdate`（`:163`）对 6 个方向复用同一个 `sourceBlock`。C++ 的 `updateNeighbors(q)` 在不传 `source` 时取 `world.get(q)`，即"被通知位置自己的方块"，与原版语义不同。

本次在已支持器件中定位到的两类接收方会读这个参数：
- `DoorBlock.neighborChanged`（`DoorBlock.java:230`）：`!this.defaultBlockState().is(block) && signal != POWERED`；
- `RailBlock.updateState`（`RailBlock.java:29-33`）：`block.defaultBlockState().isSignalSource() && countPotentialConnections() == 3`（`RailState.java:127-133`）。

**触发条件**：红石粉强度变化、红石粉/火把放置或移除、活塞推动结束这几条路径上，被通知位置恰好是门的一半或普通铁轨，且其邻居中有另一扇门 / 另一段三向普通铁轨。

**用户可见影响**：三向普通铁轨在应当自动改向时不改向（岔路方向错误）；双开门的上下半在某些更新路径上少收一次供电判定。

**最小反例建议（铁轨）**：
1. y=0 铺石头。
2. 普通铁轨 R 在 (0,1,0)；普通铁轨 R2 在 (1,1,0)，并在 (2,1,0)、(1,1,1) 再放两段普通铁轨，使 R2 的潜在连接数为 3。
3. 红石粉 W 在 (0,1,-1)（R 的北邻居），拉杆驱动 W。
4. 拨动拉杆。原版：`updateWire` 的受影响集合含 R，`updateNeighborsAt(R, redstone_wire)` 使 R2 收到 `block=redstone_wire`（`isSignalSource()==true`）→ R2 重算形状。仿真：R2 收到 `block=rail` → `isSignalSource()==false` → 形状不变。
5. 断言点：R2 的 `shape` 属性。

**最小反例建议（活塞）**：红石块在 (0,2,0)，活塞在 (0,2,-1) 朝 south 推动它；(0,1,0) 放一段潜在连接数为 3 的普通铁轨。原版在清空原点后用 `redstone_block` 通知铁轨并重算形状，仿真通知时该位置已是空气/`moving_piston`。

---

### ★R2 活塞移动路径完全跳过 `affectNeighborsAfterRemoval`（已复现并修复，见 [fixProgress.md](fixProgress.md)）

| | 位置 |
| --- | --- |
| 原版 | `LevelChunk.java:320-322`：`(blockChanged \|\| newBlock instanceof BaseRailBlock) && ((flags & 1) != 0 \|\| movedByPiston)` → `oldState.affectNeighborsAfterRemoval(serverLevel, pos, movedByPiston)` |
| 仿真 | `src/simulator.cpp:133`：`(oldState.type != state.type \|\| isRail(state.device)) && (flags & 1u) != 0 && (flags & 64u) == 0` → `onRemove` |

**机制**：原版在 `movedByPiston` 为真时**仍然调用** `affectNeighborsAfterRemoval`，只是把 `movedByPiston` 传进去让各方块自行决定；C++ 直接用 `(flags & 64u) == 0` 把整条路径关掉。26.2 中不检查 `movedByPiston` 的实现里，可被活塞推动（无方块实体、推动反应非 BLOCK/DESTROY）的有：

- `ObserverBlock.java:126-130`（`POWERED && hasScheduledTick` → `updateNeighborsInFront`）；
- `LightningRodBlock.java:112-116`（`POWERED` → `updateNeighbours`）。

（`SculkSensorBlock.java:121-125`、`ShelfBlock.java:102-105`、`HopperBlock.java:133-135` 等同样没有保护，但都带方块实体，`PistonBaseBlock.isPushable` 末行 `!state.hasBlockEntity()` 已排除。）

受影响的写入点是 `movePistonBlocks` 里的 `setBlock(cell.pos, 0, 82)`（`src/pistons.cpp:124`，flag 82 = 64|16|2，无 flag1）与 `setBlock(destination, moving, 324)`（`:114`，flag 324 = 256|64|4）。

**用户可见影响**：推动一个处于 2 刻脉冲中的侦测器、或推动一个刚被雷击/外部刺激过的避雷针时，原版会在清空原位的瞬间对其输出侧再发一次邻居更新，仿真不会。下游中继器/比较器的计划刻可能因此差一刻。

**最小反例建议**：
1. 侦测器 O 在 (0,1,0) 朝东（检测面 (1,1,0)，输出面 (-1,1,0)）；(-1,1,0) 放中继器，朝向使其以 O 为输入。
2. 改变 (1,1,0) 触发 O；在 O 变为 `powered=true` 后的第 1 刻（其熄灭刻尚在未来队列中，`hasScheduledTick` 为真），用 (0,1,-1) 的活塞朝 south 把 O 推走。
3. 断言点：中继器的计划刻队列内容与 `powered` 时序。

---

### ★R3 二极管的 `neighborChanged` 缺少"无法存活→掉落并移除"分支（已复现并修复，见 [fixProgress.md](fixProgress.md)）

| | 位置 |
| --- | --- |
| 原版 | `DiodeBlock.java:69-86`：`if (getBlockState(pos).is(this)) { if (canSurvive) checkTickOnNeighbor; else { dropResources; removeBlock; for (Direction d) updateNeighborsAt(pos.relative(d), this); } }` |
| 仿真 | `src/simulator.cpp:408`（repeater）、`:409-414`（comparator）只做 `checkTickOnNeighbor` 等价逻辑；移除只发生在形状更新阶段 `src/simulator.cpp:341` |

**机制**：抽掉中继器/比较器下方的支撑方块时，原版在**邻居通知阶段**（`updateNeighborsAt(support, oldBlock)` 走到 UP 方向时）就把二极管掉落并移除，然后额外发出 6 次 `updateNeighborsAt(diodePos.relative(d))`（共 36 次通知）。C++ 在这一刻只会评估是否排计划刻，直到 `Level.setBlock` 后半段的形状更新才通过 `!survives` 删除方块，且不发出那 6 次多向通知。

附带效应：C++ 在移除前可能给该二极管排一个计划刻（`schedule` 会消耗一个 `nextOrder`）。该刻执行时 `stepEvent` 的 `at(event.pos).type == event.type` 检查会跳过它（`src/simulator.cpp:518`），因此不会执行错误逻辑，但队列内容与原版不同。

**用户可见影响**：断支撑的瞬间，同刻内其他器件观察到的世界状态与更新序列不同；依赖"断电顺序"的定位性电路（例如靠拆方块触发的解锁电路）会出现一刻级偏差。

**最小反例建议**：中继器 P 在 (0,1,0)，支撑 S 在 (0,0,0)；在 P 周围 6 个位置放红石粉/火把作探针，并在 P 的输出方向两格处放一个中继器。用编辑器把 S 设为空气，逐事件比较通知序列与各探针的翻转时刻。

---

### ★R4 侦测器移除时缺少 `hasScheduledTick` 条件（已复现并修复，见 [fixProgress.md](fixProgress.md)）

| | 位置 |
| --- | --- |
| 原版 | `ObserverBlock.java:126-130`：`if (state.getValue(POWERED) && level.getBlockTicks().hasScheduledTick(pos, this))` |
| 仿真 | `src/simulator.cpp:243`：`case Device::observer: if (s.powered) notifyFront(p, s.facing); break;` |

**机制**：正常情况下 `POWERED=true` 蕴含存在熄灭刻，两者等价。但 26.2 的 `hasScheduledTick` 查询的是**未来队列**，本刻已被 `collect` 进批次的计划刻不再计入（`BlockTicks` 也是这样实现的，`src/blockTicks.cpp:21-26` 的 `willTick` 与 `hasScheduled` 是两个集合）。于是存在一个窗口：侦测器的熄灭刻已进入本刻批次但尚未执行，此时同批次中更早的事件把该侦测器移除——原版**不**通知输出面，仿真会通知。

**用户可见影响**：同刻内"侦测器被移除"与"侦测器熄灭刻"竞争时，仿真多发一次输出侧更新，可能提前触发下游。

**最小反例建议**：让侦测器 O 与另一台活塞/TNT 无关的破坏源在同一刻批次内排序（用 `captureTickBatches.py` 同款场景），把 O 的熄灭刻与破坏事件安排到同一刻，比较输出面器件的更新次数。注意：C++ 已有正确的原语 `Simulator::hasScheduled`（`src/simulator.cpp:452`），修复必须按被移除的 `oldState.type` 查询队列；`onRemove` 运行时世界位置已写成新方块，不能直接调用会查询当前类型的 `hasScheduled(pos)`。

---

### ★R5 中继器 `locked` 属性不在竖直形状更新中刷新

| | 位置 |
| --- | --- |
| 原版 | `RepeaterBlock.java:62-80`：DOWN 且无法存活 → 空气；否则 `directionToNeighbour.getAxis() != state.getValue(FACING).getAxis()` 时写入 `LOCKED`（Y 轴也满足该条件） |
| 仿真 | `src/simulator.cpp:343`：`s.device == Device::repeater && axis(u.direction) != 0 && axis(u.direction) != axis(s.facing)`，其中 `axis(d)==0` 表示 Y 轴，被排除 |

**机制**：C++ 额外排除了来自上/下方向的形状更新，所以竖直方向的形状更新不会刷新 `locked`。

**用户可见影响**：仅 `locked` 方块状态（即状态 ID）不同。原版的锁定判定在 `DiodeBlock.checkTickOnNeighbor`/`tick` 中用 `isLocked()` 实时计算（`RepeaterBlock.java:82-85`），因此**信号行为不受影响**；但逐刻状态 ID 差分会报错。

**复核说明**：该分支差异属实，但原建议中“flag 2 不产生形状更新”的前提错误。`Level.setBlock` 在 `(flags & 16)==0` 时仍执行形状更新，C++ 亦如此。因此原先通过普通断电留下过时 `locked` 再竖直通知的构造不能作为有效反例。需另行构造可达的过时状态和只发生竖直通知的历史；本次未动态复现 R5，也不把它计入三个已证实反例。

---

### ★R6 阳光探测器切换 `inverted` 时缺少 `BLOCK_CHANGE` 游戏事件（已修复，见 [fixProgress.md](fixProgress.md)）

| | 位置 |
| --- | --- |
| 原版 | `DaylightDetectorBlock.java:80-85`：`setBlock(newState, 2)` → `level.gameEvent(GameEvent.BLOCK_CHANGE, pos, …)` → `updateSignalStrength` |
| 仿真 | `src/devices.cpp:130-134`：`setBlock(..., 2); updateDaylight(pos); return true;`，没有 `emitGameEvent` |

**机制**：`block_change` 在 26.2 的频率表中是 11（`VibrationSystem.java:86`），是可监听事件。仿真的 `interactDevice` 漏发了它。

**用户可见影响**：把幽匿感测体放在阳光探测器旁并右键切换反相时，原版会在 `floor(距离)` 刻后让感测体激活并输出频率 11 的比较器读数，仿真完全没有反应。

**最小反例建议**：阳光探测器 D 在 (0,1,0)，幽匿感测体在 (2,1,0)（距离 2，无羊毛遮挡）；对 D 执行 `interact`；断言感测体在 2 刻后进入 `active` 且 `lastVibrationFrequency == 11`。

---

### ★R7 楼梯的 `shape` 连接属性从不计算，且该属性直接决定支撑掩码（已修复，见 [fixProgress.md](fixProgress.md)；R8 的活塞落地重算仍开放）

| | 位置 |
| --- | --- |
| 原版 | `StairBlock.java:111-128`：水平方向的形状更新会 `state.setValue(SHAPE, getStairsShape(state, level, pos))`；放置时 `getStateForPlacement` 亦计算 |
| 仿真 | `Simulator::executeShape`（`src/simulator.cpp:321-355`）只处理钟、音符盒、铁轨、绊线钩、绊线、门、箱子、存活性、侦测器、中继器、红石粉；`Simulator::place`（`:174-207`）也没有楼梯分支 |

**机制**：楼梯被 `classify()` 的白名单 `name.ends_with("_stairs")` 归入 `Device::solid`（`src/blockRegistry.cpp:39`），`supportLevel` 因此是 `implemented`、可放置，但连接属性从不更新。

**这不只是外观差异**：本次直接查 `data/blockStates.json` 中 `minecraft:oak_stairs` 的导出数据，`shape` 会改变 `supportMask`/`rigidMask`/`centerMask`：

| half=bottom, facing=north | supportMask/rigidMask/centerMask |
| --- | --- |
| `straight` | 5（down + north） |
| `inner_left` | 21（down + north + west） |
| `inner_right` | 37（down + north + east） |
| `outer_left` / `outer_right` | 1（仅 down） |

而 `wireSide`（`src/simulator.cpp:265-268`）、`survives` 的中继器/比较器分支（`:162`）、火把分支（`:163-164`）、压力板分支（`:167`）全部读这三个掩码。

**用户可见影响**：用楼梯搭内角时，原版楼梯的侧面变成 sturdy，可以贴红石火把、可以让红石粉产生 side/up 连接；仿真中该面永远不 sturdy，火把无法存活、粉线不连接。反之在应当是 `outer_*` 的位置，仿真会误认为北面 sturdy。

**最小反例建议**：
1. 在 (0,1,0) 放 `oak_stairs[half=bottom,facing=north]`。
2. 在 (0,1,1) 放 `oak_stairs[half=bottom,facing=east]`（形成内角）。
3. 原版：(0,1,0) 的 `shape` 变为 `inner_left` 或 `inner_right`，`supportMask` 含 west 或 east 位；仿真：仍是 `straight`。
4. 断言点一：两处楼梯的 `stateId`。断言点二：在该侧面放 `redstone_wall_torch`，原版可存活、仿真会被 `survives` 判为不可放置。

另：同一缺陷也让活塞推动后的楼梯保持旧 `shape`（见 R8）。

---

### ★R8 活塞落地状态用 `survives()` 近似 `Block.updateFromNeighbourShapes`（已复现并修复，见 [fixProgress.md](fixProgress.md)）

| | 位置 |
| --- | --- |
| 原版 | `PistonMovingBlockEntity.java:286`（`finalTick`）与 `:317`（`tick`）：`Block.updateFromNeighbourShapes(movedState, level, pos)`，即按 `UPDATE_SHAPE_ORDER` 逐一调用 6 次 `updateShape`（`Block.java:200-210`） |
| 仿真 | `Simulator::finishMotion`（`src/pistons.cpp:170-175`）：只做 `survives()`、红石粉重连、`waterlogged=false` |

**机制**：C++ 用"能否存活"代替了完整的形状重算。对红石粉单独补了 `wireConnections`，对其余方块一律沿用移动前的状态。

**用户可见影响**：被推动的楼梯落地后保持旧 `shape`（叠加 R7 的支撑掩码问题）；将来若把栅栏/墙/玻璃板/铁栏杆加入调色板，其连接属性同样不会重算。

**最小反例建议**：在活塞前方摆一段能形成内角的楼梯组，推动一格，比较落地后所有楼梯的 `stateId` 与红石粉/火把在其侧面的存活性。

---

### ★R9 红石粉"点/十字"右键切换的邻居更新条件与原版不同（已复现并修复，见 [fixProgress.md](fixProgress.md)）

| | 位置 |
| --- | --- |
| 原版 | `RedStoneWireBlock.java:490-524`：只有 `newState != state` 才 `setBlock` 并调用 `updatesOnShapeChange`；后者只在**该方向的连接性（`isConnected()`）确实发生变化**且邻居是导体时才 `updateNeighborsAtExceptFromFacing` |
| 仿真 | `Simulator::interact` 的 wire 分支（`src/simulator.cpp:582-587`）：无条件执行，并对**每个**导体水平邻居发通知 |

**机制**：两处放宽。
1. 原版 `SIDE→UP` 属于"连接性未变"（`RedstoneSide.isConnected()` 对 SIDE 与 UP 都为 true），不发通知；仿真会发。
2. 原版在 `newState == state`（例如四面都是粉线的十字切换回十字）时直接 PASS，不做任何 `setBlock` 与通知；仿真的 `setBlock` 虽然会因新旧相同而提前返回（`src/simulator.cpp:112`），但随后的通知循环仍然执行。

**用户可见影响**：手动切换粉线形状时，仿真会多发若干次邻居更新；在准连接（QC）敏感的电路里可能提前触发活塞/二极管。

**最小反例建议**：粉线 W 在 (0,1,0) 处于十字形；东侧 (1,1,0) 放实心导体方块，其上 (1,2,0) 放粉线（使该方向的连接为 `up`）；对 W 执行 `interact`。原版不通知 (1,1,0)，仿真通知。断言点：在 (1,1,0) 下方或旁边挂一个准连接活塞，观察是否被激活。

---

### ★R10 `pushable()` 用方块名白名单代替 `getDestroySpeed()==-1`，且 `PUSH_ONLY` 分支多一次方块实体判定（已修复，见 [fixProgress.md](fixProgress.md)）

| | 位置 |
| --- | --- |
| 原版 | `PistonBaseBlock.java:240`（黑曜石四件套）、`:252-253`（`getDestroySpeed(level,pos) == -1.0F` → false）、`:257-264`（switch 中 `PUSH_ONLY` 直接 `return direction == connectionDirection`，**不**再判 `hasBlockEntity`） |
| 仿真 | `src/pistons.cpp:19`（名字白名单里额外写死 `minecraft:bedrock`）、`:25`（`pushReaction == 3 && movement != connection` → false，随后落到 `:27` 的 `return !state.blockEntity`） |

**主代理复核修正**：C++ 没有硬度信息，但“当前不可触发”的原判断错误。`EndPortalFrameBlock` 已作为 `externalStimulus` 允许放置，其注册表 `pushReaction=NORMAL`、无方块实体，且不在 `pushable()` 的硬编码不可推动名单中。原版硬度为 −1，因此会拒绝推动，C++ 则允许。

**动态反例已确认**：`tests/scenarios/endPortalFramePush.json` 在活塞前放末地传送门框，第 1 gt 供电。第 2 gt 原版活塞保持缩回（stateId 2264），C++ 已伸出（stateId 2258）。见 `confirmedCounterexamples.json`，这是当前支持范围内可达的差异，不是未来扩容风险。

釉面陶瓦也因 `_terracotta` 白名单可放置，属于 `PUSH_ONLY`，所以“目前没有可放置实例”同样不正确；但它没有方块实体，因此额外的 `!blockEntity` 判断对它没有影响。需要把已经复现的硬度检查缺失与尚无反例的附加条件分开。后续应导出不可破坏属性并按原版判断顺序处理，而非仅增加一个特例名字。

---

## 2. 有意偏离（需要写进文档，不应视为缺陷）

### R11 更新预算耗尽后的行为
原版 `CollectingNeighborUpdater.addAndRun`（`:68-85`）在超过 `maxChainedNeighborUpdates` 后**记录一条错误日志并丢弃后续更新，继续执行已入栈的部分**。C++ `Simulator::enqueue`（`src/simulator.cpp:74-78`）抛异常、置 `faulted` 并要求撤销或加载快照。这符合 `AGENTS.md`「不得丢事件后继续假装正确」，但意味着在超长更新链（原版会静默降级）的电路上两者行为必然不同，**不能**声称此类电路兼容。

### R12 振动监听器不每刻空转
原版 `SculkSensorBlock.getTicker`（`:156-165`）让每个感测体每游戏刻都跑 `VibrationSystem.Ticker.tick`。C++ 只在有候选或行进中的振动时排 phase 2 事件（`src/vibrations.cpp:95-99`、`:142`）。静态核对结论：`Ticker.tick` 在 `currentVibration == null` 且选择器为空时只做一次 `chosenCandidate` 查询、无任何写入或事件，因此空闲刻无可观测副作用。此偏离是性能优化，前提是"选择器为空"，该前提由 `handleGameEvent`（`VibrationSystem.java:213-215`）在 `currentVibration != null` 时直接返回来保证。

---

## 3. 待实验假设

### ☆R13 `FullNeighborUpdate` 携带的状态快照 vs 仿真重新读世界
`Level.updateNeighbourForOutputSignal`（`Level.java:1010`、`:1015`）与 `DetectorRailBlock.updatePowerToConnected`（`:122`）调用的是**带状态**的 `neighborChanged(state, pos, block, …)`。`CollectingNeighborUpdater.FullNeighborUpdate`（`:114-120`）把该状态**记录下来**，在真正出栈执行时用的是入队时的快照。接收侧 `DiodeBlock.neighborChanged`（`:73-75`）先用当前世界状态做 `is(this)` 守卫，再用**快照**做 `canSurvive` 与 `checkTickOnNeighbor`。

C++ 的 `executeNeighbor`（`src/simulator.cpp:385-395`）总是重新读 `world.get(p)`，`neighborChanged(pos, source)` 的第二参数只是"触发方块"，没有状态快照概念。

触发条件：从容器写入到比较器通知实际执行之间，比较器自身的状态被改写（例如同一批更新中比较器先被换朝向或被移除再放置）。这在常规电路中难以构造，需用原版差分确认是否可达。若可达，用户可见影响是比较器在极端同刻竞争下的排刻结果不同。

### ☆R14 未实现器件在 `Device` 枚举与容量表中的占位耦合
`Device::dispenser` / `crafter` / `furnace` 已存在于枚举（`include/simulator/types.hpp:57`），`inventorySize` 已给出 9/9/3 槽（`src/containers.cpp:21-22`），`classify()` 也把 `DispenserBlock`/`CrafterBlock`/`AbstractFurnaceBlock` 映射过去（`src/blockRegistry.cpp:19-20`）。但 `supportLevel` 判据 `typeInfo.device <= Device::movingPiston`（`:92`）把它们留在 `unimplemented`，`Simulator::place`（`src/simulator.cpp:175`）会拒绝放置，因此当前安全。

风险点：`stimulateDevice`（`src/devices.cpp:212`）对 `Device::dropper` 开放了库存输入，`transferItem`/`containerAnalog` 都会对这些 device 正常工作。一旦有人为了"能放置发射器"而把 supportLevel 改成 implemented，发射器的分发行为表、合成器的 `TRIGGERED`/`CRAFTING`/槽位禁用、熔炉的 `WorldlyContainer` 分面槽位都会静默按普通容器处理。建议：要么删掉占位容量，要么在 `place` 之外再加一道运行期断言。

### ☆R15 `Mth.SIN` 表构造式与 C++ 不同
原版 `Mth.java:35-39`：`sin[i] = (float)Math.sin(i / 10430.378350470453)`；C++ `src/devices.cpp:20-23`：`values[i] = (float)std::sin((double)i * π * 2.0 / 65536.0)`。两者数学上相等（`10430.378350470453 == 65536/(2π)`），但双精度求值路径不同，个别 `i` 上的 float 结果可能差 1 ULP。索引计算本身已正确复刻（`Mth.java:54-56` 的 `(int)((long)(i * 10430.378350470453 + 16384.0) & 65535L)` ↔ `src/devices.cpp:25-26`）。仓库已有 `java26_2Daylight.json` 的 11,536 个数值样本对照（`referenceValidation.md`），既有数值样本通过不等于穷尽 65536 个表项，不能因此排除本假设；仍需逐项比较整个表及临界输入。

---

## 4. 已明确记录的实现边界（不是差异，是未实现）

| 缺口 | 位置 | 影响 |
| --- | --- | --- |
| 物品实体（掉落物） | `HopperBlockEntity.java:218-246`、`:358-361` | 漏斗不吸取地面物品；投掷器抛出物变成外部动作 `itemEjected`（`src/droppers.cpp:26-28`） |
| 实体容器（箱子矿车/漏斗矿车/驴子） | `HopperBlockEntity.java:393-398`，含 `level.getRandom().nextInt(entities.size())` | 漏斗与投掷器完全不与实体容器交互；原版这一次 RNG 抽取在仿真中不存在 |
| 物品展示框比较器输入 | `ComparatorBlock.java:107-114、120-127` | 比较器读不到展示框 |
| 命令方块矿车的探测铁轨读数 | `DetectorRailBlock.java:147-151` | 只实现容器矿车分支（`src/rails.cpp:231-238`） |
| `ShelfBlock`（26.2 中存在，`POWERED` + 比较器输出 + 侧向链） | `ShelfBlock.java:49-133` | `classify()` 返回 `unsupported`，不可放置 |
| `CrafterBlock` / `DispenserBlock` / `AbstractFurnaceBlock` / `BrewingStandBlock` / `ShulkerBoxBlock` | — | 同上，见 R14 |
| `SculkShriekerBlock` | 振动监听器，含 `requiresAdjacentChunksToBeTicking` | 不可放置 |
| `TntBlock` / `CommandBlock` / `StructureBlock` / `JigsawBlock` / `BigDripleafBlock` / `CreakingHeartBlock` | 均有 `neighborChanged` 或比较器输出 | 不可放置 |
| 实验性红石规则 | `ExperimentalRedstoneWireEvaluator`、`Orientation`、`ExperimentalRedstoneUtils` | C++ 完全不建模 `Orientation`；目标固定为非实验版 |
| `Block.UPDATE_SKIP_SHAPE_UPDATE_ON_WIRE`（=128） | `Block.java:98`；`NeighborUpdater.java:46` | 本次未定位到传入该 flag 的实际主线调用；未通过完整字节码调用/复合常量分析证明不可达，因此不把缺失分支判为当前已复现故障 |

---

## 5. 逐组核对结论（35 组）

"结论"列：**一致** = 本次逐句比对未发现语义差异；**差异** = 见上文编号；**仅定位** = 未逐句比对。

| # | 器件 / 机制组 | 原版主要位置 | C++ 主要位置 | 结论 |
| --- | --- | --- | --- | --- |
| 1 | `Level.setBlock` 主干与 flag 语义（1/2/16/32/64/256/512） | Level.java:222-268、LevelChunk.java:296-353、Block.java:88-102 | simulator.cpp:109-154 | 一致，除 R2 |
| 2 | `CollectingNeighborUpdater` 栈与 `addedThisLayer` 反序压栈 | CollectingNeighborUpdater.java:68-112、128-180 | simulator.cpp:74-104 | 一致，除 R11（预算） |
| 3 | 邻居顺序 `{W,E,D,U,N,S}` / 形状顺序 `{W,E,N,S,D,U}` | NeighborUpdater.java:18、BlockBehaviour.java:86-88 | types.hpp:14-15 | 一致 |
| 4 | 信号读取（`getSignal` / `getDirectSignalTo` / `getBestNeighborSignal` / `getControlInputSignal`） | SignalGetter.java:13-105 | simulator.cpp:15-39、356-366 | 一致 |
| 5 | 红石粉强度求解 | DefaultRedstoneWireEvaluator.java:18-43、RedstoneWireEvaluator.java:29-47 | simulator.cpp:287-297 | 一致（含 `max(0, wireSignal-1)` 语义） |
| 6 | 红石粉连接计算与形状更新 | RedStoneWireBlock.java:124-194、236-258 | simulator.cpp:265-286、344-354 | 一致 |
| 7 | 红石粉更新集合的 HashSet 遍历序（7 元素、容量 16） | DefaultRedstoneWireEvaluator.java:27-36 | simulator.cpp:10-13、300-303 | 一致（`Sets.newHashSet()` 确为默认容量 16，7 元素不扩容） |
| 8 | 计划刻队列、优先级、每刻 65536 上限、`hasScheduledTick` vs `willTickThisTick` | ServerLevel.java:389、DiodeBlock.java:92-100 | blockTicks.hpp/cpp | 仅定位（已有 java26_2Scheduler.json 差分记录） |
| 9 | 阶段序：blockTicks → blockEvents → entities → blockEntities | ServerLevel.java:353-452 | blockTicks.hpp:19、simulator.cpp:497-537 | 一致 |
| 10 | 中继器 | DiodeBlock.java:53-186、RepeaterBlock.java:39-90 | simulator.cpp:356-367、408、457-463 | 差异 R3、R5 |
| 11 | 比较器 | ComparatorBlock.java:66-183 | simulator.cpp:369-384、409-414、464 | 差异 R3；物品展示框输入未实现 |
| 12 | 红石火把（60gt 窗口 / 8 次 / 160gt） | RedstoneTorchBlock.java:43-150 | simulator.cpp:407、468-484 | 一致 |
| 13 | 拉杆 / 按钮 | ButtonBlock.java:108-187、LeverBlock | simulator.cpp:578-579、devices.cpp:74-95 | 一致（`ticksToStayPressed` 常量与 Blocks.java:1930/2724-2733/4938 相符） |
| 14 | 压力板 / 重量压力板 | BasePressurePlateBlock.java:75-129、PressurePlateBlock.java:42-49、BlockSetType.java:85-111 | devices.cpp:50-72 | 一致（生物敏感度按方块名判定与 26.2 数据吻合） |
| 15 | 侦测器 | ObserverBlock.java:50-130 | simulator.cpp:221、243、342、466 | 差异 R4 |
| 16 | 红石灯 / 铜灯 | RedstoneLampBlock.java:35-56、CopperBulbBlock.java:33-75 | simulator.cpp:415-416、222、44、467 | 一致 |
| 17 | 活塞信号判定与方块事件编码 | PistonBaseBlock.java:104-147 | pistons.cpp:6-13、81-92 | 一致（`Direction.get3DDataValue()` 与 C++ 枚举序一致） |
| 18 | 活塞结构解析（12 格上限、碰撞重排、黏液/蜂蜜分支） | PistonStructureResolver.java 全类 | pistons.cpp:29-71 | 一致（含 `reorderListAtCollision` 的 `std::rotate` 等价性） |
| 19 | 活塞移动、清理顺序、落地 | PistonBaseBlock.java:149-372、PistonMovingBlockEntity.java:275-340 | pistons.cpp:100-183 | 差异 R1、R2、R8 |
| 20 | 漏斗传输与冷却（推/拉、7 vs 8 刻） | HopperBlockEntity.java:97-348 | hoppers.cpp:74-146 | 一致：逐场景推演（推入方在前/在后、新建漏斗、从非漏斗容器拉取）四种排列的最终就绪刻均与 `tryMoveInItem`（:334-341）+ `tryMoveItems`（:123-127）一致 |
| 21 | 容器比较器读数、双箱合并、上方遮挡 | AbstractContainerMenu.getRedstoneSignalFromContainer、ChestBlock、HopperBlockEntity.java:378-391 | containers.cpp:102-146 | 一致（`Math.min(container.getMaxStackSize(), stack.getMaxStackSize())` 在 26.2 中等于物品上限，与 C++ 取值相同） |
| 22 | 投掷器（选槽 RNG、插入失败仍写回、朝向容器 vs 抛出） | DropperBlock.java:47-79、DispenserBlockEntity.getRandomSlot | droppers.cpp:4-48 | 一致 |
| 23 | 堆肥桶（首次必成、失败仍消耗、输出容器双次清空） | ComposterBlock.java:285-480 | composters.cpp 全文件 | 一致（`addItem` 的短路求值与 C++ `(level==0 && chance>0) \|\| nextDouble()<chance` 等价） |
| 24 | 唱片机（播放进度、20 刻事件、曲终解锁、ticker 装卸） | JukeboxBlockEntity.java:36-130、JukeboxSongPlayer.java:45-88、JukeboxSong.java:46-52 | jukeboxes.cpp 全文件 | 一致（`hasFinished` = `elapsed >= lengthInTicks()+20`） |
| 25 | 雕纹书架 / 陶罐 | ChiseledBookShelfBlock.java:158-197、DecoratedPotBlock | containers.cpp:26-54、hoppers.cpp:65 | 一致 |
| 26 | 门 / 活板门 / 栅栏门（含游戏事件先后） | DoorBlock.java:224-238、TrapDoorBlock.java:108-143、FenceGateBlock.java:142-202 | simulator.cpp:417-432、devices.cpp:119-124 | 一致（活板门的 `gameEvent` 藏在 `playSound` 内，C++ 的先发后写与之相符；栅栏门先写后发亦相符） |
| 27 | 铁轨（普通/充能/激活/探测） | BaseRailBlock.java:63-132、RailBlock.java:29-33、DetectorRailBlock.java:55-159、RailState.java | rails.cpp 全文件 | 差异 R1；`RailState.place` 细节仅定位 |
| 28 | 绊线与绊线钩 | TripWireBlock / TripWireHookBlock | tripwire.cpp 全文件 | 仅定位（已有 java26_2Tripwire.json 差分记录） |
| 29 | 阳光探测器 | DaylightDetectorBlock.java:54-116、Mth.java:35-56 | devices.cpp:18-27、97-113 | 差异 R6；☆R15 |
| 30 | 标靶 | TargetBlock.java:42-113 | devices.cpp:167-189、simulator.cpp:219、487 | 一致（轴选择、`Mth.frac`、`max(1, ceil(...))` 全部对应） |
| 31 | 避雷针 | LightningRodBlock.java:74-125 | devices.cpp:227-232、simulator.cpp:245、489 | 差异 R2 |
| 32 | 讲台 | LecternBlock.java:155-208、LecternBlockEntity.java:159-175 | devices.cpp:242-265、simulator.cpp:51-56、246、490 | 一致（`setChanged` 先于 `signalPageChange` 的顺序与 C++ 的 `runtimeChanged` 先于 `setBlock` 相符） |
| 33 | 幽匿感测体 / 校频幽匿感测体 | SculkSensorBlock.java:83-243、CalibratedSculkSensorBlockEntity | vibrations.cpp:149-176 | 一致 |
| 34 | 振动传播、选择器、遮挡射线 | VibrationSystem.java:118-361、VibrationSelector.java、EuclideanGameEventListenerRegistry.java:76-124 | vibrations.cpp:17-147 | 一致（含 `1.0E-5F` 微移、`-1e-7` 收缩、`BlockPos.distSqr` 半径判定、`floor(distance)` 行进刻） |
| 35 | 音符盒 / 钟 | NoteBlock / BellBlock / BellBlockEntity | notes.cpp、bells.cpp | 仅定位（已有 java26_2Notes.json、java26_2Bells.json 差分记录） |

---

## 6. 建议的复跑优先级（供主代理排期）

1. **R7 / R8（楼梯形状 → 支撑掩码）** —— 影响面最广，且能用纯静态放置场景复现，不依赖时序。
2. **R1（通知源方块）** —— 铁轨反例最容易断言；同时决定是否要给 `updateNeighbors` 增加显式 source 参数的全面改造。
3. **R2（活塞移动跳过 onRemove）** —— 侦测器/避雷针场景，需要精确的刻编排。
4. **R3（二极管断支撑）** —— 需要逐事件比较更新序列，建议用 `captureRedstone.py` 增设场景。
5. **R6（日光传感器游戏事件）** —— 已由主代理复现第 13 gt 差异；修复须保留事件时机和源上下文，再把反例转为常规回归。
6. **R5 / R9** —— 只影响状态 ID 与多余更新，可与上面场景合并采集。
7. ☆R13 / ☆R15 —— 先确认可达性再决定是否投入。

## 7. 本报告不主张的内容

- 不主张任何"完整兼容"。上表中标"一致"的组只代表**本次静态阅读未发现差异**，不代表已通过原版差分。
- 标"仅定位"的组（8、27 的 `RailState`、28、35）本次没有逐句比对，其既有可信度全部来自 `referenceValidation.md` 中已记录的 fixture。
- 第 4 节列出的缺口是**已知未实现**，不应被计入覆盖率。

## 主代理复跑补充

R6：日光传感器切换，13 gt 原版感测体 active / 10，C++ inactive / 0。R7：相邻两段楼梯在 0 gt 原版连接为 inner_right，C++ 仍为 straight。R10：末地传送门框阻挡活塞，2 gt 原版缩回，C++ 伸出。以上均为原版真实捕获，不是人为篡改预期的负对照。其余静态差异仍需分别复现，不能从这三例外推。
