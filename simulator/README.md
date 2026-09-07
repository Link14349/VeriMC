# simulator

VeriMC 的 Minecraft 红石电路模拟子项目。C++20 原生执行，Three.js 浏览器界面；全部实现位于本目录。

目前是可运行的首个预览版本，完成基础电路的搭建、运行和调试闭环。**整个模块仍在开发中，尚未完整覆盖红石器件。** 完整进度与已验证范围见 [实施记录](docs/implementationStatus.md)，目标范围见 [设计文档](../docs/module-1-design.md)。

## 启动

当前验证平台：macOS / Apple Silicon，Apple Clang 17，Boost 1.90，nlohmann-json 3.12，Zstandard 1.5.7，OpenSSL 3，Node 20.11。日常使用无需 Java 或 Minecraft。

```sh
# 安装构建依赖（已安装则跳过）
brew install cmake boost nlohmann-json zstd openssl@3 node

# 在仓库根目录运行；构建后自动打开默认浏览器
python3 simulator/runSimulator.py

# 已构建后直接启动
python3 simulator/runSimulator.py --no-build
```

如果页面提示后台版本过旧，需要重新启动原终端中的服务；仅刷新页面不会替换正在运行的 C++ 进程。启动器会检查文件接口版本，避免把旧后台当成已更新版本。

服务只监听 `127.0.0.1:28765`。保留终端运行，Ctrl+C 停止。`--no-open` 不打开浏览器，`--port` 指定另一个本机端口。

## 使用

- 初始样例：拉杆经过两级中继器控制红石灯；按钮驱动铜灯记忆电路。
- 左侧选择器件，按 `2` 或点击放置工具，在当前 Y 层左键放置。右侧可铺底板和调整方向，`R` 旋转。
- 右键拖动旋转、中键平移、滚轮缩放。支持俯视、适合画面和编辑层剖切。
- `1` 选择；`3` 移除；`4` 放置探针；`5` 操作拉杆、按钮等。Space 运行/暂停，`S` 前进一个游戏刻。
- 不限速模式在电路稳定后停在最后一个事件时刻；底部“事件/s”统计连续运行的实际计划事件，不把空闲时间跳跃算成处理速度。
- 属性面板检查坐标、方块状态和信号强度；中继器、比较器的朝向属性指向输入。
- 波形支持缩放、跟随、双游标和触发暂停；探针菜单的 `↑` 或 `↓` 设置边沿断点。Shift 单击波形放置 B 游标。
- 浏览器接收滞后、未确认采样满额时自动暂停并保留后续事件；接收完成后可继续运行。历史截断会显示数量，具体预算和极端中止条件见 [采样传输](docs/traceTransport.md)。
- 工程菜单默认导出 `.vmcb` 电路或 `.snapshot.vmcb` 运行快照，导入同时接受 `.vmcb` 和旧 `.verimc.json` / `.json`。新工程只导出 VMCB，JSON 仅用于读取旧工程。快照包含计划事件、器件内部状态与探针历史；文件操作显示进度并可取消，失败保留当前工程，成功导入可以撤销。
- VCD 一个游戏刻对应 50 ms，同刻边沿保留顺序，不虚构物理子刻时间。
- Cmd/Ctrl+Z 撤销、Shift+Cmd/Ctrl+Z 重做，采用原生内存快照并控制历史预算。Cmd/Ctrl+C、Cmd/Ctrl+V 复制/粘贴当前器件。

**`.vmcb` 已接入 C++ 读写和浏览器工程菜单**。文件通过 HTTP 分块上传/下载，布局直接进入原生分区，浏览器不解析完整工程 JSON。格式使用状态表复用、16³ 分区、位打包/稀疏/游程编码、独立 Zstandard 压缩与 CRC；详见 [格式规范](docs/vmcbFormat.md) 和 [文件规模实测](docs/vmcbFiles.md)。当前默认读取预算为 200 万方块、8 GiB 文件及 2 GiB 候选内存预估；极端稀疏布局可能先触及内存预算。旧 JSON 使用原生解析器保留 64 位整数，但完整 DOM 的内存成本仍然存在。

## 当前覆盖

可用：结构支撑方块、红石块、拉杆、按钮、粉线、火把、中继器、比较器、侦测器、红石灯、铜灯、普通/黏性活塞、黏液/蜂蜜黏连、门/活板门/栅栏门。

