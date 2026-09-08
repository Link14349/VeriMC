# Minecraft 26.2 参考与验证

参考版本固定为 Java Edition 26.2 正式版，非实验性红石。官方版本清单、服务端下载地址和 SHA-1 记录于 `data/referenceVersion.json`；原版 DataVersion 为 4903。

2026-09-08 的源码区域索引、逐项核对、新鲜原版对照和环境限制见 [红石审计报告](redstoneAudit/auditReport.md)。新增 `auditReference.py --output <新目录> --checker <checkReference>` 可在不覆盖既有 fixture 的情况下复跑。参考捕获器现显式关闭自然随机刻，并记录实际功能开关；官方 GameTest 的 `trade_rebalance` 开关仍开启，不能称为严格 vanilla-only 专用服务器验证。

## 可重现流程

1. 安装 Java 25 或更高版本。
2. `python3 tools/reference/fetchReference.py`：下载并校验服务端，提取游戏 JAR，缓存留在 `.cache/reference/`。
3. 在缓存目录运行 `java -DbundlerMainClass=net.minecraft.data.Main -jar server.jar --reports --output reports`，同时提取运行依赖。
4. `python3 tools/reference/runReferenceTool.py`：通过原版 API 导出状态 ID、属性、导电、支撑、推动反应和方向输出表。
5. `python3 tools/reference/captureRedstone.py`：用本项目原创输入场景运行原版 GameTest，生成 `tests/fixtures/java26_2Redstone.json`。测试世界仅位于忽略的 `.cache/reference/captureWorld/`，重跑时替换该缓存世界。
6. 构建后执行 `ctest --preset release`，独立 C++ 内核按相同绝对坐标、放置顺序和游戏刻逐帧比较状态 ID 与比较器内部输出。

扩展数据与场景：生成报告后运行 `python3 tools/reference/exportItems.py` 导出默认物品堆叠上限；`captureDevices.py`、`captureContainers.py` 和 `captureHoppers.py` 分别生成环境器件、库存与传输场景。`runReferenceTool.py ExportDaylight <绝对输出路径>` 生成阳光探测器数值对照。所有 GameTest 生成脚本共享缓存世界，请顺序运行。

正常构建、启动、仿真和测试不依赖 Java 或 Minecraft 安装；注册表和场景观测结果随项目提供。JAR、反编译参考源码、游戏资源和缓存世界不进入 Git。

## 已核查的规则来源

在校验后的官方服务端中核查 `Level`、`LevelChunk`、`CollectingNeighborUpdater`、`DefaultRedstoneWireEvaluator`、`RedStoneWireBlock`、`DiodeBlock`、`RepeaterBlock`、`ComparatorBlock`、`ObserverBlock`、`RedstoneTorchBlock`、`LeverBlock`、`ButtonBlock`、`CopperBulbBlock` 和 `RedstoneLampBlock` 的行为。C++ 为独立实现；原版源码不随项目分发。

- 邻居通知顺序：西、东、下、上、北、南；形状更新顺序：西、东、北、南、下、上。
- 红石粉无虚构游戏刻延迟，保留原版局部 HashSet 的位置相关遍历次序。
- 中继器的朝向属性指向输入，脉冲保持、侧向锁定和计划优先级分别建模。
- 侦测器响应检测面形状更新；比较器保留内部输出；铜灯在上升沿翻转。

## 验证边界

当前差分覆盖粉线衰减、中继器短脉冲、锁定/解锁、侦测器、铜灯、火把/灯状态和比较器读出/减法，以及普通活塞推动、黏性回拉、短脉冲吐块、准连接需通知才触发、黏液分支、蜂蜜隔离和 13 方块阻塞。共 25 刻、47 个观测点。另验证活塞运动中保存/恢复后继续运行。没有证据的复杂场景不能因基本测试通过而自动标为兼容。

当前队列区分计划刻、活塞方块事件与运动实体更新；方块实体阶段使用固定注册序号。方块计划刻按区块收集，每刻最多 65,536 项，分别保存未来计划与本刻待执行批次。跨区块积压、优先级、回调排期和集合查询已有原版专项对照，详见 [调度说明](tickScheduling.md)。立即更新预算耗尽会使本次运行报错并停止，禁止继续执行该部分更新过的状态；可撤销或加载有效快照。

扩展差分覆盖另有 29 刻 / 32 点环境器件和 17 刻 / 14 点容器，以及 65 刻 / 25 点漏斗槽位传输；详细机制与边界见 [环境输入](environmentInputs.md) 和 [库存模型](inventoryModel.md)。阳光公式的 11,536 个数值样本不等于世界光照仿真测试。

