# 固定组件与跨层连接实施计划

2026-09-08，用户已授权继续 M2。常规组件读取由 Claude CLI Opus 实现，连接检查、仿真适配和集成由 Codex 实现。

**Goal:** 生成有限、固定位置的小组合电路，保留组件、端口、初始化和来源，真实执行 C++ 仿真并比较输入输出。

**Architecture:** `verimc/tools/` 的 Python 标准库工具作为物理装配研究入口，暂不修改 C++ HDL 编译器的 build 语法。组件资产读取与空间变换独立于受限连接规则；`verimc/physicalAdapter/` 链接现有 simulatorCore，按有序计划执行，输出真实状态与验证报告。

**Tech Stack:** Python 3、C++20、CMake、现有 simulatorCore。

## 交付步骤

1. `tools/physicalCells.py`：严格组件读取，路径与哈希、端口、旋转、位置支持域及初始化校验。未知能力拒绝。`tests/physicalCellsTests.py` 覆盖解析与空间边界。
2. `tools/physicalClosure.py`：固定实例装配；仅支持一根数字信号沿指定直线水平轴的单调阶梯线路，端点由组件引脚确定。长度、支撑、头部空间、keepout、其它组件隔离先检查。没有搜索，不包装为 route auto。
3. `physicalAdapter/`：独立 CMake 工程复用 C++ 核心。先用注册表预检，再执行每个组件及线路的有序放置，真实玩家拉杆输入和指定输出面的观察；不直接写 powered 当作信号传播。
4. `examples/physical/`：两端固定中继器组件、由低层到高层的红石粉阶梯，外部拉杆是显式测试适配器。正反向输入转换与逻辑恒等模型对比。产出设计包、可打开的 simulator 工程和单独验证报告。
5. `tests/physicalClosureTests.py` 与原生集成：负坐标、四个水平旋转、碰撞/隔离/超长/高度/初始化拒绝、确定性输出、输出通道方向、真实仿真边沿。

## 验收与边界

组件清单和物理设计包保持 unverified，不伪造目标构建 SHA-256 或原版证据。simulator 场景结果单独记录，附当前规则、内核构建指纹和输入/初始化/观测哈希。有限场景通过不产生时序通用上界或所有历史等价声明。

此阶段不是自动布局布线、HDL 任意逻辑映射或蓝图导出。接口仅 bit，方块实体、同步时钟和移动组件不支持。受限实验包有独立版本标记，缺失规范所需原版证据时不得发布为完整规范的成功物理构建。

## 验证命令

`python -m unittest discover -s verimc/tests -p '*physical*Tests.py'`

`cmake -S verimc/physicalAdapter -B verimc/buildPhysical -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<本地依赖前缀>`

`cmake --build verimc/buildPhysical -j 6`

`python verimc/tests/physicalIntegrationTests.py <physicalAdapter可执行文件>`

完成后记录实际结果并作本地提交，不推送远程。临时文件删除受工具自动审批阻挡，保留并报告。
