# `.vmcl` 逻辑电路中间格式 v1

2026-09-07。扩展名固定为 `.vmcl`，内容为 UTF-8 JSON。格式标识 `verimc.logic`，格式版本整数 `1`，语言版本 `0.1`。文件适配器入口为 `readVmclJson` / `writeVmclJson`，逻辑校验由 `validateLogicGraph(const LogicGraph&)` 完成；CLI `validate` 可独立读取文件，不需要源文件或 Minecraft。

本文件只定义序列化协议；各编译阶段共用的内存结构见 [共享逻辑 IR](logicIr.md)。

## 1. 用途与版本

这是展开后的类型化逻辑图。实例元数据保留层级，运算、寄存器及信号属于同一张图。它包含计算和连接含义，不包含图形编辑器的节点坐标，也不包含 Minecraft 方块、传播延迟或运行快照。

读取者必须先验证格式版本、字段、类型、节点签名和连接。未知操作不能降为常量或空操作。v1 对字段采用封闭集合；未来增加有语义的字段或操作应升级格式版本。重复 JSON 键拒绝；数组顺序有定义。大整数不能用 JSON number 保存。

## 2. 顶层字段

| 字段 | 含义 |
|---|---|
| `format` / `formatVersion` | `verimc.logic` / `1` |
| `languageVersion` / `compilerVersion` | `0.1` / 生成工具版本，目前 `0.1.0` |
| `top` | 包名限定的顶层模块名 |
| `sources` | `{path, sha256, byteLength}` 数组；规范化项目相对路径，不含绝对路径或 `..` 逃逸 |
| `instances` | `{path, module, parameters, source}`；展开后的模块实例，参数值为十进制字符串（bit 为 `0`/`1`） |
| `ports` | `{name, direction, node}`；按顶层源码声明顺序，方向为 input/output |
| `nodes` | 稳定、连续整数 ID 的节点数组 |
| `connections` | signal 的显式位段驱动，见下文 |
| `semantics` | 固定状态与采样协议，见第 5 节 |
| `limits` | 本次编译的资源上限，见第 7 节 |
| `validation` | `{stage:"logicalGraph", physicalVerified:false, sourceTestsExecuted:false}` |

`validation` 是产物类别说明，读取者不能把它当作证明，应重新执行图校验。`sourceTestsExecuted:false` 明确表示源码中的 test 没有被编译命令执行。测试工具的实际运行结果另行记录。

源区间 `source` / `sourceSpan` 的结构统一为 `{file, start, end, line, column}`。file 引用 sources.path；start/end 是 UTF-8 字节偏移，左闭右开；line/column 从 1 开始，column 按 Unicode 标量计数。文件修改后可用 SHA-256 判断映射是否过期。源码哈希不是数字签名。

## 3. 类型与值

标量类型为 `{kind, width}`：

- bit、clock 的 width 为 1；level 为 4，但 level 表示一个 0–15 强度通道。
- bits、uint、int 的 width 为 1–4096。int 使用二进制补码。
- enum 另含 `identity`（包名限定身份）、`members`（声明顺序），width 为成员数量所需的最小位宽，至少 1。类型相等包括身份与成员。
- array 为 `{kind:"array", width:0, length, element}`；length 为 1–65536。element 不能为数组或 clock。总位数为 length × element 的总位数。

nat/integer 仅用于编译期，在图中不可出现。数组按元素序号递增、元素内最低位优先打包；enum/level 保持类型身份。bit、clock、level、enum 的源码接线不可取位；内部数组提取也必须保留元素类型。

常量的 `value` 和寄存器 `resetValue` 是规范十进制字符串：无前导零，零为 `"0"`，不接受 `"-0"`。int 用有符号数学值，其余标量用非负值；array 用整个数组的非负打包比特值，其中有符号元素按补码编码。enum 值必须是已声明成员编码，array 内枚举元素也要有效。

## 4. 节点与连接

每个节点恰好包含：

```json
{
  "id": 0,
  "op": "input",
  "type": {"kind": "uint", "width": 4},
  "inputs": [],
  "attributes": {"name": "Adder.a", "role": "input"},
  "source": {"file": "adder.vmc", "start": 106, "end": 127, "line": 6, "column": 5},
  "instance": "Adder"
}
```

以上只演示字段形状；真实范围、哈希和 ID 以编译产物为准。

id 等于数组下标。inputs 中的整数引用其它节点，顺序由操作定义。节点顺序是确定的构建顺序，允许前向引用，不保证拓扑排序。name 是保留的命名锚点，instance 是展开路径，可包括 generate 下标。生成分组不是独立硬件模块，因此节点的 instance 也可指向 instances.path 下的生成作用域。

`input` 是顶层外部输入；`signal` 是 wire、顶层输出或子模块端口的命名锚点；`register` 是状态边界。signal 从 connections 获得值，不能把“还没连接”解释成零。

每条连接恰好包含：

