# Java 26.2 兼容性问题清单

本轮已检查主要机制区域，但尚未完成逐函数语义核对和全部组合特性的动态验证。此清单不代表完整兼容认证。

3 个已复现缺陷、6 个静态差异验证任务、1 个剩余假设任务、3 个未实现功能/覆盖任务、1 个原版特性验证任务。

已修复项的原版证据和排除范围见 [fixProgress.md](fixProgress.md)；下方复选框反映当前状态。

## 任务

- [x] [R6：[已修复] 日光传感器切换漏发振动事件](https://github.com/Zhen-WushuiLingchun/VeriMC/issues/1)
- [x] [R7：[已修复] 相邻楼梯未更新连接形状](https://github.com/Zhen-WushuiLingchun/VeriMC/issues/2)
- [x] [R10：[已修复] 活塞错误允许推动末地传送门框](https://github.com/Zhen-WushuiLingchun/VeriMC/issues/3)
- [ ] [R1：[静态差异·待复现] 邻居通知来源方块与原版不一致](https://github.com/Zhen-WushuiLingchun/VeriMC/issues/4)
- [ ] [R2：[静态差异·待复现] 活塞移动时全局跳过移除回调](https://github.com/Zhen-WushuiLingchun/VeriMC/issues/5)
- [ ] [R3：[静态差异·待复现] 中继器和比较器断支撑处理阶段不同](https://github.com/Zhen-WushuiLingchun/VeriMC/issues/6)
- [ ] [R4：[静态差异·待复现] 侦测器移除缺少计划刻状态判断](https://github.com/Zhen-WushuiLingchun/VeriMC/issues/7)
- [ ] [R8：[静态差异·待复现] 活塞落地未完整重算邻居形状](https://github.com/Zhen-WushuiLingchun/VeriMC/issues/8)
- [ ] [R9：[静态差异·待复现] 粉线点/十字切换的额外邻居通知](https://github.com/Zhen-WushuiLingchun/VeriMC/issues/9)
- [ ] [hypotheses：[验证任务] 核对锁定刷新、状态快照和日光数值边界](https://github.com/Zhen-WushuiLingchun/VeriMC/issues/10)
- [ ] [itemFrame：[未实现] 比较器缺少物品展示框输入](https://github.com/Zhen-WushuiLingchun/VeriMC/issues/11)
- [ ] [entityIo：[未实现] 漏斗和投掷器缺少实体物品反馈链](https://github.com/Zhen-WushuiLingchun/VeriMC/issues/12)
- [ ] [unsupported：[覆盖清单] 未实现器件与占位支持门禁](https://github.com/Zhen-WushuiLingchun/VeriMC/issues/13)
- [ ] [quirks：[特性保留] 刻内差分、零刻/BUD 与区块生命周期验证](https://github.com/Zhen-WushuiLingchun/VeriMC/issues/14)

## 特性保留要求

准连接、黏性活塞短脉冲吐块、脉冲延长和更新顺序效应应按原版保留，不因其被称作 bug 而消除。新增 30 个布置、1,284 次位置/帧观测全部匹配，仅证明这些具体历史与采样字段。

R11 更新预算耗尽暂停是当前明确约定的差别；R12 空闲振动优化不是已确认故障；R14 占位器件仍拒绝放置。零刻机器、复杂 BUD、更新抑制其他路径和区块生命周期仍须补证据。

[详细策略与新增测试范围](https://github.com/Zhen-WushuiLingchun/VeriMC/blob/9c6f535/simulator/docs/redstoneAudit/quirkCompatibilityPlan.md) · [新增捕获哈希和环境](https://github.com/Zhen-WushuiLingchun/VeriMC/blob/9c6f535/simulator/docs/redstoneAudit/quirkDifferentialResults.json)

建议先修复三个真实反例，再核对更新执行主干，建设刻内差分和典型机器回归。区块/实体模型先明确设计和边界；本次建立任务不表示这些功能已经实现。

汇总入口：[issue #15](https://github.com/Zhen-WushuiLingchun/VeriMC/issues/15)。