`captureTorches.py` 另外生成 195 刻 / 5 点火把烧毁与重放置对照。世界级熄灭历史保留原版 60 gt 窗口、8 次阈值和 160 gt 恢复计划；移除方块不删除该历史，按坐标计数但按世界时间统一过期。

`captureRails.py` 生成 45 刻 / 93 点轨道场景，覆盖连接、坡道、岔路、供电路径、支撑及真实矿车接触/库存；机制边界详见 [rails.md](rails.md)。

`runReferenceTool.py ExportScheduler <绝对输出路径>` 直接运行原版调度器，生成 206 次初始排期、21 刻 / 181 次回调的对照；`captureTickBatches.py` 增加第七组 GameTest，覆盖 25 刻 / 8 点同刻侦测器事件。

`captureTripwire.py` 增加第八组 GameTest，覆盖 70 刻 / 121 点连接、交叉、长度边界、实体接触、断线、剪刀及支撑破坏。场景通过 `discardDrops` 清除拆钩掉落物，避免随机轨迹混入受控接触输入，详见 [绊线说明](tripwire.md)。

`captureButtons.py` 增加第九组 GameTest，覆盖 101 刻 / 16 点按钮接触、安装朝向与复查。真实箭的碰撞盒分别与弹起/按下检测形状相交；原生专项另验证同刻释放—重按的边沿及中途快照。具体环境边界见 [环境输入](environmentInputs.md)。

`captureDroppers.py` 增加第十组 GameTest，覆盖 31 刻 / 19 点。`runReferenceTool.py ExportDropperSlots <绝对输出路径>` 生成 600 次选槽。`ExportDropperMotion` 接受四个绝对路径参数：输出 JSON、缓存 captureWorld、缓存 fixturePacks、输出报告 XML；先运行任一 capture 脚本生成公共空结构。该工具在一次 GameTest 回调内分别重置原版随机源并立即读取实体，36 组位置/速度位模式对照通过，详细隔离条件见 [投掷器说明](droppers.md)。

`captureTargets.py` 增加第十一组 GameTest：53 刻 / 26 点，六面命中、箭/其他投射物、重复命中、红石粉输出与带电标靶重放置。`runReferenceTool.py ExportTarget <绝对输出路径>` 直接调用原版强度计算，4,272 次样本覆盖各信号阈值相邻浮点数及正负/大坐标，详见 [标靶说明](targets.md)。

`captureCopperChests.py` 增加第十二组 GameTest：25 刻 / 132 点，64 种铜箱配对、原版玩家放置、变体同步、库存保留及传输。`playerPlace` 在参考端调用方块原版放置方法，在独立内核调用编辑器放置方法；普通 `stateId` 命令仍是原始状态编辑，二者不混用。详见 [铜箱说明](copperChests.md)。

`captureBookshelves.py` 增加第十三组 GameTest：57 刻 / 11 点，书籍标签、操作次序、最后槽位读数、漏斗预检查及投掷器过滤，详见 [书架说明](bookshelves.md)。`exportItems.py` 同时从固定 JAR 导出书架物品标签。

`capturePots.py` 增加第十四组 GameTest：73 刻 / 26 点，单槽物品上限、六向插入、漏斗失败通知与唤醒、清空和移除，详见 [陶罐说明](decoratedPots.md)。

`exportVibrations.py` 从固定 JAR 的注册表和标签导出 61 种游戏事件、频率、潜行过滤与羊毛/地毯/共振材质。`captureVibrations.py` 和 `captureDeviceVibrations.py` 增加第十五、十六组 GameTest，分别为 161 刻 / 13 点和 155 刻 / 31 点。前者对照显式事件，后者由原版器件自行产生事件；详细机制和未覆盖来源见 [振动说明](vibrations.md)。

`runReferenceTool.py ExportNotes <绝对输出路径>` 导出完整乐器映射、27 种乐器与 25 个音高。`captureNotes.py` 增加第十七组 GameTest，100 刻 / 34 点；`ExportNoteRandom` 使用与 `ExportDropperMotion` 相同的四路径参数格式，运行 112 次隔离声音事件。详见[音符盒说明](notes.md)。

`captureBells.py` 增加第十八组 GameTest，123 刻 / 34 点，额外逐刻比较原版钟的摆动状态。`runReferenceTool.py ExportBellHits <绝对输出路径>` 生成 2,688 次方向和浮点边界对照。已有电源旁新放音符盒/钟的通知行为也在场景中，详见[钟的说明](bells.md)。

