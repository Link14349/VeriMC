# PR #2 审阅反馈处理（2026-09-10）

审阅基线为 `8cf3533`。先建立最小回归，再修改实现；最终修复四项确定缺陷，对跨区块接触建议用原版实验重新判定。未把“所有测试通过”作为全世界兼容的证明。

## 已修复

1. **展示框移除清空宿主库存**：运行记录同时持有 values、inventory 和 output，只在三者均空时回收。移除后仍发出运行状态变化通知。新增箱子和漏斗各存 10 个石头的组合回归，检查移除展示框后的库存与快照。
2. **VMCB 丢失区块状态**：电路与快照的 META 都写入完整 `chunkStates`，即使空表也明确写 `[]`。读入后通过公共工程恢复路径验证；缺失、重复和非法邻接表原子拒绝。覆盖冻结按钮队列的二进制往返、恢复 ticking 后释放及与 JSON 续跑一致。
3. **JSON 表恢复依赖顺序**：先解析并建立完整区块表，再检查 unloaded 邻居约束，避免把尚未读取的邻居误当成默认 entityTicking。覆盖 loaded 的 3×3 环、unloaded 中心、正逆序、电路/快照、重复记录和非法完整拓扑。坐标先限制到可安全乘 16 的范围，避免溢出。
4. **规则指纹漏掉标签**：`blockTags.json` 纳入八文件指纹。回归在隔离副本中改变标签文件字节，要求规则指纹变化。

上述四项各有旧实现失败、新实现通过的回归。本地完整失败日志在 `simulator/testResults/pr2Fixes/baselineTests.log`。其余原版差分不因修复而改写预期。

## 跨区块接触：撤回试作修改，保留原版反例

审阅指出“物品所在区块 entityTicking、漏斗区块 blockTicking 时外层调度阻止拾取”。这准确描述了当前模型的拦截，但**仅凭模型内的假定无法推出原版应当拾取**。

新增 `java26_2CrossChunkContact` 严格 vanilla-only 捕获，固定原点 `[16,-59,32]`，17 帧、2 个观察点：

- 漏斗相对位置 `[16,1,4]`，上方石头阻断常规吸取；第 2 刻撤销漏斗区块的强加载票据。
- 第 5 刻声明物品偏移 `[-0.05,0.75,0.5]`。观测确认漏斗区块为 blockTicking，邻区块仍为 entityTicking。
- 原版第 6 刻库存仍为空，直至本场景结束都没有拾取。试作的“按物品区块放行”修改却在第 6 刻输出模拟值 1、原版 0，故撤回该修改。
- 额外 `--probe` 记录真实位置和 `noPhysics`：第 6 刻物品从原始 x=15.95 挤到相对 x=`16.224728864431384`，`noPhysics=true`，已经跨入非实体 ticking 区块。不是静止在原声明位置持续触发接触。
- 源码链对应 `ItemEntity.tick` 的碰撞/挤出、`Entity.isAffectedByBlocks` 的 `!noPhysics` 守卫，以及 `HopperBlockEntity.entityInside`。探针只读取字段；移除探针字段后的整条观察时间线与无探针捕获一致。

最终内核保留既有调度守卫，新场景已作为正向原版回归注册；“不拾取”也是需要保留的行为。本场景不能证明其他运动轨迹或全部跨区块接触正确。**完整碰撞、挤出、动态位置/区块归属与接触顺序仍未建模**，#12 保持打开。尤其不能把声明式 groundItems 的位置不变假设当成真实实体轨迹。

重现：在配置好 JDK 25、参考缓存和 Release checker 后运行：

```sh
python3 simulator/tools/reference/captureCrossChunkContact.py /absolute/output
python3 simulator/tools/reference/captureCrossChunkContact.py /absolute/probeOutput --probe
```

## 验证和兼容性

最终 Release CTest 3/3，核心 140/140，VMCB 14/14；时间线 54/54。参考工具失败路径 5/5、暂停拒绝 1/1，并用独立 Python 二进制读取器检查本轮生成的 checkpoint。展示框、漏斗拾取、blockTicking 和抛出反馈四场景在本轮另行严格原版重跑；跨区块新增场景另外运行了有/无探针两次。详细结果和规范化内容哈希见 `pr2ReviewFixEvidence.json`。

本轮也修正交付判断：核心测试和 checkReference 共用重放器，不能记成两份独立原版证据。新增源码回归及二进制组合测试与原版运行分别报告。

规则指纹变化使旧 `.vmcb` 明确要求迁移；快照 ABI 为 `simulatorCheckpoint2`，旧 ABI 拒绝。没有实现自动迁移，也无法从已丢失字段的旧二进制中猜回区块状态。保留原文件；可用旧版导出仍完整的 JSON，或以电路设计重新建立区块状态后运行。JSON 的历史兼容不构成旧运行语义的等价承诺。

没有性能测量或全平台验证。#12、#14、#15 的其余验收项继续开放。本轮推送用于让原作者复审确定修复及反例，不代替原作者的合并决定。
