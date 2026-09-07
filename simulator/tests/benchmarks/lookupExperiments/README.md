# 2026-09-07 中间性能实验

这些文件保留本轮调优过程的原始测量，便于辨认未采用的方案；最终交付结果是上级目录的 `checkedDispatchM1.json`、`checkedDispatch64ProbesM1.json` 和 `performanceReplayM1.json`。

- `worldLookup*`、`queryChanges*`：区块写缓存、负坐标移位与按需支撑查询；部分文件是不同时间的交错复测。
- `worldLookupLto*`：另开 ThinLTO 的实验，没有额外收益，最终未采用。
- `updateQueueM1.json`：另拆分入队/执行函数的实验，没有额外收益，最终未采用。
- `neighborDispatchM1.json`：加上无响应邻居通知的提前返回。
- `alignedStatesM1.json`：进一步对齐只读状态表，随后进行了最终的交错复测。
- `*Replay.json`：相应阶段与原始版本的完整快照比较；最终比较见上级目录。

各阶段样本来自普通桌面，存在时间漂移和后台活动，不能挑取各组最快值拼成加速比。源码和二进制散列仅标识当时构建；这些未提交的中间版本没有独立 Git 提交。
