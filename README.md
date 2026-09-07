# VeriMC

面向 **Minecraft Java Edition 26.2 正式版**的红石硬件设计工具，包含方块级电路模拟器、VeriMC 硬件描述语言及编译前端、逻辑图查看器。

项目正在开发中。目前可以手工搭建并调试方块电路，也可以将 HDL 源码编译成逻辑网表，在浏览器中查看连接。**HDL 到 Minecraft 方块电路的转换、自动布局布线以及 Litematic / Create 蓝图导出尚未实现。**

## 子项目

| 目录 | 用途 | 技术栈 |
| --- | --- | --- |
| [simulator/](simulator/README.md) | 三维搭建、独立仿真、单步调试、探针波形、工程与运行快照 | C++20、CMake、Three.js、React |
| [verimc/](verimc/README.md) | 语言规范、教程、解析与静态检查、模块展开、逻辑网表输出和校验 | C++20、CMake、ANTLR4 |
| [vmcl-visualize/](vmcl-visualize/README.md) | 浏览器本地读取逻辑网表、自动排列逻辑图、搜索连接、导出 SVG | TypeScript、React Flow、ELK.js、Vite |
| [docs/](docs/module-1-design.md) | 总体设计、目标范围与验收要求 | Markdown |

三个子项目分别构建。模拟器日常运行无需 Java 或 Minecraft；逻辑图查看器无需业务后端。

## 快速开始

下面各组命令均从仓库根目录开始执行。原生项目目前主要在 **macOS / Apple Silicon** 上验证，其它平台尚未完成验证。

### 运行方块模拟器

需要 Python 3、C++20 编译器、CMake 3.25+、Boost 1.90+、nlohmann/json 3.12+、Zstandard 1.5+、OpenSSL 3、Node.js 与 npm。

```sh
# macOS：安装依赖（需已安装 Xcode Command Line Tools 和 Homebrew）
brew install cmake boost nlohmann-json zstd openssl@3 node python

# 构建 C++ 服务和网页，启动后自动打开浏览器
python3 simulator/runSimulator.py
```

默认地址为 [http://127.0.0.1:28765/](http://127.0.0.1:28765/)，在终端按 `Ctrl+C` 停止。再次启动可加 `--no-build` 跳过构建，`--no-open` 关闭自动打开浏览器，`--port` 指定端口。

工作台支持放置器件、操作拉杆、运行/暂停、单步和探针波形；可从工程菜单加载内置实验或导入 [8 位加法器样例](simulator/examples/README.md)。操作方法和器件覆盖见 [模拟器说明](simulator/README.md)。

### 编译 VeriMC 源码

需要 C++20 编译器、CMake 3.25+、Boost 1.90+、nlohmann/json 3.12+、OpenSSL 3，以及 Java 17+ 和 Python 3。Java 用于构建时生成解析器，Python 用于回归测试；运行编译器本身不需要二者。首次配置会下载并校验 ANTLR 4.13.2，缓存位于 `verimc/.cache/`。

```sh
cmake -S verimc -B verimc/build -DCMAKE_BUILD_TYPE=Release
cmake --build verimc/build -j 6

# 将计数器编译为逻辑网表；输出放在已忽略的构建目录中
verimc/build/verimcCli compile verimc/examples/valid/counter.vmc --top Counter -o verimc/build/counter.vmcl
verimc/build/verimcCli validate verimc/build/counter.vmcl
```

重复编译覆盖已有文件时需显式加 `--force`。从 [入门教程](verimc/docs/languageTutorial.md) 开始学习语法，更多参数与 C++ 接口见 [编译器说明](verimc/README.md)。

### 查看逻辑图

需要 Node.js 20.11+ 和 npm。

```sh
cd vmcl-visualize
npm ci
npm run dev
```

打开 [http://127.0.0.1:5174/](http://127.0.0.1:5174/)，选择内置示例，或拖入上一步生成的 `verimc/build/counter.vmcl`。读取、检查和布局均在浏览器中进行，文件不会上传。`npm run build` 生成可由静态服务器托管的 `dist/`；完整操作见 [逻辑图查看器说明](vmcl-visualize/README.md)。

## 文件格式与工作流

| 格式 | 含义 | 使用方式 |
| --- | --- | --- |
| `.vmc` | VeriMC HDL 源码 | 由 `verimcCli compile` 编译 |
| [`.vmcl`](verimc/docs/vmclFormat.md) | 逻辑网表，包含节点、类型、端口、连接及源码来源 | 编译器输出、独立校验、网页查看 |
| [`.vmcb`](simulator/docs/vmcbFormat.md) | 方块电路工程 | 模拟器导入与导出 |
| `.snapshot.vmcb` | 包含事件队列、器件状态和探针历史的运行快照 | 模拟器保存与恢复运行 |

当前 HDL 工作流为 `.vmc → .vmcl → 逻辑图查看器`；模拟器通过手工搭建或导入方块工程使用。两者之间的物理转换尚未接通，逻辑图的屏幕排列也不代表 Minecraft 布局布线。

## 验证

完成对应项目的构建后，可运行以下检查；这些命令覆盖原生测试、编译器回归和查看器检查，完整服务流程与原版差分验证见各子项目文档。

```sh
# 模拟器原生测试
ctest --test-dir simulator/build --output-on-failure

# 编译器、共享 IR 与 .vmcl 适配器测试
ctest --test-dir verimc/build --output-on-failure

# 查看器测试、类型检查与生产构建
npm --prefix vmcl-visualize test
npm --prefix vmcl-visualize run build
```

实际覆盖与验证记录：[模拟器进度](simulator/docs/implementationStatus.md)、[Minecraft 参考验证](simulator/docs/referenceValidation.md)、[编译器状态](verimc/docs/compilerStatus.md)、[查看器验证](vmcl-visualize/docs/verification.md)。

## 当前边界

- Minecraft 兼容目标固定为 Java Edition 26.2；模拟器仍在补充器件和环境行为，不能视为完整兼容。已验证行为、外部刺激接口和缺口以器件文档为准。
- 编译器处理选定顶层展开后的纯逻辑设计；源码中的 `test` / `build` 声明目前只做语法解析，不执行测试或物理实现。
- 逻辑图查看器不执行电路仿真；完整的按位组合环和时钟域检查需使用 `verimcCli validate`。
- TNT 复制机、流体农场、矿车计算机及依赖生物 AI 的机器暂不在实现范围内。

## 开发约定

源码、依赖、测试、示例和构建产物放在所属子项目内。生成的网表建议输出到 `verimc/build/`，本地模拟器工程可放在 `simulator/runtime/`；这些目录不会进入 Git。正式示例和测试夹具继续纳入版本管理，依赖锁文件应一并提交。

沟通和设计文档默认使用中文，代码标识符使用英文驼峰命名。每次有意义且已验证的更新独立提交，完整提交说明不超过 20 个字符。详细约定见 [AGENTS.md](AGENTS.md)。
