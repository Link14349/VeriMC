# VeriMC 0.1：物理组件与实现规范

状态：语言设计草案的规范组成部分；接口、库与路由能力尚未实现。目标固定为 Minecraft Java Edition 26.2，规则依据和支持范围沿用 simulator 的版本证据，不在语言内重写游戏机制。

## 1. 结构组件怎样进入语言

`cell` 声明一个具有固定端口、固定尺寸的物理组件类型：

```text
cell Buffer4 {
    input dataIn: uint<4>;
    output dataOut: uint<4>;
    source "cells/buffer4.vmcell.json";
}
```

source 必须恰好一个；路径相对于本文件解析并受项目/依赖根限制。此例说明语法，不表示已经提供或验证 Buffer4 的红石实现。cell 端口必须与清单完全一致，包括名称、方向、逻辑类型、位序和时钟协议。

cell 不接受参数、行为语句、内部实例或在线下载地址。需要参数化时，用 module 与 generate 实例化固定组件。一个 module 可以同时包含逻辑表达式和 cell 实例，结构设计因此沿用同一模块及连接规则。

组件有逻辑模型时可在模型适用条件内参加逻辑验证；没有模型时，其输出是“无模型”，不能为假或零。结构组件内的反馈和运动依赖真实方块执行，不按组合依赖图拆成逻辑环。外围不得由此绕过未建模反馈诊断：缺少模型的完整设计不能通过逻辑测试。

cell 的内部方块受保护。默认只允许库明示的刚性变换和端口接线；合并、删线、布尔优化、器件替换、自动修复内部结构均不允许。改变内部设计必须创建新的组件内容版本并重做验证。

## 2. 组件清单与文件

组件清单采用 UTF-8 JSON，文件名 `name.vmcell.json`，重复 JSON 键报错。除专用 `annotations` 对象外，未知字段报错。路径引用的资产与清单一起计算内容哈希，不允许清单读取文件以外的代码或命令。

以下表格定义 v1 必需字段；可空项必须显式写 null，不使用缺失值推断行为。

| 字段 | 类型与规则 |
|---|---|
| `format` / `formatVersion` | 固定 `verimc.cell` / `1` |
| `id` / `version` | 非空组件键与精确版本字符串；库中组合唯一 |
| `targetId` | 必须匹配构建目标配置 id |
| `ports` | 端口数组；每项含 `name`、`direction`（input/output）、`type`（规范类型文本） |
| `pins` | 物理引脚数组，见第 3 节；覆盖全部端口叶子 |
| `blocks` / `blockEntities` | 指向规范化方块数组与实体数据数组文件的相对路径 |
| `bounds` | `{min: [x,y,z], max: [x,y,z]}`，整数半开包围盒，原点为局部 (0,0,0) |
| `keepouts` | 相同半开盒数组；保留的空气/运动/隔离区，不自动生成方块 |
| `transforms` | 允许的朝向数组，只含 north/east/south/west；0.1 无镜像和俯仰 |
| `placementDomain` | 适用位置集合描述，见下文；不能写笼统“任意位置”代替证据 |
| `initialization` | 指向初始化计划文件的路径；即使为空计划也必须提供 |
| `model` | null，或 `{source, module, parameters, sourceSha256}`；引用无 cell 的纯逻辑模块 |
| `timing` | `{arcs, clockChecks, settling, characterization}`，见第 5 节 |
| `requiredCapabilities` | simulator 必需能力键数组，按行为而非仅方块 ID 标识 |
| `evidence` | `{status, cases}`；status 为 unverified/scenarioVerified，cases 为证据记录路径数组 |
| `annotations` | 非必需说明对象；不能携带影响运行的隐藏字段 |

model 的参数对象值只能是 JSON 整数或布尔值。模型端口与 cell 声明同名同类型；cell 的时序与初始条件限制仍需检查。模型可以描述组合功能或语言核心支持的同步状态；不能调用其他物理 cell 来循环证明自身正确。清单中的类型使用展开后的具体位宽/长度；枚举使用包名限定的类型名并随模型源哈希确认身份，不存源文件局部导入别名。

`placementDomain` 固定字段为 `originMin`、`originMax`（半开允许原点范围）、`chunkResidues`（允许的 x/z 对 16 的非负余数对数组）、`dimension`（维度 ID）。空 residue 数组表示没有可用位置，不能表示全部。负坐标余数使用数学 floor 模运算。方向由 transforms 限制；坐标范围与余数组合仍只描述支持域，不自动证明域内每个点等价。

方块数组每项为 `{pos: [x,y,z], name: "minecraft:...", properties: {...}}`，按 x、y、z 字典序规范化，属性键排序，坐标唯一，所有属性必须被目标注册表接受。方块实体数组每项为 `{pos, dataVersion, data}`；data 使用 simulator 约定的无损 NBT 类型表示。适配器没有这种无损能力时必须拒绝相关组件，不把暂用 JSON 当作通用 NBT。

