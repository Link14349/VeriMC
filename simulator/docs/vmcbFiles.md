# VMCB 文件验证与规模测量

2026-09-07，Apple M1 / 16 GiB，macOS 15.7.4，Apple Clang 17，C++20 Release，Zstandard 1.5.7 level 3。原始数据及源码指纹见 [vmcbFilesM1.json](../tests/benchmarks/vmcbFilesM1.json)。

本轮每种布局运行一次，未做预热、多次中位数或 p95。写入计时包含编码和文件关闭；读取计时包含解码、校验及电路初始化。未执行 fsync，不把结果当作持久化磁盘速度。RSS 是整个原生进程的高水位，包含注册表、源世界、候选世界及初始化副本，在构造旧 JSON 对照数据之前读取。

| 布局 | 方块数 | 分区数 | 完整 VMCB 字节 | 写入秒 | 读取秒 | 峰值 RSS MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 16 种羊毛密集布局 | 100,000 | 64 | 59,459 | 0.0143 | 0.0080 | 97.2 |
| 16 种羊毛密集布局 | 1,048,576 | 256 | 547,647 | 0.0933 | 0.0653 | 97.4 |
| 16 种羊毛密集布局 | 2,000,000 | 512 | 1,068,186 | 0.1805 | 0.1246 | 97.5 |
| 每区 64 个同状态方块 | 1,048,576 | 16,384 | 1,475,265 | 2.3253 | 0.6964 | 856.8 |
| 每个木桶含两个库存槽 | 10,000 | 40 | 28,216 | 0.0899 | 0.1704 | 101.5 |

每组往返均逐方块比较坐标与状态，容器另比较库存，全部相同。密集布局用 mt19937 种子 24680 选择 16 种羊毛；稀疏布局每个分区的局部索引为 0、64…4032；木桶槽 0 为 64 个石头，槽 13 为 31 个红石。导出的都是电路设计，无探针、波形或活动队列；不代表动态红石仿真 TPS、快照恢复或浏览器 FPS。

约 105 万方块的密集样本为 547,647 字节（0.522 MiB），稀疏样本为 1,475,265 字节（1.407 MiB）。稀疏布局的内存和目录成本明显更高，不能按文件大小推算可运行规模。

## 同语义 JSON 对照

使用同一源世界、名称与电路导出选项；JSON 压缩对整份文本采用同一 Zstandard level 3，VMCB 按段压缩。以下计完整文件/文本，不仅是布局：

| 样本 | VMCB | 紧凑 JSON | 缩进 JSON | 紧凑 JSON + zstd | 缩进 JSON + zstd |
| --- | ---: | ---: | ---: | ---: | ---: |
| 10 万密集方块 | 59,459 | 6,476,817 | 13,276,909 | 418,583 | 489,899 |
| 1 万库存木桶 | 28,216 | 2,330,583 | 5,110,678 | 24,800 | 32,283 |

对重复库存的样本，整份紧凑 JSON 压缩后比 VMCB 略小；分区目录、独立压缩和规则身份有固定成本。VMCB 的收益也包括原生布局直接读取、避免逐方块 JSON DOM、分段校验和内存预算，并不承诺所有数据都比整文件压缩 JSON 更小。

## 正确性与集成

- Release 与 ASan/UBSan：83 项既有核心测试及 11 组 VMCB 测试通过。覆盖四种编码、独立字节向量、旧 JSON 大整数、初始化对照、器件快照续跑、损坏与取消。
- 独立 Python 读取器读取仓库内的 181 方块按钮快照，核对文件头/目录/CRC/状态布局、tick 7 和 tick 20 的按钮释放事件。该读取器独立于 C++ 编解码实现。
- 本地文件协议：VMCB 读写、旧 JSON 导入及拒绝 JSON 导出、撤销重做、快照继续运行、损坏/取消/截断/超额上传保持原工程，会话/来源校验。现有 HTTP/WebSocket、背压和运行控制回归通过。
- 前端构建和 8 项前端测试通过；浏览器实操完成 VMCB 快照导入和默认下载，控制台无错误。

未覆盖的规模验收：大型动态快照、百万方块浏览器发布/渲染、服务复制世界暂停时间、取消延迟、磁盘写满/崩溃注入、持续覆盖率引导模糊测试。压缩期间可查询/取消，但复制世界及部分排序/初始化步骤不是可抢占的。旧 JSON 路径仍需要完整 DOM，默认按文件长度限制其内存风险。

## 复现

在仓库根目录先按 README 完成 Release 构建，再运行：

```sh
mkdir -p simulator/testResults/vmcb
./simulator/build/vmcbBenchmark 100000 dense simulator/testResults/vmcb/dense100k.vmcb
./simulator/build/vmcbBenchmark 1048576 dense simulator/testResults/vmcb/dense1m.vmcb
./simulator/build/vmcbBenchmark 2000000 dense simulator/testResults/vmcb/dense2m.vmcb
./simulator/build/vmcbBenchmark 1048576 sparse simulator/testResults/vmcb/sparse1m.vmcb
./simulator/build/vmcbBenchmark 10000 containers simulator/testResults/vmcb/containers10k.vmcb
python3 simulator/tests/vmcbReaderTests.py
```

生成的规模样本属于测试产物，保存在忽略的 testResults 中，不进入仓库。
