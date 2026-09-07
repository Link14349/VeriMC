# 共享逻辑 IR

2026-09-07。内存模型入口为 [logicGraph.hpp](../include/verimc/logicGraph.hpp)。前端与逻辑校验器使用同一套 C++20 类型；后续优化、逻辑执行与组件映射可直接接收 `LogicGraph`。这些后续阶段尚未实现。

## 数据流与依赖

```text
.vmc → ANTLR → AST → 展开、类型检查 → LogicGraph → 后续逻辑处理阶段
                                        ↕
                                .vmcl JSON 读写适配器
```

文件输出是可选分支，阶段之间不必经过文件或 JSON 对象。

| CMake 目标 | 职责与依赖 |
| --- | --- |
| `verimcIr` | 模型、诊断、类型工具、逻辑校验；只依赖 C++ 标准库和 Boost 大整数 |
| `verimcCore` | 源码解析与编译；公开接口依赖 `verimcIr`，不链接 `verimcJson` |
| `verimcJson` | `.vmcl` v1 导入导出、诊断 JSON 输出；依赖 `verimcIr`，私有使用 nlohmann/json |
| `verimcCli` | 组合前端与文件适配器，处理命令行和文件发布 |

所有公开头文件都不包含 JSON 或 ANTLR 类型。前端内部仍使用 nlohmann/json 做语言字符串解码与 UTF-8 检查，这是解析器的私有依赖，不承载 IR。`verimcIr` 不依赖 AST、解析器、文件适配器、OpenSSL 或 Java。

## 数据模型

`LogicGraph` 拥有以下内容，各阶段直接读写这些字段：

| 类型/字段 | 内容 |
| --- | --- |
| `LogicNode` / `nodes` | 操作种类、输出类型、有序输入 ID、类型化属性、源码区间、展开路径 |
| `LogicConnection` / `connections` | 源/目标节点 ID、两端位偏移、位数、连接视图类型、源码区间 |
| `LogicPort` / `ports` | 顶层名称、`PortDirection`、节点 ID |
| `LogicType` | `TypeKind`、位宽；数组的长度和只读元素类型；枚举的名义身份和成员 |
| `LogicInstance` / `instances` | 模块实例路径、模块名、参数数值、源码区间 |
| `LogicSource` / `sources` | 相对路径、SHA-256、源码字节长度 |
| `GraphLimits` / `limits` | 编译与校验资源预算；`CompileOptions` 复用这组字段 |
| `top`、语言/编译器版本 | 顶层身份与编译来源信息 |

操作采用 `NodeKind` 枚举；节点属性采用 `std::variant`，不存在通用键值 JSON 属性袋：

- `SignalAttributes`：名称、`SignalRole`，用于输入与信号锚点。
- `RegisterAttributes`：名称、复位值。
- `ConstantAttributes`：常量数值。
- `SliceAttributes`：位偏移；`ShiftAttributes`：移位量。
- 其它运算为 `std::monostate`，不携带额外属性。

常量、复位值和模块参数在内存中均为 `Integer`（`boost::multiprecision::cpp_int`），可直接做数学运算；仅 JSON 适配器将其转换为十进制字符串。`Nat`/`Integer` 类型标签供前端常量分析复用，最终图中的节点和连接不能包含这两类编译期类型。

## 引用、所有权和语义

`NodeId` 为 32 位有符号索引，合法值对应 `nodes` 下标；`invalidNodeId = -1` 只用于构建期尚未物化的值，完成图不允许悬空或负引用。追加节点保留旧 ID；删除、压缩、重新排序节点必须同时重映射输入、连接与端口。节点不保存指向其它节点的指针，也不持有 AST/ANTLR 对象，因此图可以在编译器析构后独立使用。

复制图会复制可变容器、节点、属性和大整数。数组元素类型用 `shared_ptr<const LogicType>` 共享只读描述；修改数组类型时替换元素描述，不能通过一个图修改另一个图的元素。当前支持一维数组；校验器拒绝空元素指针、嵌套数组及非法布局。

`inputs` 是运算的数据依赖；`connections` 是信号锚点的显式驱动。位偏移从最低位开始，数组从第 0 个元素开始打包。当前连接源必须是完整值，目标可为合法分片；节点顺序是构建顺序，允许前向引用，不保证拓扑顺序。

寄存器输入依次为下一值、时钟、复位，可以用 `registerInput(RegisterInput::Next/Clock/Reset)` 访问。输出代表当前状态。上升沿、同步高有效复位、所有寄存器同时提交、未复位时无效，是当前逻辑 IR 的固定语义；图中没有运行时寄存器状态。组合环检查以位为单位，并在寄存器输出处切断组合依赖。

源码来源仍是当前图的校验约束：节点、连接和实例必须引用存在的源文件以及合法字节区间。后续变换需保留可追溯来源。节点的 `instance` 是展开路径，允许包含 `generate` 作用域，不要求每条路径单独出现在模块实例表中。

## C++ 使用

```cpp
#include "verimc/compiler.hpp"
#include "verimc/vmclJson.hpp" // 仅需要文件导入导出时包含

verimc::CompileOptions options;
options.input = "counter.vmc";
options.top = "Counter";
verimc::LogicGraph graph = verimc::compileFile(options);

// 其它阶段可直接遍历/修改 graph.nodes、graph.connections 等。
verimc::validateLogicGraph(graph);

std::string bytes = verimc::writeVmclJson(graph);
verimc::LogicGraph restored = verimc::readVmclJson(bytes);
```

`compileFile` 返回已经通过逻辑校验的 IR。`validateLogicGraph(const LogicGraph&)` 直接检查类型、操作签名、属性种类、引用、驱动、按位组合环、端口、时钟、来源和预算；内部不序列化。

`readVmclJson` 检查文件版本、封闭字段集合、JSON 数值表示、重复键和嵌套预算，构造 IR 后调用同一校验器；`writeVmclJson` 也先检查调用者交来的图。JSON 格式标识、格式版本、字段拼写、十进制字符串编码和文件验证声明均在适配器中维护，模型没有 `json()` 方法。以后增加二进制适配器不需要改变各阶段使用的数据结构。

`.vmcl` 仍保持 v1 兼容。C++ API 有意调整：旧的 JSON 返回值和 `validateLogicGraph(json)` 已替换为上述 IR 接口。

## 独立构建与验证

仅构建和测试 IR，不查找 JSON、ANTLR、Java 或 OpenSSL：

```sh
cmake -S verimc -B verimc/buildIrOnly -DCMAKE_BUILD_TYPE=Release \
  -DverimcBuildCompiler=OFF -DverimcBuildJson=OFF
cmake --build verimc/buildIrOnly -j 4
ctest --test-dir verimc/buildIrOnly --output-on-failure
```

完整构建另有前端 API 测试（只链接 `verimcCore`）、JSON 适配器往返测试和原有端到端行为测试。`tests/fixtures/legacyCounter.vmcl` 是重构前编译器生成的 v1 兼容性夹具；不随新序列化器自动重新生成。它用于验证旧文件导入 IR 再导出后保持相同字节，无需原始源码。
