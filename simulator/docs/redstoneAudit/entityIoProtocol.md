# 器件层实体输入/输出协议

2026-09-09 收尾补充：`containerEntities` 覆盖原版 `Container` 选择器的**全部**实体类型，
即运输矿车、漏斗矿车与运输船/运输竹筏的被动库存。
2026-09-10 补上另一半：**漏斗矿车自己的主动吸取**已实现（见下面单独一节）。
运输船/竹筏那一支与漏斗矿车主动吸取这一支**都尚无原版差分**——
只有内核单元回归，各需要一次新的捕获才算实测验证。
新的严格 vanilla-only 实体容器复跑与检查器审计见 [layeredAudit.md](layeredAudit.md)。

对应 issue #12 的第一条验收：**先定义器件层实体输入/输出协议及时间、坐标、库存和随机源语义**。

本项目不做全世界实体仿真。掉落物的运动、碰撞、合并、拾取延迟和消失都**不在**模型内；
这里定义的是「用户/外部模型声明实体处于什么状态，内核执行原版的方块侧逻辑」这一层的接口。
既有的压力板、绊线、按钮、探测铁轨、物品展示框输入都属于同一层，本文把规则统一写清楚。

## 通用语义

**时间**。实体输入通过 `stimulate` 写入，与命令同刻生效，不占用游戏刻。
原版里由实体自己驱动的回调（`Block.entityInside`）在**实体阶段**执行，
它早于方块实体阶段、晚于方块计划刻，内核用同一顺序的阶段 3 事件表示。
在阶段 3 之后（含处理命令的阶段 4）写入的实体输入，最早在**下一刻**的实体阶段生效，
这与原版「刚生成的实体本刻已经过了实体阶段」一致。

**坐标**。位置是相对于所属方块角点的实数偏移，默认在方块中心正上方。
内核按原版的包围盒规则求交：掉落物是 0.25 × 0.25 × 0.25，水平居中于位置、竖直从 y 向上；
`AABB.intersects` 用严格不等号，**相切不算相交**。
坐标只是输入，**不作为观测量比较**：原版每刻都会用 `Entity.move` 从包围盒反算位置，
产生 1e-15 量级的漂移，对它做等值比较没有意义。

**顺序**。列表顺序**就是**迭代顺序。原版的顺序来自实体分区存储，
器件层协议不建模那一层，因此顺序是显式输入而不是推导结果。
这意味着多实体场景的顺序只在被实测覆盖的配置上得到验证。

**库存**。被吸入的物品按原版 `HopperBlockEntity.addItem` 的槽位顺序进入容器，
每成功写入一格调用一次 `Container.setChanged()`（因此会通知比较器）。
物品只支持默认组件；带组件的物品仍然明确拒绝。

**随机源**。掉落物路径**不消耗任何随机数**。实体容器路径**会**消耗：
`getEntityContainer` 用 `level.getRandom().nextInt(list.size())` 在候选里选一个，
**只有一个候选时也照抽**。抽取的时机与次数是被比较的观测量，见下面的实体容器一节。

## 已实现：漏斗吸取掉落物

输入写在漏斗上：

```json
{"groundItems": [{"item": "minecraft:stone", "count": 5, "x": 0.5, "y": 1.0, "z": 0.5}]}
```

一次刺激**整体替换**该漏斗上声明的掉落物集合，空数组表示全部移除；
不能与其他刺激字段混用；最多 32 条；`count` 必须在 1 到该物品堆叠上限之间；
坐标限制在漏斗附近 `[-2, 4]`。

原版有**两条**把掉落物送进漏斗的路径，两条都已实现：

| 路径 | 触发点 | 判据 | 上方完整方块 |
|---|---|---|---|
| `HopperBlockEntity.suckInItems` | 方块实体阶段，漏斗自己的 tick | 与 `SUCK_AABB` 相交 | **会**阻挡（除非在 `DOES_NOT_BLOCK_HOPPERS` 里） |
| `HopperBlock.entityInside` | 实体阶段，每个实体每刻各一次 | 与漏斗**自己那一格**重叠，且与 `SUCK_AABB` 相交 | 不阻挡 |