运行派生属性与设计初始化的关系由初始化计划声明，不能只依靠保存一份 powered 状态。组件文件是静态设计，不能内含待执行事件队列、随机源状态或移动中的快照。

## 3. 端口的电气含义

每个引脚项固定包含 `port`、`leaf`、`pos`、`face`、`channel`、`encoding`、`maxLoads`。

- `port` 对应声明端口；`leaf` 是由数组下标和位下标组成的整数数组。标量 bit/level/clock 用 `[]`，向量的第 i 位用 `[i]`，数组第 j 项的第 i 位用 `[j,i]`。枚举按规范编码拆位。
- `pos` 是组件局部方块坐标，`face` 为六向之一。所有方向使用目标游戏坐标语义。
- 输入 `channel` 只能是 `receivePower`；输出为 `weakOutput` 或 `strongOutput`。更复杂的物品或事件接口不在 0.1 的逻辑端口中伪装成电平。
- bit 和数字总线引脚的 `encoding` 为 `{kind: "digital", low: [a,b], high: [c,d]}`。范围闭区间、互不重叠且位于 0–15；落在两区间之外是无效电平。低高范围必须由组件证据支持，不一律假设所有组件都接受相同阈值。
- level 使用 `{kind: "strength"}`，保留完整 0–15；clock 使用 `{kind: "clock", low: [...], high: [...]}`，电平解释与时钟检查一起确定有效边沿。
- `maxLoads` 为输出支持的非负负载数，输入填 null。负载数是该库所定义兼容接入器件的计数，不是通用电气扇出保证；其它负载组合须重新表征。

逻辑一条总线在物理层是有顺序的引脚集合；不能将 uint<4> 自动编码为一根强度线。路由中新增中继、检测或编码器件都应显式进入器件图、方块清单与成本/时序报告。

## 4. 初始化与外部接口

初始化计划文件格式为 `{format: "verimc.initPlan", formatVersion: 1, strategy, steps, externalRequirements}`。strategy 必须为 `orderedPlacement` 或 `bulkThenUpdates`，只有目标适配器明确支持并验证该策略时才可执行。

steps 是有序数组，操作仅允许：

| `op` | 其它字段 | 含义 |
|---|---|---|
| `place` | `pos` | 按 blocks 文件中的设计状态放置该方块，执行目标的放置更新 |
| `update` | `pos`、`kind` | 执行目标配置列出的受支持更新类型；未知 kind 报错 |
| `wait` | `ticks` | 非负整数 gt，由 simulator 推进且保留全部事件 |

orderedPlacement 必须恰好覆盖全部方块一次；bulkThenUpdates 先按目标策略装入完整静态结构，steps 不得含 place。不同组件计划不得随意交错。构建产物记录稳定的实例顺序与每个操作的展开来源；若实例间初始化会互相影响，最终整电路测试必须覆盖该顺序。

externalRequirements 为 `{port, purpose}` 数组，purpose 是人可读说明，例如外部时钟源或复位流程。它不执行动作，也不能当作已经满足。测试输入以单独时间线提供；可实装设计保留外部接口条件，并在导出前报告它们。

若初始化需要上述操作集无法表达的玩家交互、实体或随机状态，0.1 报不支持。不能让组件资产运行任意脚本，也不能把精确快照硬塞成可实装设计。

## 5. 时序约定与证据

timing 中全部时间用非负整数 gt；刻内顺序依赖用证据条件描述，不用小数 gt 或纳秒模拟。未知上界使用 null，并使相应时序检查无法通过。

- `arcs` 项为 `{from, to, minGt, maxGt}`，from/to 是端口名，表示约定输入转换到输出转换的延迟范围；有条件或多状态差异时必须在 characterization 中记录分组证据，取覆盖支持域的保守界。
- `clockChecks` 项为 `{clock, data, setupGt, holdGt, minHighGt, minLowGt}`。data 可以是输入端口数组，必须覆盖同步模型依赖的输入及复位。边界为闭窗口：采样前 setupGt 至采样后 holdGt 期间数据必须保持，临界同刻更新还需满足 characterization 指定的顺序条件。
- `settling` 项为 `{output, maxGt}`，给出完整接口条件下输出进入有效稳定状态的上界。上界不能只由测试观察到的最大值冒充证明结果；报告证据性质和未覆盖情况。
- `characterization` 指向说明环境、负载、方向/位置、输入转换类、脉宽、同刻顺序、初始化和观测方法的 JSON 记录。其最小字段为 `profileId`、`conditions`（字符串数组）、`caseIds`、`limitations`（字符串数组）。未知或不可机器检查的条件阻止自动时序通过，除非被最终验证配置明确、可追踪地检查。

