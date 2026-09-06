# Minecraft 26.2 参考与验证

参考版本固定为 Java Edition 26.2 正式版，非实验性红石。官方版本清单、服务端下载地址和 SHA-1 记录于 `data/referenceVersion.json`；原版 DataVersion 为 4903。

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