`SUCK_AABB = Block.column(16, 11, 32)` 平移到方块上之后是
`x ∈ [px, px+1]`、`y ∈ [py+0.6875, py+2]`、`z ∈ [pz, pz+1]`。

两条路径都走 `tryMoveItems`：先在库存非空时向朝向方向推出一件，再在库存未满时执行吸取动作，
任一成功就置 8 gt 冷却并 `setChanged`。折算成内核的 `readyAt` 时要看本刻的 `pushItemsTick`
是否还会再递减一次冷却：方块实体阶段设的是 `+8`，实体阶段设的是 `+7`。

`addItem(Container, ItemEntity)` **只有整叠放进去**才算变化。部分放入会把剩余量写回实体、
继续看下一个掉落物，本次不算变化——但 `tryMoveInItem` 里「漏斗从空变非空」那一条仍然会设冷却。
这个原版怪癖已按原样实现并有回归覆盖。

上方是容器时，`suckInItems` 走容器分支，**根本不会**看掉落物。

### 一个必须说清楚的可达性事实

`suckInItems` 的「上方完整方块阻挡」在**静止**掉落物上观察不到：
吸取体积的下端 11/16 正好是漏斗自己的空腔起点，上方是完整方块时，
体积内唯一的自由空间就在漏斗那一格里，而那里恰好由不受阻挡的 `entityInside` 路径接管。
阻挡判据仍按原版实现，但本轮的固定场景**没有**构造出让它改变结果的历史。

## 已实现：漏斗与容器实体（运输/漏斗矿车、运输船/运输竹筏）

输入写在**实体所在的那一格**上，可以是空气格：

```json
{"containerEntities": [{"type": "chest_minecart", "inventory": [{"slot": 0, "item": "minecraft:stone", "count": 6}]}]}
```

一次刺激**整体替换**该格的容器实体列表，空数组表示全部移除（空气格上连器件数据行也一并删掉）；
不能与其他刺激字段混用；最多 16 个；只接受 `type` 与 `inventory` 两个字段。

`EntitySelector.CONTAINER_ENTITY_SELECTOR` 是 `entity instanceof Container && entity.isAlive()`
（`net/minecraft/world/entity/EntitySelector.java:13`）。26.2 里满足它的实体只有两条继承线：

| 继承线 | 类 | `getContainerSize()` | 注册 ID |
|---|---|---|---|
| `AbstractMinecartContainer implements ContainerEntity` | `MinecartChest` | 27 | `chest_minecart` |
| 同上 | `MinecartHopper` | 5 | `hopper_minecart` |
| `AbstractChestBoat implements ContainerEntity` | `ChestBoat` | 27 | `oak_/spruce_/birch_/jungle_/acacia_/dark_oak_/mangrove_/cherry_/pale_oak_chest_boat` |
| 同上 | `ChestRaft` | 27 | `bamboo_chest_raft` |

共 12 个注册 ID。`ContainerEntity extends Container`
（`net/minecraft/world/entity/vehicle/ContainerEntity.java:32`）；
`AbstractChestBoat.getContainerSize()` 在 `vehicle/boat/AbstractChestBoat.java:114` 返回 27，
`ChestBoat` 与 `ChestRaft` 都**不**覆写它（两个子类只覆写 `rideHeight`）。
木头种类各自是**独立的 `EntityType`**（`EntityTypeIds.java` 的 `*_CHEST_BOAT` / `BAMBOO_CHEST_RAFT`，
注册见 `EntityTypes.java` 的 `chestBoatFactory` / `chestRaftFactory`），共用同一个 Java 类。

不是候选的常见误判：`Player`（`Avatar implements ContainerUser`，`player/Player.java:126`）、
`CopperGolem`（`ContainerUser`，`animal/golem/CopperGolem.java:58`）、
`AbstractHorse` 及其 `AbstractChestedHorse`/`Donkey`/`Llama` 子类
（只实现 `PlayerRideableJumping, HasCustomInventoryScreen, OwnableEntity`，
`animal/equine/AbstractHorse.java:80`）——它们都**不**实现 `Container`。
不带箱子的 `oak_boat` / `bamboo_raft` 同理。其余类型一律拒绝。