`exportJukebox.py` 导出 22 张默认唱片定义。`captureJukeboxes.py` 顺序生成第十九、二十组 GameTest，151 刻 / 68 点与 1,456 刻 / 11 点，逐刻比较播放进度、输出及库存，覆盖首次注册、重新注册、曲终解锁和漏斗空槽条件。`ExportJukeboxPlayback` 使用与 `ExportDropperMotion` 相同的四路径参数，22 张唱片完整播放共 4,265 个隔离样本，详见[唱片机说明](jukeboxes.md)。

`ExportComposting` 在通常四路径参数之后，追加 `data/compostingRules.json` 的绝对输出路径，导出 115 种材料和 4,176 次隔离插入记录。`captureComposters.py` 增加第二十一组 GameTest，111 刻 / 26 点，验证临时输入/输出容器及失败抽取副作用，见 [堆肥桶说明](composters.md)。

`runReferenceTool.py ExportSineTable <绝对输出路径>` 用反射导出原版 `Mth.SIN` 全部 65,536 项与 1,079 组日光临界角度；核心回归逐项比较 float 位模式，全部相同。`captureRepeaterLockRefresh.py` 增加中继器锁定刷新组：13 刻 / 7 点，覆盖竖直形状更新刷新 `LOCKED`、水平对照与真实锁定对照。

`captureRemovalNotifySource.py` 增加移除回调来源组：17 刻 / 20 点，拉杆、中继器、比较器被移除时的通知来源方块，用被通知位置再外一格的三向普通铁轨读取。

`captureWireShapeToggle.py` 增加粉线点/十字切换组：21 刻 / 6 点，用准连接未通知的活塞检测右键切换发出的邻居通知；捕获器的 `interact` 相应支持红石粉。

`capturePistonLandingShape.py` 增加活塞落地形状组：21 刻 / 12 点，被推动的楼梯重算内角、音符盒重读乐器、侦测器落地即排脉冲。

`captureObserverRemoval.py` 增加侦测器移除组：21 刻 / 11 点，用准连接但未被通知的活塞检测移除时是否发出输出侧通知，覆盖过时供电状态、计划刻仍排队、探测器后放置以及移除后立即重放置四种情形。

`captureDiodeSupportBreak.py` 增加二极管断支撑组：21 刻 / 34 点，中继器与比较器在供电与不供电两种状态下失去支撑，用二极管正南两格的三向普通铁轨读取原版移除时额外发出的六向通知。

`capturePistonRemovalCallback.py` 增加活塞移除回调组：31 刻 / 14 点，把带电避雷针推走，用三向普通铁轨读取移除时刻的通知；含一组不供电的对照。

`captureRailNotificationSource.py` 增加邻居通知来源组：41 刻 / 29 点，用三向普通铁轨读取通知来源方块，覆盖红石粉隔 0/1/2 个导体、红石火把放置与移除、活塞推走铁轨上方红石块，另含一组红石粉旁双层门的对照。

`captureStairShapes.py` 增加楼梯连接形状组：17 刻 / 153 点，覆盖四朝向 × 上下半 × 内外角、`canTakeShape` 阻断、原版玩家放置、拆除回到 `straight`，以及内角侧面变 sturdy 后红石墙火把的存活与消失。`ExportReference` 同时逐方块导出 `stairs`（`block instanceof StairBlock`）。`CaptureRedstone` 的 `playerPlace` 对楼梯按其 `getStateForPlacement` 语义设置玩家朝向与点击面；箱子路径不变。

`captureDaylightVibration.py` 增加日光传感器振动组：81 刻 / 6 点，覆盖右键切换 `inverted` 发出的 `block_change`，包括两个切换方向、距离 3 与 5 格的行进刻数，以及背面滤波为 11（接受）和 9（拒绝）的校频感测体。场景不观察传感器自身 `power`，天空亮度仍是外部刺激。

`capturePistonPushability.py` 增加活塞可推性组：25 刻 / 36 点，覆盖普通方块、按原版 `getDestroySpeed()==-1` 拒绝的基岩与末地传送门框，以及釉面陶瓦 `PUSH_ONLY` 的推出、不可拉回和侧向分支留置。`ExportReference` 同时导出每个方块状态的 `destroySpeed`，注册表数据指纹随之变化，旧 `.vmcb` 需要显式迁移。详见 [修复进度](redstoneAudit/fixProgress.md)。
