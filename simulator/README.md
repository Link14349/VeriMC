# simulator

VeriMC 的 Minecraft 红石电路模拟子项目。C++20 原生执行，Three.js 浏览器界面；全部实现位于本目录。

目前是可运行的首个预览版本，完成基础电路的搭建、运行和调试闭环。**整个模块仍在开发中，尚未完整覆盖红石器件。** 完整进度与已验证范围见 [实施记录](docs/implementationStatus.md)，目标范围见 [设计文档](../docs/module-1-design.md)。

## 启动

当前验证平台：macOS / Apple Silicon，Apple Clang 17，Boost 1.90，nlohmann-json 3.12，Node 20.11。日常使用无需 Java 或 Minecraft。

```sh
# 安装构建依赖（已安装则跳过）
brew install cmake boost nlohmann-json node

# 在仓库根目录运行；构建后自动打开默认浏览器
python3 simulator/runSimulator.py

# 已构建后直接启动
python3 simulator/runSimulator.py --no-build
```

服务只监听 `127.0.0.1:28765`。保留终端运行，Ctrl+C 停止。`--no-open` 不打开浏览器，`--port` 指定另一个本机端口。

## 使用

- 初始样例：拉杆经过两级中继器控制红石灯；按钮驱动铜灯记忆电路。
- 左侧选择器件，按 `2` 或点击放置工具，在当前 Y 层左键放置。右侧可铺底板和调整方向，`R` 旋转。
- 右键拖动旋转、中键平移、滚轮缩放。支持俯视、适合画面和编辑层剖切。
- `1` 选择；`3` 移除；`4` 放置探针；`5` 操作拉杆、按钮等。Space 运行/暂停，`S` 前进一个游戏刻。
- 属性面板检查坐标、方块状态和信号强度；中继器、比较器的朝向属性指向输入。
- 波形支持缩放、跟随、双游标和触发暂停；探针菜单的 `↑` 或 `↓` 设置边沿断点。Shift 单击波形放置 B 游标。
- 浏览器接收滞后、未确认采样满额时自动暂停并保留后续事件；接收完成后可继续运行。历史截断会显示数量，具体预算和极端中止条件见 [采样传输](docs/traceTransport.md)。
- 工程菜单导出/导入 JSON 电路或运行快照。快照包含计划事件、器件内部状态与探针历史。VCD 一个游戏刻对应 50 ms，同刻边沿保留順序，不虚构物理子刻时间。
- Cmd/Ctrl+Z 撤销、Shift+Cmd/Ctrl+Z 重做，采用原生内存快照并控制历史预算。Cmd/Ctrl+C、Cmd/Ctrl+V 复制/粘贴当前器件。

## 当前覆盖

可用：结构支撑方块、红石块、拉杆、按钮、粉线、火把、中继器、比较器、侦测器、红石灯、铜灯、普通/黏性活塞、黏液/蜂蜜黏连、门/活板门/栅栏门。

环境输入面板支持普通/测重压力板、阳光探测器、避雷针、讲台和标靶。炼药锅、蜂箱、重生锚等支持方块状态的比较器读数，具体边界见 [环境输入](docs/environmentInputs.md)。工程菜单提供“活塞实验”和“环境实验”样例。四类铁轨的连接、供电及矿车接触输入已开放，详见 [铁轨说明](docs/rails.md)。普通箱、陷阱箱、木桶已支持库存编辑与双箱联动；漏斗已支持容器之间的传输与锁定，提供“漏斗实验”；掉落物吸入、加工等仍在开发。原版状态表的覆盖范围大于实际仿真覆盖范围。

绊线与绊线钩已支持连接、接触检测、断线脉冲和剪刀操作，提供“绊线实验”，详见 [绊线说明](docs/tripwire.md)。界面支持较窄的浏览器窗口，最小工作区宽度为 640 像素。

## 验证

```sh
cd simulator
cmake --preset release
cmake --build --preset release -j 6
ctest --preset release

cmake --preset debug
cmake --build --preset debug -j 6
ctest --preset debug

# 服务启动后，验证 HTTP / WebSocket 完整流程
python3 tests/serverTests.py
python3 tests/backpressureTests.py
node tests/webTests.mjs

# 有实际驱动的中继器链基准
./build/simulatorCli --benchmark 1000
```

Release、ASan/UBSan、原版 GameTest 差分和浏览器实操分别验证。参考生成流程及边界见 [参考验证](docs/referenceValidation.md)。

## 开发约定

- lowerCamelCase：函数、变量、成员、命名空间和项目文件。
- UpperCamelCase：类型。Minecraft 原始方块 ID、第三方代码与工具规定文件名保持原样。
- CMake 管理 C++，前端构建为本地静态资源；正常使用不依赖远程页面。
- 提交说明不超过 20 字，并点明新增功能或修复问题。