位置与运动**不建模**：矿车与船一律视作停在格中心，
`getEntityContainer` 那个 1×1×1 判据在这个约定下恒为真。
船的浮力、水面行为、乘骑、划桨与撞碎同样**完全不建模**——
「支持运输船」指的只是「它是 `getContainerAt` 的一个候选容器」这一件事。

**运输船/竹筏这一支尚无原版差分。** 既有的 `java26_2EntityContainers` 捕获里只出现矿车；
船的分支目前只有内核单元回归（含与矿车逐刻对比的一致性用例），
需要一次新的捕获才算实测验证。

原版 `HopperBlockEntity.getContainerAt` 的查询顺序被逐条复现：

1. `getBlockContainer` 先查。**命中就结束**，此时**不消耗随机数**——
   同一格里即使停着装满的矿车也完全看不见。
2. 没有方块容器才查 `getEntityContainer`，它收集该格所有 `Container && isAlive` 的实体，
   然后 `nextInt(size)` 选一个。**候选非空就一定抽一次**，与本次是否真的搬动无关。
3. 选中之后就只跟这一个打交道，搬不动也**不会**退回去吸掉落物
   （原版 `container != null` 分支直接 `return false`）。因此一辆**空**矿车（或一条空船）
   照样能把底下够得着的掉落物完全挡住。

矿车与船都不是方块实体：`AbstractMinecartContainer.setChanged`（`:66`）与
`AbstractChestBoat.setChanged`（`:144`）都是**空实现**，所以写入它们的库存
**不**触发比较器更新。两者也都不是 `WorldlyContainer`，
没有 `canPlaceItem`/`canTakeItem` 覆写，分面限制对它们不适用。

**空转也要抽**。原版 `tryMoveItems` 搬不动东西时**不设冷却**，下一刻整套重跑一遍；
只要那一格有容器实体，`getEntityContainer` 就每刻消耗一次 `nextInt`，一件都没搬动也照抽。
内核原本在空转一刻后就让漏斗休眠（对方块容器是等价的，因为那条路径不消耗随机），
容器实体把这个惰性排程变成了可观测量，现已按原版逐刻重试。
搬成功之后的 8 gt 冷却期间原版走不到 `getEntityContainer`，因此冷却里**不**抽。

## 已实现：漏斗矿车主动吸取（**尚无原版差分**）

上面那一节讲的是**方块漏斗**去看矿车；反过来，`MinecartHopper` 自己每刻也会吸一次。
这一条现在也实现了，但必须先说清楚证据等级：

> **这一支尚无原版差分。** 本轮参考捕获资源被独占，本节所有规则都是从 26.2 反编译源码
> **推出的期望**，只有内核单元回归覆盖。唯一的实测旁证是上一轮的一次原版探针，
> 只覆盖「格中心 + 上面第二格是方块漏斗」这一个几何（数据见本节末尾）。

### 源码依据

| 结论 | 位置 |
|---|---|
| `tick()`：清 `consumedItemThisFrame` → `super.tick()` → `tryConsumeItems()` | `minecart/MinecartHopper.java:84-88` |
| `tryConsumeItems()` **没有任何冷却字段**，判据只有 服务端 / `isAlive` / `isEnabled` / 本帧未搬过 | 同上 `:97-102` |
| 移动途中 `makeStepAlongTrack` 还会额外吸一次 | 同上 `:91-95` |
| `suckInItems()` 先走方块漏斗那套，失败才扫包围盒 `inflate(0.25, 0, 0.25)` 里的掉落物 | 同上 `:104-117` |
| `getLevelY() = getY() + 0.5`；`isGridAligned()` 为 **false** | 同上 `:63-81` |
| 查询格 = `BlockPos.containing(levelX, levelY + 1.0, levelZ)`，取用方向恒为 `DOWN`；`isGridAligned` 为假时**跳过**「上方完整方块阻挡」 | `HopperBlockEntity.java:218-246` |
| `getSourceContainer → getContainerAt`：`getBlockContainer` 优先，为空才 `getEntityContainer` | 同上 `:354-356 / :367-376 / :378-391` |
| `getEntityContainer` 候选非空就消耗一次 `level.getRandom().nextInt(size)` | 同上 `:393-398` |
| 置 8 gt 冷却那一条要求收件方 `instanceof HopperBlockEntity`；矿车不是，所以**矿车永远不进冷却** | 同上 `:314-348`（第 334 行） |
| 成功取出时对**源容器**调用 `setChanged()`（失败时的 remove/restore 同样可观测） | 同上 `:248-265`（第 255 行） |
| 实体阶段早于方块实体阶段 | `ServerLevel.java:426`（`entityTickList.forEach`）与 `:450`（`tickBlockEntities()`） |
| 堆肥桶 `OutputContainer.canTakeItemThroughFace` 只看方向 DOWN，**与取用者的位置无关** | `ComposterBlock.java:461-473` |

