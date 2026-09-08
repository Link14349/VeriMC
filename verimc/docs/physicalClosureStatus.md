# 固定组件与跨层连接：首个可执行闭环

2026-09-08。入口是 `tools/physicalClosure.py`，使用 Python 标准库组织物理资产，实际方块执行仍由原生 C++ `simulatorCore` 完成。它是受限实验工具，**没有接入 HDL 编译器的 build/route auto，也不输出完整物理规范的成功构建**。

## 已实现的数据流

`ascending.fixed.json → 严格读取 vmcell → 固定放置和旋转 → 受限阶梯线路 → physicalExperiment.json → C++ 有序放置/拉杆交互/探针 → 逻辑接口比较`。

组件清单与引用资产计算 SHA-256。固定放置检查负坐标余数、允许方向、世界高度、包围盒、占用与 keepout。编译后的实验包保留全部方块、interfaceMap、sourceMap、有序初始化计划和资产/工具锁定信息。sourceMap 指向真实组件清单或实验请求的 UTF-8 字节区间，**不是 HDL 源码映射**。

当前仅识别支撑石块与一个中继器组成的固定 buffer，内部不优化、不改写。两端朝向一致，一根数字信号沿一个水平轴通过红石粉阶梯连接。跨度限定 3–14 格、高度变化 0–跨度减 3，每端保留水平接入。线路有支撑、头部空间和隔离区检查。超范围时明确失败，不暗中添加中继器，不做路径搜索。

外部输入通过额外、明确列入材料和来源的落地拉杆提供；它与组件输入引脚不同。仿真按组件顺序放置，再放置线路和输入适配器，使用 `Simulator::place` 和 `interact` 触发真实核心更新。输出按引脚指定的 strongOutput 通道读取；适配器将向外引脚面转换为内核的接收端到信号源方向。

## 运行

从仓库根目录生成包（Windows 的 python 或 Linux 的 python3 均可）：

```sh
python verimc/tools/physicalClosure.py build verimc/examples/physical/ascending.fixed.json --root verimc --output verimc/testResults/physicalExperiment.json
```

构建适配器，依赖与 simulator 相同，但不需要 Boost/网页服务：

```sh
cmake -S verimc/physicalAdapter -B verimc/buildPhysical -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<本机依赖前缀>
cmake --build verimc/buildPhysical -j 6
```

使用同一操作系统环境的 Python 与原生可执行文件：

```sh
python verimc/tools/physicalClosure.py verify verimc/testResults/physicalExperiment.json --root verimc --adapter verimc/buildPhysical/physicalAdapter --output-dir verimc/testResults/physicalClosure
```

Windows 多配置生成器的可执行文件通常位于 `verimc/buildPhysical/Release/physicalAdapter.exe`。WSL 构建应使用 WSL Python 和 `/mnt/...` 路径，不直接由 Windows Python 执行 Linux ELF。

验证生成两个文件：

- `simulationReport.json`：真实观察值、探针边沿、内核构建指纹、规则指纹、初始化/时间线/观察值哈希、逻辑比较结果。
- `circuitDesign.json`：可在现有模拟器中打开的设计，保留初始关闭的拉杆及附加来源元数据。单独导入设计不会自动重放测试时间线；完整复现须保留实验包并运行适配器。

输出明确写 `unverified`；原版构建 SHA-256 缺失时为 null，绝不虚构。实际 C++ 测试通过在独立报告中体现，不反写组件 evidence 为 scenarioVerified。

verify 会重新核对全部来源资产和工具哈希；源码或工具变化时拒绝旧包，要求重新构建，不沿用过期 sourceMap。

## 验证和未覆盖

```sh
python -m unittest discover -s verimc/tests -p '*physical*Tests.py'
python verimc/tests/physicalIntegrationTests.py verimc/buildPhysical/physicalAdapter
```

WSL GCC 11.4 Release 集成的 91 个场景通过：四个朝向 × 正负坐标共 8 种放置、所声明范围全部 78 种跨度/升层组合，另有断线对照、缺失初始化、非法方块属性、错误输出面、过早采样。每个有效放置检查初始值、上升/下降、保持输入和全部输出探针边沿。示例共 16 个真实方块，跨越 2 层；当前场景输出边沿发生在输入翻转后 4 gt，**这是观测结果，不是通用时序保证**。

最终组件与装配回归共 155 项：Windows Python 3.13 运行 155 项，154 通过、1 项文件符号链接权限相关测试跳过；WSL Python 3.10 全部 155 通过。目录联接逃逸在 Windows 实际检查，文件/目录符号链接逃逸在 WSL 实际检查。审查另补充浮点格式版本拒绝、过期资产/工具锁拒绝，测试临时根使用独立目录。

尚未验证：原版差分、脉宽/刻内顺序的全域表征、外部环境干扰、同步采样窗口、任意逻辑映射、分支线路、下降/转弯/交叉线路、自动布局布线、蓝图导出。当前恒等 bit 模型只用于有限输入输出比较，不证明两个组件模型结构等价。

## 分工

实际 Claude CLI 模型 `claude-opus-5` 实现组件读取、变换、诊断、相应回归和子集说明。Codex 实现受限连接规则、C++ 适配器、真实仿真集成、示例和最终审查。