evidence 记录最小字段：`caseId`、`gameBuildSha256`、`adapterVersion`、`profileId`、`initializationSha256`、`inputTimelineSha256`、`observationsSha256`、`result`（pass/fail）、`coverage`（说明数组）。scenarioVerified 仅表示列出的场景通过；任何失败、缺失或不匹配的记录均不得被忽略。

时序检查同时覆盖数据路径、时钟到达差、最小路径、脉宽和复位；只检查最大组合延迟不足以放行。最终采样窗口内须比较逻辑模型与实际方块值，窗口外的脉冲若会触发下游状态也必须检查。不能把有限场景验证宣称为所有可能历史上的形式化证明。

## 6. 构建配置语法

```text
build Counter4 for Counter(width = 4) {
    target "targets/java26_2.target.json";
    library "cells/main.vmlib.json";
    bounds (0, 0, 0)..(64, 32, 64);
    clock clk { period 80gt; high 40gt; }
    input reset { setup 8gt; hold 4gt; }
    input enable { setup 8gt; hold 4gt; }
    output count { settle 12gt; }
    port count at (0, 1, 0) face west stride (0, 0, 2);
    map value using "demo:register4";
    place value at (8, 1, 8) facing north;
    route auto;
    objective volume;
}
```

这里的尺寸和 80/40/8/4/12gt 都是**示范约束值，不是已验证器件性能**；示例引用的红石库未随本轮交付。未来构建必须检查可实现性，不能仅接受配置就宣称满足。

构建配置只能引用一个完整 module 实例，不能以测试或另一个 build 为目标。这里按顶层是否有 clock 输入区分同步接口与组合接口；即使寄存器在子模块里，也需要同一根时钟约束。构建表达式可使用自身文件可见的常量，不自动把目标模块的局部参数导入名称空间。规则：

| 指令 | 次数与含义 |
|---|---|
| target、library、bounds、route | 各恰好一次；无隐含默认目标/库/无限空间 |
| `clock path { period D; high D; }` | 同步设计恰好一次，对应顶层唯一 clock；组合设计禁止 |
| `input path { setup D; hold D; }` | 同步设计每个非 clock 输入恰好一次；仅顶层完整端口，数组同约束 |
| `output path { settle D; }` | 所有输出各一次；指约定稳定上界，非注入延迟 |
| `port path at coord face direction [stride coord];` | 可选固定顶层端口引脚；其它端口由路由器安排并报告实际位置 |
| `map path using "cellKey";` | 可选强制模块实例或完整 reg 使用库中实现，见下文 |
| `place path at coord facing direction;` | 可选固定实例/寄存器实现组的原点与方向 |
| `objective volume;` / `objective materials;` | 至多一次，默认 volume；只在满足硬约束的方案中选择 |

path 必须在展开后唯一解析；不支持通配选择和穿透受保护 cell。map/place 可引用生成块内的实例；顶层 reg 可以直接用名称。重复、重叠或冲突约束报错，不能以后写覆盖前写。

map 模块实例要求组件模型与实例的展开模型一致；0.1 仅接受可核对的逻辑图结构一致（允许身份重命名），不凭测试通过断言任意两个不同模型等价。map reg 要求组件具有规范寄存器接口 `dataIn`、`dataOut`、`clk`、`reset`，相同数据类型与复位值。模块内部 next 选择在寄存器 dataIn 前实现，不能假定库自动知道 Counter 的逻辑。其它组件自动选择只限库声明并验证的映射规则。

bounds 和各项坐标中的数字均为编译期整数，空间盒采用 min 含、max 不含。构建根原点为游戏世界坐标；组件相对坐标变换后必须仍在 bounds 和目标合法坐标内。north 对应组件原始朝向；east/south/west 分别绕局部 y 轴从俯视顺时针旋转 90/180/270 度，作用在整数方块坐标和引脚面上。变换顺序为绕局部 (0,0,0) 旋转再平移；east 的坐标变换为 `(x,y,z) -> (-z,y,x)`，其余依次复合。

数字总线 port 的 stride 是相邻叶子坐标差，按数组元素递增、元素内最低位优先展平。多叶端口固定落点必须指定非零 stride；标量禁止 stride。位到引脚映射和物理编码必须来自目标接口适配器并被记录。

时长为编译期非负整数后接 gt 或 rt，1rt 规范化为 2gt。周期 P 必须正，0 < H < P；周期测试使用低相 P−H，且输入 setup ≤ P−H、hold ≤ H、输出 settle ≤ H。满足这些不等式只是外部测试窗口可安排，内部时钟/路径验证仍须通过。

## 7. 目标配置与库索引