### 实现出来的语义

* **查询格是「上面第二格」。** 本协议约定矿车停在格中心（y = 格底 + 0.5），
  于是 `levelY + 1.0 = 格底 + 2.0`。
  **边界：停在铁轨高度（y = 格底 + 0.0625）的矿车掏的是正上方那一格**
  ——经典的「漏斗矿车压在箱子底下」——**而那个位置目前无法声明**：
  本协议里位置的粒度是「哪一格」，不是实数 y。要支持它必须先给容器实体加上竖直偏移输入。
* **每刻一次，没有冷却。** 上面第二格是装了 3 件东西的方块漏斗时，矿车连着三刻各掏一件。
  同一个源换成方块漏斗在下面接，8 gt 冷却下三刻只搬得动一件。
* **同刻里矿车先于方块漏斗。** 源漏斗只剩最后一件时，矿车抢得到，源漏斗自己那一次推出落空。
* **随机源。** 方块容器分支**不抽**；上面第二格是容器实体时每刻抽一次 `nextInt`，
  一件都搬不动也照抽。同一格里的每辆漏斗矿车**各自**吸一次，因此两辆就是每刻两次。
* **矿车只吸不推。** `MinecartHopper` 没有 `ejectItems`，装满了就停在那儿
  （但仍然每刻调用一次 `suckInItems`，所以该抽的随机数一次不少）。
* **写入矿车库存不通知比较器**（`AbstractMinecartContainer.setChanged` 是空实现）；
  从方块容器里取走那一件**会**通知源容器。
* **调度。** 事件跑在阶段 3（实体接触），挂在矿车所在的那一格上，
  事件里记的方块类型固定是 0——矿车不是方块，那一格的方块可以随便换。
  没有可观测效果时矿车休眠，靠「上面第二格的方块或器件数据发生变化」唤醒；
  电路工程（不带运行队列）加载后会显式让每辆矿车重新起跑。

### 这一支明确**没有**建模的部分

1. **矿车吸掉落物**。`suckInItems` 的 `else` 分支与 `MinecartHopper.suckInItems` 自己那段
   包围盒扫描都没实现：`groundItems` 是声明在**漏斗方块**上的，矿车没有对应的声明入口。
2. **停在铁轨高度**（见上）。
3. **`isEnabled()` / 激活铁轨禁用**。`activateMinecart` 唯一的调用点在
   `NewMinecartBehavior.java:250` / `OldMinecartBehavior.java:66` 的 `moveAlongTrack` 里，
   属于轨道与运动模型，本协议不建模，因此内核里的漏斗矿车**恒为启用**。
   停在通电激活铁轨上的漏斗矿车在原版里是停用的，这里会分叉。
4. **跨格的矿车先后顺序**。同一格内按声明顺序（协议既有约定）；
   不同格之间按内核的排程顺序，不是原版的实体列表顺序。竖直串联矿车的场景不受保证。
5. `stimulate` 的 `carts` 输入（探测铁轨接触）是**另一套**声明，与吸取无关，不会驱动它。

### 上一轮的原版探针实测（唯一的实测旁证）

12 刻、矿车停在格中心：方块漏斗放在矿车**上面第二格**时，第 3/4/5 刻各被搬走一件、3→0，
**没有冷却**；放在矿车**正上方**时 12 刻内一件没少；全程世界随机源一步没动
（矿车这一侧命中的是 `getBlockContainer`，走不到 `getEntityContainer`）。
这组数据只覆盖这一个几何，其余全部靠 `coreTests.cpp` 的
`hopper minecarts suck from the second block above them` 做内核内部回归。