```json
{
  "target": 2,
  "targetOffset": 0,
  "source": 4,
  "sourceOffset": 0,
  "width": 4,
  "type": {"kind": "uint", "width": 4},
  "sourceSpan": {"file": "adder.vmc", "start": 200, "end": 224, "line": 10, "column": 5}
}
```

含义是将 source 的值接到 target 的指定范围。v1 的 sourceOffset 固定为 0，source 必须是完整、同类型、同位数的值；源取位通过 slice 节点表达。target 只能是 signal。目标范围必须能解释为 type 对应的合法完整信号、数组元素、位或位段，不能隐式改变符号或破坏 enum/level/clock。

每个 signal 的每一位恰有一个驱动；不同连接可以覆盖互不重叠的范围。连接数组的先后不表示传播顺序。组合环校验按位追踪依赖，寄存器输出打断组合依赖；逐位进位链不会因共享一个总线锚点被误判成环。

## 5. 操作与寄存器

除下表列明外，attributes 必须是空对象。所有操作的位宽、符号、合法类型遵循语言规范。

| op | inputs 顺序 | attributes / 含义 |
|---|---|---|
| input、signal | 空 | name、role；role 为 input/output/wire |
| constant | 空 | value |
| register | next、clock、reset | name、role=`register`、resetValue、initialState=`invalid` |
| add、sub | 左、右 | 相同定宽数输入，输出扩一位；sub 输出 int |
| wrapAdd、wrapSub | 左、右 | 输入输出同一数值类型，模 2^N 回绕 |
| neg | 输入 | 数值输入，输出 int<N+1> |
| and、or、xor、bitNot | 左、右 / 输入 | 按位操作，保持类型 |
| logicalAnd、logicalOr、logicalNot | 左、右 / 输入 | bit；按左侧值短路有效性 |
| eq、ne、lt、le、gt、ge | 左、右 | bit 结果；关系比较按数值符号；enum 仅相等比较 |
| mux | 条件、真分支、假分支 | 条件 bit，分支同类型；禁止 level/clock；只读取选中分支的有效性 |
| slice | 输入 | offset；提取指定范围，保持合法位段/数组元素类型 |
| reinterpret | 输入 | 保持全部比特，显式改变数字/位向量类型解释 |
| widen | 输入 | uint 补零、int 符号扩展，目标宽度在 type 中 |
| low | 输入 | 低 N 位，输出 bits<N> |
| shiftLeft、shiftRight | 输入 | amount；编译期非负整数，超过输入宽度时规范化为宽度；右移 int 符号扩展 |
| concat | 高位部分到低位部分 | bit/bits 拼接，输出 bits |
| array | 元素 0、1、2… | 构造保留类型的数组 |

bitwise not 和算术结果必须按输出类型归一化，不依赖宿主整数溢出或超宽移位。

semantics 固定为：

```json
{
  "clockEdge": "rising",
  "reset": "synchronousHigh",
  "stateUpdate": "simultaneous",
  "initialRegisters": "invalid"
}
```

所有寄存器的 clock 沿 signal 别名最终指向同一个顶层 clock input。寄存器输出表示旧状态；一次采样使用同一份旧状态计算所有 next，再共同提交。reset 为高优先选 resetValue，此时不读取 next；未复位的旧状态无效，不能隐式置零。状态图中的保持通常是 mux 选回该 register 的输出。消费者必须保留选中路径的有效性，不能先求所有输入再执行 mux。

图中的同步关系不表示 Minecraft 方块在同一瞬间更新；物理后端仍要实现并验证采样窗口。

## 6. 确定性与读取边界

规范生成器使用两个空格缩进、LF 换行、末尾换行；对象键按字典序序列化，节点和端口按稳定构建顺序。sources 按路径排序。不同文件根目录下的相同相对源码、顶层、参数、预算与工具版本产生相同内容。

读取器验证格式、封闭字段、类型、操作签名、源范围、常量、连接、驱动、组合环、顶层输入及唯一时钟。读取时不信任 producer 的 validation 标记。它能检查结构一致性，不能在没有源文件的情况下证明逻辑与源码等价，也不能证明物理可实现。

## 7. 预算

limits 记录可配置的 maxNodes、maxBits、maxInstances、maxSteps、maxDepth、maxSourceBytes，以及固定 maxWidth=4096、maxArrayLength=65536、maxIntegerBits=65536。默认分别为 100000 节点、1000000 总节点位、10000 实例、1000000 展开步、128 层、全部源文件共 4 MiB。

解析器另有固定 200000 token 上限。maxDepth 同时用于语法嵌套、单表达式运算符数量、每个模块/函数的分支数量等保守解析预算；这些限制会明确拒绝过大的输入，不截断源码。按位图检查最多建立 20000000 条依赖边。读取 CLI 最多接受 256 MiB 的 JSON 文件、128 层 JSON 容器；读取器还有独立的 1000000 节点和 10000000 位硬上限。

预算超限报 EElaborationLimit。失败时不发布新产物，不覆盖上次成功产物。普通输出采用排他发布；显式 `--force` 才替换已有文件。