`*.target.json` 必需字段：`format="verimc.target"`、`formatVersion=1`、`id`、`edition="java"`、`minecraftVersion="26.2"`、`gameBuildSha256`、`referenceManifestSha1`、`registrySha256`、`profileId`、`profile`、`coordinateBounds`、`inputAdapterId`、`requiredCapabilities`。

profile 必须完整引用 simulator 的规则配置，禁止实验规则混入正式目标。gameBuildSha256 是实际参考构建的 SHA-256；现有参考数据若仅有 SHA-1，应在后续接入时对本地已校验资产计算 SHA-256 并保留原 SHA-1，不伪造换算。inputAdapterId 固定测试输入与游戏事件之间的注入位置和观测阶段。

`*.vmlib.json` 必需字段：`format="verimc.library"`、`formatVersion=1`、`id`、`version`、`targetId`、`cells`、`mappingRules`、`routingRules`。cells 项为 `{key, manifest, sha256}`；sha256 指清单文件字节，引用资产另逐项锁定。

mappingRules 项为 `{operation, inputTypes, outputType, cellKey, modelSha256}`。operation 只能是 `not/and/or/xor/add/sub/compare/mux/register/constant`；类型精确、无模式语言。一个操作可以先按规范展开成更小逻辑，不存在映射时必须报错。register 的额外必需字段为 `resetValue`（按类型的规范常量文本）；compare 另含 `predicate`（eq/ne/lt/le/gt/ge）。

routingRules 项为 `{id, sourceEncoding, sinkEncoding, segmentCells, junctionCells, allowedDirections, checkerId, checkerParameters, characterization}`。编码按第 3 节形式；cell 列表均引用本库键。checkerId 指向编译器已实现、版本固定的空间/接线验证器，其参数模式由该验证器定义并记录在锁文件；它只负责已声明的布局规则，不能更改仿真机制。未知验证器、未知参数、无法机器检查的必要条件均报 ECapability，不能仅依据自由文本说明进行自动布线。首个验证器与具体器件库需在物理实现阶段共同验证，本轮不虚构其算法能力。

构建生成 `verimc.lock.json`，记录语言/编译器/规范版本、全部源文件与资产 SHA-256、目标规则、库版本、路由规则版本、搜索种子和预算。这里的锁文件是未来构建产物约定，本轮不生成虚构版本或哈希。

## 8. 放置、路由与失败处理

先做有限逻辑展开与组件映射，再按规则化区域放置及限定路径布线。固定组件、keepout、支撑、引脚接入、强度、方向、隔离空间与运动空间都参与检查。route auto 表示在这些规则内搜索，不表示任意两点都能连通。

跨组件的空间和初始化相互作用必须检查；方块不重叠不等于电气隔离。连接失败、无合适引脚、超出位置支持域、缺少复位步骤或时序不能满足时构建失败，报告网络、源码、实例、约束和已尝试的原因。

允许输出明确标为 incomplete/unverified 的诊断布局供查看，但它不能作为成功设计、验证电路或可实装蓝图交付。无模型、无证据或不支持的器件不能静默替换为空气、无操作或常量。

固定输入、配置、工具/库版本与种子时，规范化输出必须相同；wall-clock 时间预算导致搜索提前结束时，报告中明确搜索未完成，不能声称确定性成功产物。建议以确定的展开/搜索步数预算作为可复现测试基准。

## 9. 输出与 simulator 适配

成功物理构建输出一个设计包，语义上包含：

- CircuitDesign：版本清单、Minecraft 名称/状态/坐标、方块实体及区域原点。
- interfaceMap：顶层端口/位索引到物理位置、面、编码和读写通道。
- sourceMap：源文件哈希、字节区间、实例路径、逻辑资源与方块集合；区间用 UTF-8 字节偏移，左闭右开，同时附 1 起始行列供显示。列按 Unicode 标量计数，制表符计一列，UI 可另做视觉展开。
- initialization：最终展开的有序计划、外部输入需求及目标配置。
- implementationReport：面积/体积/材料、路由、时序要求与实测/推导界、未覆盖条件、库与验证结果。
- lock：本次构建精确依赖与预算。

这些是必需的语义数据，不把 simulator 当前临时 JSON 字段冻结为完整跨项目交换格式。后续适配工作必须定义版本化序列化，并对缺少的字段/能力拒绝接入；不得丢掉初始化历史或源码映射后假装成功。

编译器不保存运行事件队列；运行快照由 simulator 从明确的设计与输入执行后生成。设计与快照格式不可互换。源码文件哈希改变或用户修改生成方块后，相应 sourceMap 标为失效；首版不反向推导任意 HDL。

Litematic / Create 导出属于后续模块。测试探针、虚拟时钟、刺激时间线与验证标签不会自动变成 Minecraft 方块。