## 明确未实现（issue #12 仍然打开的部分）

1. **掉落物的运动与再吸收闭环**。投掷器抛出仍然只产生 `itemEjected` 外部动作，
   见 [投掷器说明](../droppers.md)。把抛出物变成漏斗输入需要用户或外部模型显式声明，
   内核不模拟弹道、摩擦、合并与消失。
2. **实体所在方块的挤出**。原版对卡在碰撞形状里的掉落物调用 `moveTowardsClosestSpace`
   把它推到最近的空位。协议不建模，因此**声明的位置应当处在自由空间里**；
   放在方块碰撞形状内部的输入会与原版分叉，回归场景刻意避开了这种位置。
3. ~~**漏斗矿车自己的吸取**~~ —— **已实现**，见上面的「已实现：漏斗矿车主动吸取」一节。
   仍然打开的只剩那一节末尾列的四条边界：矿车吸掉落物、停在铁轨高度（掏正上方那一格，
   目前无法声明）、激活铁轨的 `isEnabled()` 禁用、跨格的矿车先后顺序。
   **这一支尚无原版差分**，只有内核单元回归。既有的 51 条 fixture 里两辆漏斗矿车
   （`java26_2EntityContainers` 的 `[12,1,4]` 与 `[45,1,4]`）上面第二格都是空气，
   实现前后逐条比对全部不变。
4. **矿车/船的运动与轨道、水面交互**。停在格中心是**输入约定**，不是推导结果；
   推动、加速、脱轨、跨格移动、浮力、划桨、乘骑、撞碎都不在模型内。
   多个容器实体的**顺序**同样是显式输入
   （原版顺序来自实体分区的插入顺序），只在被实测覆盖的配置上得到验证。
6. **运输船/运输竹筏尚无原版差分**。类型集合、槽位数与拒绝路径按 26.2 反编译源码实现，
   漏斗侧走的是与矿车**完全同一条**代码路径，并有「混放时逐刻与两辆矿车对照」的单元回归；
   但**没有**任何一帧原版观测覆盖过船。需要一个新的 `captureEntityContainers` 变体
   （至少覆盖：单船候选的 `nextInt(1)`、船与矿车混放的选择序列、装满 27 槽的船挡住推入时的逐刻空转）。
5. **包围盒上探 0.2 格——已实测到分叉**。矿车在格中心时包围盒是
   y ∈ `[cellY+0.5, cellY+1.2]`，会探进上面一格，于是**上面那一格**的
   `getContainerAt` 查询也能命中它。探针实测：原版在矿车正上方那一格报告有一辆
   `hopper_minecart`，内核报告为空——内核的「一格一组声明」模型里没有跨格的概念。
   本轮提交的 fixture 逐个核对过 14 个观测格与 7 个漏斗的 source/eject 格，
   **没有任何查询落在矿车正上方那一格**，因此不受影响、也确实 match；
   但这是器件层协议的真实边界，竖直堆叠矿车的场景必须先解决它。

完整矿车计算机与依赖生物 AI 的机器仍在既定排除范围内，本文不改变该范围。

## 验证

`captureHopperPickup.py` → `java26_2HopperPickup`：30 刻，八组场景，
观测方块状态、比较器读数、库存以及**原版 `getItemsAtAndAbove` 返回的剩余掉落物**。
覆盖吸取体积的上端与水平边界、漏斗空腔内的实体、上方完整方块 / `bee_nest` / 台阶、
8 gt 冷却下的逐个吸取、部分吸入的剩余量、上方是容器时的优先级、
受电禁用与断电恢复，以及中途整体替换声明集合。

