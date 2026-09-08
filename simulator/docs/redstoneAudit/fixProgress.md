# 红石审计问题修复进度

本文件按 issue 逐项记录已完成的修复、原版证据和明确排除项。审计快照本身留在
[auditReport.md](auditReport.md)、[implementationReview.md](implementationReview.md) 与
[coreReview.md](coreReview.md)；那些文档描述 2026-09-08 当时的状态，实际状态以本文件为准。

参考环境与审计当时一致：Java Edition 26.2 正式版，游戏 JAR SHA-256
`183c0499c5f855570ee487dd38e141a53f0121f83a0b07a3bac2d8b6698823e8`，JDK 25.0.4.1，
GameTest 功能开关 `minecraft:vanilla` + `minecraft:trade_rebalance`，`randomTickSpeed=0`，
实验红石关闭。仍然不能称为严格 vanilla-only 专用服务器验证。

## R10 活塞推动判据（issue #3）已修复

**根因**：`Simulator::pushable` 用方块名白名单代替原版
`PistonBaseBlock.isPushable` 的 `state.getDestroySpeed(level, pos) == -1.0F` 判断，
因此末地传送门框（`destroySpeed = -1`、`pushReaction = NORMAL`、无方块实体）被错误允许推动。
`PUSH_ONLY` 分支在原版是 `return direction == connectionDirection` 直接返回，
C++ 却在其后又执行了一次 `!hasBlockEntity()`。

**改动**：

- `tools/reference/ExportReference.java` 增加导出每个方块状态的 `destroySpeed`
  （`BlockBehaviour.BlockStateBase.getDestroySpeed` 返回的是与位置无关的常量字段，逐状态导出即精确）。
- 重新生成 `data/blockStates.json`（SHA-256 `a7f075994561df20f9be27071933ced23164f34612327ecc8bbec5c41bf19a16`）。
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
