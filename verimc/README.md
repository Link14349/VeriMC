# VeriMC 语言

VeriMC 的硬件描述语言与编译工具子项目，面向 Minecraft Java Edition 26.2 红石电路。

**当前阶段：已有 C++20 编译前端，可将纯逻辑 VeriMC 源码编译为 `.vmcl` 逻辑网表。** 红石组件映射、已验证器件库和自动布局布线仍待实现。

- [编译器设计方案](docs/compilerDesign.md)：分层、ANTLR4 选择、实现边界和后端路线。
- [共享逻辑 IR](docs/logicIr.md)：独立于文件格式的内存模型与 C++ 接口。
- [.vmcl 格式](docs/vmclFormat.md)：可独立读取的逻辑中间体。
- [编译器实施与验证](docs/compilerStatus.md)：本次实际覆盖和后续缺口。
- [从零入门教程](docs/languageTutorial.md)：一步步学习连接、comb、参数、寄存器、计数器和测试，附七个完整源文件与练习答案。
- [语言规范 0.1](docs/languageSpec.md)：查询类型、连接、状态语义等精确规则。
- [物理实现规范](docs/physicalSpec.md)：结构组件、布局、时序、组件库和 simulator 边界。
- [测试与诊断规范](docs/testingSpec.md)：逻辑测试、真实 gt 时间线、诊断和验收要求。
- [成套示例](examples/README.md)：7 份核心正例、2 份物理接口审阅例和 13 份反例。
- [机器可读文法](grammar/verimcGrammar.lark)与[设计核查记录](docs/specReview.md)：本轮实际验证范围。
- [已批准的主要设计思路](docs/languageDirection.md)：保留第一轮方案及取舍背景。
- [simulator 总设计](../docs/module-1-design.md)：方块仿真、工程文件与后续模块边界。

本子项目的语言文档、未来源码、器件库、测试、样例和构建配置统一放在 `verimc/`。`simulator/` 继续负责真实方块机制与仿真；Litematic / Create 蓝图导出仍属于后续模块。

## 构建与使用

依赖：CMake 3.25+、C++20 编译器、Java 17+（只在构建时生成解析器）、Boost 1.90+、nlohmann/json 3.12+、OpenSSL 3。CMake 会在 `.cache/` 缓存并校验 ANTLR 4.13.2 生成器与 C++ runtime；也可以提前放入同版本文件供离线构建。运行 `verimcCli` 不需要 Java、Python、Minecraft 或 simulator。

以下命令在仓库根目录运行：

```sh
cmake -S verimc -B verimc/build -DCMAKE_BUILD_TYPE=Release
cmake --build verimc/build -j 8
ctest --test-dir verimc/build --output-on-failure

verimc/build/verimcCli compile verimc/examples/valid/counter.vmc --top Counter -o verimc/build/counter.vmcl
verimc/build/verimcCli validate verimc/build/counter.vmcl
verimc/build/verimcCli compile verimc/examples/valid/adder.vmc --top Adder --param width=8 -o verimc/build/adder8.vmcl
```

省略 `-o` 时输出在源文件旁，沿用文件名主干和 `.vmcl` 扩展名。已有输出须显式 `--force`。顶层模块用 `--top` 指定；默认导入根为输入文件所在目录，需要跨子目录导入时通过 `--root` 指定项目根。源文件中的 test/build 声明只做语法解析，不因 compile 成功而标记测试通过或物理可实现。

`parse input.vmc` 只检查该文件句法并建立 AST；`compile` 检查所选顶层展开后的逻辑；`validate input.vmcl` 不重新读取源文件。错误以 JSON 写到 stderr，退出码非零；成功消息写到 stdout。C++ 接口 `compileFile(CompileOptions)` 返回独立的 `LogicGraph`，`validateLogicGraph(const LogicGraph&)` 直接校验内存模型。`.vmcl` 导入导出通过 `readVmclJson` / `writeVmclJson` 适配器完成，详见 [IR 模型和独立构建](docs/logicIr.md)。

消毒器检查：

```sh
cmake -S verimc -B verimc/buildSanitize -DCMAKE_BUILD_TYPE=Debug -DverimcSanitizers=ON
cmake --build verimc/buildSanitize -j 8
ctest --test-dir verimc/buildSanitize --output-on-failure
```