`captureEntityContainers.py` → `java26_2EntityContainers`：56 刻、14 个观测格、固定种子，
除方块状态与库存外还逐帧比较**该格的容器实体列表（含顺序）**与**世界随机源的原始状态**。
五组场景：从运输矿车拉取（8 gt 冷却下逐件、27 槽平铺顺序）、推入漏斗矿车、
同格箱子遮蔽矿车（全程零抽取）、空矿车遮蔽掉落物（撤走矿车后同一个掉落物立刻被吸走，
证明它一直可达）、两辆矿车时 `nextInt(2)` 的选择序列，
以及**稳态空转**：拉取侧上方一辆空运输矿车、推出侧对着一辆五槽全满的漏斗矿车。
各场景的抽取刻互不重叠，因此结论不依赖刻内的方块实体顺序。
**这个捕获里只有矿车，一条船都没有。**

**漏斗矿车主动吸取目前也只有 `coreTests.cpp` 的单元回归**
（用例 `hopper minecarts suck from the second block above them`）：
从上面第二格的方块漏斗逐刻各掏一件（并与「方块漏斗在下面接、8 gt 冷却下三刻只搬一件」对照）、
从上面第二格的箱子里吸空、正上方那一格装满的箱子一件都掏不走、
空气/实心方块/非容器方块下什么也不做且不抽随机数、
上面第二格是容器实体时每刻抽一次（两辆矿车就每刻两次、同格的运输矿车不参与）、
同刻里抢在方块漏斗前面拿走最后一件、休眠后被「器件数据变化」与「方块放置」分别唤醒
（后者用装满的堆肥桶，顺带覆盖 `OutputContainer` 隔一格也能取的那条）、
运行快照往返一致、电路工程重新加载后自己起跑。
反向验证做过 12 处定点破坏（查询格改成正上方、给矿车加 8 gt 冷却、实体容器不抽随机数、
吸取推迟一刻、两条唤醒路径各删一条、电路加载不起跑、方块容器不再优先、
只让第一个实体吸、声明时不唤醒、堆肥桶忽略实体目标槽位、刻内阶段顺序调换），
逐个确认用例真的报错后恢复。这些都是**内核内部一致性**，不是原版实测。

运输船/运输竹筏目前**只有 `coreTests.cpp` 的单元回归**
（用例 `chest boats and rafts are container entities too`）：
12 个注册 ID 的槽位数边界（最后一格接受、再往后一格拒绝）、非容器实体（普通船/竹筏、驴、骡、
羊驼、玩家、铜傀儡、普通矿车、类名而非注册 ID）的拒绝、快照往返与破坏后的拒绝、
从运输船拉取、向运输竹筏推入、装满 27 槽的船挡住推入时逐刻仍抽，
以及「船+矿车混放 40 刻的随机源与库存逐刻等于两辆矿车」的一致性对照。
这些都是**内核内部一致性**，不是原版实测。

全程共 41 次抽取，逐帧的 48 位状态可以反推出每刻的抽取次数，
「空转也要抽」与「冷却期间不抽」都是**直接读出来的**，不是推断：

| 刻 | 每刻抽取次数 | 含义 |
|---|---|---|
| 1、3、5、6、9、13、14、17、21、22、29、30、37、38 | 各 1 | 前四组场景，每次都是恰好 1 步 LCG |
| 40 | 2 | 空转组两侧同刻：拉取侧成功搬走一件，推出侧抽了但搬不动 |
| 41–47 | 各 1 | 拉取侧进入 8 gt 冷却，`isOnCooldown` 直接返回、**够不到** `getEntityContainer`；只有推出侧在抽 |
| 48–56 | 各 2 | 矿车已空 / 目标已满，两侧都稳态空转，**每刻各抽一次且一件都不动** |

捕获侧有一个坑值得记下来：撤走矿车**不能**用 `discard()`。
`AbstractMinecartContainer.remove` 在 `reason.shouldDestroy()` 时调 `Containers.dropContents`，
而 `dropItemStack` 是**先抽三个 `nextDouble` 再判断槽位是否为空**，
撤走一辆空的运输矿车就要消耗 27×6=162 次世界随机。捕获器改用 `CHANGED_DIMENSION`。

边界值刻意远离精确相切：原版每刻用 `Entity.move` 反算位置会漂移约 1e-15，
因此**不断言恰好相切**的行为。掉落物一律零速度、关闭重力，
与压力板/绊线的接触输入是同一个约定。每组使用不同物品，避免原版
`mergeWithNeighbours` 把两个声明合并成一个。