环境输入面板支持普通/测重压力板、阳光探测器、避雷针、讲台和标靶。炼药锅、蜂箱、重生锚等支持方块状态的比较器读数，具体边界见 [环境输入](docs/environmentInputs.md)。工程菜单提供“活塞实验”和“环境实验”样例。四类铁轨的连接、供电及矿车接触输入已开放，详见 [铁轨说明](docs/rails.md)。普通箱、陷阱箱、铜箱、木桶已支持库存编辑与双箱联动；漏斗已支持容器之间的传输与锁定，提供“漏斗实验”；掉落物吸入、加工等仍在开发。原版状态表的覆盖范围大于实际仿真覆盖范围。

绊线与绊线钩已支持连接、接触检测、断线脉冲和剪刀操作，提供“绊线实验”，详见 [绊线说明](docs/tripwire.md)。界面支持较窄的浏览器窗口，最小工作区宽度为 640 像素。

按钮环境面板支持箭矢留在按钮内、只触及弹起部分和移走箭；木按钮按原版周期复查并保留同刻释放与重按，石按钮忽略箭矢。

投掷器支持九槽编辑、随机投放和向容器传输，提供“投掷器实验”。向外抛出时显示初始位置与速度并等待环境反馈，支持动作导出与快照续跑；随机语义边界见 [投掷器说明](docs/droppers.md)。

八种铜箱支持跨变体配对、去蜡与氧化程度同步，转换保留库存，具体规则见 [铜箱说明](docs/copperChests.md)。

雕纹书架支持六槽书籍、最后操作槽位读数，以及漏斗/投掷器传输；清空后保留读数，详见 [书架说明](docs/bookshelves.md)。

饰纹陶罐支持单槽库存、容量读数和六向传输，详见 [陶罐说明](docs/decoratedPots.md)。

幽匿感测体和校频感测体支持振动传播、遮挡、频率筛选和紫水晶共振，提供“振动实验”及频率探针。现有按钮、门、容器等来源会自动发出事件，未接入的来源可显式输入；完整边界见 [振动说明](docs/vibrations.md)。

钟支持供电/敲击、支撑转换和振动，详见 [钟的说明](docs/bells.md)。

唱片机支持 22 张默认唱片、播放计时、比较器与库存传输；曲终保留唱片，浏览器可查看播放进度，详见 [唱片机说明](docs/jukeboxes.md)。

堆肥桶支持 115 种材料、概率升层、20 gt 成熟及漏斗进出料，详见 [堆肥桶说明](docs/composters.md)。

音符盒支持调音、材质/头颅乐器、遮挡和演奏振动，提供“音符实验”与演奏记录，详见 [音符盒说明](docs/notes.md)。

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
python3 tests/fileTransferTests.py
python3 tests/vmcbReaderTests.py
python3 tests/backpressureTests.py
python3 tests/runControlTests.py
python3 tests/dropperTests.py
python3 tests/targetTests.py
python3 tests/vibrationTests.py
python3 tests/noteTests.py
python3 tests/bellTests.py
python3 tests/jukeboxTests.py
python3 tests/composterTests.py
node tests/webTests.mjs

# 有实际驱动的中继器链基准
./build/simulatorCli --benchmark 1000

# 原生工程文件基准；输出需放在忽略的 testResults 下
mkdir -p testResults
./build/vmcbBenchmark 1048576 dense testResults/million.vmcb

# 预热后测量五次，并保存中位数、p95、原始样本和构建身份
python3 tools/runBenchmarks.py --circuits 10000 --output testResults/baseline.json
python3 tools/runBenchmarks.py --circuits 10000 --probes 64 --output testResults/probes64.json
```

Release、ASan/UBSan、原版 GameTest 差分和浏览器实操分别验证。参考生成流程及边界见 [参考验证](docs/referenceValidation.md)。

## 开发约定

- lowerCamelCase：函数、变量、成员、命名空间和项目文件。
- UpperCamelCase：类型。Minecraft 原始方块 ID、第三方代码与工具规定文件名保持原样。
- CMake 管理 C++，前端构建为本地静态资源；正常使用不依赖远程页面。
- 提交说明不超过 20 字，并点明新增功能或修复问题。
