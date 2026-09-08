# 物理组件装配工具：支持子集与限制

日期：2026-09-08。实现：`tools/physicalCells.py`，只用 Python 3 标准库（hashlib、json、pathlib、re）。
测试：`tests/physicalCellsTests.py`。

本文件只记录组件读取/变换模块。跨组件装配、受限连接检查和实际 C++ 仿真由其它模块完成，见[闭环实施记录](physicalClosureStatus.md)。

本工具是[物理规范](physicalSpec.md)第 2、3、4 节的**独立研究与验证入口**，用于在实现物理后端之前
把“清单读取 + 固定放置”这段规则单独写清楚、单独测试。它**不是**已有 C++ 编译器的一部分：

- C++ 前端当前只支持源码到逻辑图，`build` 与器件映射仍未实现，见[编译器实施记录](compilerStatus.md)。
  本工具的存在**不表示** `build`、自动布线或蓝图导出已经可用。
- 本工具不执行仿真。任何红石行为、时序、电气正确性都必须由 C++ 仿真核心负责，本工具一次都没有运行过。
- 本工具不读方块注册表、不读 `*.target.json`、不读 `*.vmlib.json`。方块状态最终是否被目标注册表接受，
  由 C++ 判定；本工具只在自己声明的保守子集内做结构检查。
- 本工具不执行清单引用的任何代码，也不下载任何东西：全部行为是读文件、解析 JSON、算哈希、算整数坐标。

## 1. 公开 API

```python
from physicalCells import PhysicalError, loadCell, placeCell
```

### `class PhysicalError(ValueError)`

带 `code` 与 `detail` 属性；`str(error)` 形如 `"ECapability: ..."`，即诊断码就是消息前缀。

| code | 含义 |
|---|---|
| `EPath` | 引用是绝对路径、盘符路径、网络地址、含反斜杠或控制字符，或解析后（含符号链接、目录联接）逃出项目根 |
| `EResourceMissing` | 引用的文件不存在或不是普通文件 |
| `EFormat` | JSON 结构、类型、取值范围、排序、唯一性或覆盖关系不符合规范 |
| `EArtifactVersion` | `formatVersion` 不是本工具支持的版本 |
| `ETarget` | 清单 `targetId` 与调用方给出的构建目标不一致 |
| `ECapability` | 规范允许、但本工具没有能力核验或实现的特性；一律拒绝，不猜测 |
| `EPlacement` | 朝向不在允许集合、原点不在支持域、keepout 与自身方块重叠，或变换后几何不自洽 |
| `EInitialization` | 初始化计划与方块清单的覆盖关系不成立 |

### `loadCell(manifestPath, projectRoot, targetId, *, supportedUpdateKinds=None, providedCapabilities=None)`

返回 `{manifest, blocks, blockEntities, initialization, assets}`：

- `manifest`：原始 JSON 对象，字段原样保留（`model` 为 null 时保持 null）。
- `blocks` / `blockEntities`：引用文件解析出的数组。
- `initialization`：初始化计划对象（组件局部坐标）。
- `assets`：**相对于 projectRoot 的 POSIX 路径 → SHA-256 十六进制串**，覆盖清单本身与全部被引用文件
  （方块数组、方块实体、初始化计划、characterization、model 源码）。

两个关键字参数用于避免猜测目标配置，二者默认都表示“调用方没有提供任何东西”：

- `supportedUpdateKinds`：目标配置声明的受支持 `update` 类型集合。默认 `None` 时，任何 `update` 步骤都报
  `ECapability`——规范规定 kind 来自目标配置，本工具不读目标文件，因此不自行认定任何 kind 合法。
- `providedCapabilities`：目标声明已提供的能力键集合。默认 `None` 时，清单的 `requiredCapabilities`
  必须为空，否则报 `ECapability`。

### `placeCell(cell, instanceId, origin, facing, *, worldYRange=None)`

返回 `{blocks, pins, bounds, keepouts, initialization, instanceId}`，坐标均为构建根坐标系下的世界坐标：

- `blocks`：变换后的方块数组，按 x、y、z 重新规范化排序，属性键排序。
- `pins`：变换后的引脚（`pos` 与 `face` 已变换，`port`/`leaf`/`channel`/`encoding`/`maxLoads` 不变）。
- `bounds`：变换后的半开包围盒 `{min, max}`。
- `keepouts`：变换后的半开盒数组。
- `initialization`：坐标已变换的初始化计划，另加 `instanceId` 字段记录实例来源。

`worldYRange` 是可选的 `(最小含, 最大不含)` 世界 y 界。默认 `None` 表示**本工具不知道目标世界高度**，
因此不做该项检查，由 C++ 目标适配器负责；给出时会检查方块、bounds 与 keepout 的 y 区间。
`placeCell` 不修改传入的 `cell`，也会重新校验它，防止调用方传入被改坏的对象。

## 2. 实际支持的子集

### 2.1 组件清单

- `format` 固定 `verimc.cell`、`formatVersion` 固定 `1`；初始化计划为 `verimc.initPlan` / `1`。
- 清单文件名必须以 `.vmcell.json` 结尾，且解析后位于 `projectRoot` 内。
- 规范表格列出的 18 个必需字段逐个检查；`annotations` 可选且内容被忽略；其余未知字段报错。
- JSON 严格性：重复键、UTF-8 BOM、非 UTF-8 字节、`NaN`/`Infinity`、超深嵌套一律拒绝；
  布尔不当作整数（`[false, 0, 0]` 不是坐标），浮点不当作整数。
- 全部半开盒要求每个轴 `min < max`；`bounds` 必须包含所有方块与所有引脚位置。
- keepout 与组件自身方块重叠报 `EPlacement`（keepout 是保留的空气/运动/隔离区）。

### 2.2 端口与引脚

- **只支持标量 `bit` 端口**。`uint<n>`、`level`、`clock`、枚举、数组一律报 `ECapability`。
  因此 `leaf` 必须是空数组，多叶端口、位到引脚的展平顺序与 stride 都不在本轮范围内。
- **只支持 `digital` 编码**。`{kind: "strength"}` 与 `{kind: "clock"}` 报 `ECapability`；
  未知 kind 报 `EFormat`。`low`/`high` 必须是 0–15 内有序、互不重叠的闭区间。
- 引脚必须恰好覆盖每个端口叶子一次；同一 `(pos, face)` 不能被两个引脚占用。
- 引脚位置必须落在 `bounds` 内**且**对应一个已声明的方块。
- 输入 `channel` 只能是 `receivePower`，输出只能是 `weakOutput`/`strongOutput`；
  输入 `maxLoads` 必须是 null，输出必须是非负整数。

### 2.3 方块子集

只接受五种方块，其余报 `ECapability`：

| 方块 | 属性与旋转语义 |
|---|---|
| `minecraft:stone` | 无属性 |
| `minecraft:redstone_wire` | `north`/`east`/`south`/`west`（none/side/up，**属性名随旋转改名**）、`power`（0–15，不变） |
| `minecraft:repeater` | `facing`（水平四向，**取值随旋转**）、`delay`（1–4）、`locked`、`powered` |
| `minecraft:lever` | `facing`（水平四向，**取值随旋转**）、`face`（floor/wall/ceiling，不变）、`powered` |
| `minecraft:redstone_lamp` | `lit` |

规则与限制：

- 该表**未依据 Minecraft 26.2 注册表核验**，只是本工具声明的保守子集。注册表接受与否最终由 C++ 负责。
- 方块状态属性值必须是非空字符串；表内属性必须全部写出，不用缺失值推断默认状态。
- 表外属性名或表外取值**不会被静默丢弃**：原样保留，但该方块被标记为旋转语义未知，
  只允许 `north`（恒等）放置，其它朝向报 `ECapability`。
- 方块数组必须按 x、y、z 字典序规范化、属性键排序、坐标唯一；重复坐标与未排序分别有独立诊断。

### 2.4 变换

- `north` 为恒等；`east` 为 `(x,y,z) -> (-z,y,x)`，`south`、`west` 依次复合，即绕局部 y 轴俯视顺时针
  90/180/270 度。变换顺序是绕局部 (0,0,0) 旋转后再平移到 origin。
- 方向随之旋转：`north→east→south→west→north`（按 east 一步）；`up`/`down` 不随水平旋转改变。
  引脚 `face`、`facing` 取值、红石粉四向属性名都用同一张方向映射表。
- **半开盒不直接旋转 max 点**：先把 `[min, max)` 还原成整数占用格区间 `min .. max-1`，旋转上下两角，
  再取分量最小/最大并加 1 复原为半开盒。测试用穷举占用格集合验证变换结果不多不少。
- `placementDomain`：`origin` 必须落在 `[originMin, originMax)`（含 y 分量）内，且
  `(floorMod(x,16), floorMod(z,16))` 必须出现在 `chunkResidues` 中。取模是数学 floor 模，负坐标结果非负
  （`-1 → 15`、`-16 → 0`、`-17 → 15`）。空 `chunkResidues` 表示没有可用位置，不表示全部。
- 只允许 `transforms` 中列出的朝向；`transforms` 只能含 north/east/south/west，镜像与俯仰报 `ECapability`。

### 2.5 初始化

- 只支持 `orderedPlacement`，且 `place` 步骤必须**恰好覆盖每个方块一次**（缺失与重复都有独立诊断）。
  `bulkThenUpdates` 报 `ECapability`，因为本工具没有可验证的批量装入语义。
- `wait` 的 `ticks` 必须是非负整数 gt；本工具只做结构检查，推进由 simulator 负责。
- `update` 默认全部报 `ECapability`，除非调用方通过 `supportedUpdateKinds` 显式给出目标支持的类型。
- `externalRequirements` 的 `port` 必须是已声明端口；它只是被记录和转发的条件，不被执行，也不视为已满足。

### 2.6 证据、时序与模型

- `evidence.status` 只接受 `unverified` 且 `cases` 为空。`scenarioVerified` 报 `ECapability`：
  本工具无法核验 `gameBuildSha256`、输入时间线与观测记录，因此**拒绝而不是接受**。
- `timing.arcs`：`from` 必须是输入端口、`to` 必须是输出端口，`minGt` 非负，`maxGt` 可为 null 表示未知上界。
- `timing.settling`：必须覆盖每个输出端口恰好一次，`maxGt` 为 null 表示未知上界。
- `timing.clockChecks` 必须为空数组；非空报 `ECapability`（需要 clock 端口能力）。
- `timing.characterization`：只有在 `arcs` 为空且所有 `settling.maxGt` 都是 null 时才可为 null；
  否则必须提供记录文件，并检查 `profileId`、`conditions`、`caseIds`、`limitations` 四个最小字段、锁定其哈希。
  由于证据只允许 `unverified`，`caseIds` 必须为空。
- **接受这些字段不等于时序已验证**。本工具不做任何时序签核，只检查结构、方向与哈希。
- `model` 为 null 时保留 null，不编造等价逻辑。非 null 时检查结构、参数只能是 JSON 整数或布尔值、
  `sourceSha256` 与源文件实际哈希一致，并把源文件计入 `assets`；
  **不做任何语义或等价性检查**，模块是否为无 cell 的纯逻辑模块由 C++ 编译器判定。

### 2.7 路径安全

引用的绝对路径、盘符路径（`C:/...`）、网络地址（含 `://`）、反斜杠分隔、控制字符、空路径段一律拒绝。
所有路径用 `pathlib.resolve()` 解析（因此会解析符号链接与 Windows 目录联接），解析后必须仍在 `projectRoot` 内，
且必须是普通文件。`projectRoot` 本身必须是已存在的目录。

## 3. 未实现（不要当作已支持）

- 器件映射、逻辑展开、自动布局布线、连通性/电气隔离检查、成本与体积目标。
- 目标配置 `*.target.json`、库索引 `*.vmlib.json`、`mappingRules`、`routingRules`、`checkerId` 验证器。
- `verimc.lock.json` 构建产物、CircuitDesign / interfaceMap / sourceMap / implementationReport 输出包。
- 仿真、红石行为、时序验证、Minecraft 26.2 差分验证、Litematic 与 Create 导出。
- 多叶端口与总线引脚的 stride 展平；`strength`/`clock` 编码；clock 端口与时钟检查。
- 方块实体（NBT）：`blockEntities` 文件必须是空数组，非空按规范拒绝该组件，因为本工具没有无损 NBT 能力。
- 世界高度与目标合法坐标范围：除非调用方传入 `worldYRange`，否则不检查。
- 跨组件的重叠、keepout 冲突与初始化交错：`placeCell` 只处理单个实例，组件之间的相互作用未实现。

## 4. 测试

```sh
python verimc/tests/physicalCellsTests.py
```

`unittest`，131 个测试方法，每个用例使用自己的临时目录作为项目根，不读写仓库内文件。
在 Windows 11 + Python 3.13.5 上的实际结果：**运行 131 个，130 个通过，1 个跳过，0 个失败**
（当前账户没有创建符号链接的权限，文件级符号链接用例被跳过；同一条逃逸路径由目录联接用例覆盖并通过）。
分组：

| 分组 | 数量 | 覆盖 |
|---|---|---|
| `LoadValidCellTests` | 8 | 正常加载、assets 覆盖与哈希一致、引用文件变化时哈希变化、可重复性、model 锁定与哈希不一致 |
| `JsonStrictnessTests` | 15 | JSON 语法错误、清单与引用文件的重复键、未知字段、缺失字段、BOM、NaN、非 UTF-8、布尔/浮点坐标、版本 |
| `PathSafetyTests` | 12 | `..` 逃逸、绝对路径、盘符、网络地址、反斜杠、缺文件、目录引用、符号链接与目录联接逃逸、清单在根外、文件名后缀 |
| `BlockValidationTests` | 12 | 重复坐标、未排序方块与属性键、越界方块、未支持方块、非法方块名、缺属性、非字符串属性值、空数组、keepout 重叠、空盒、非空方块实体 |
| `PortAndPinTests` | 17 | 非 bit 类型、重复端口、叶子未覆盖/重复、非空叶子、通道方向、maxLoads、引脚落点与面、strength/clock 编码、区间重叠与越界 |
| `TargetAndCapabilityTests` | 5 | targetId 不一致、requiredCapabilities 未提供/已提供、镜像与重复 transforms |
| `EvidenceAndTimingTests` | 12 | scenarioVerified 拒绝、unverified 必须空 cases、未知状态、clockChecks、settling 覆盖、arc 方向与界、characterization 必需性与字段 |
| `InitializationTests` | 11 | place 缺失/重复/指向未知方块、bulkThenUpdates、未知策略、update 拒绝与显式声明后接受、未知 op、负 wait、外部需求端口、计划版本 |
| `PlacementDomainTests` | 10 | 余数越界/负值/重复、原点范围颠倒、维度格式、floor 取模、负坐标区块余数接受与拒绝、半开原点范围、空余数集 |
| `PlacementTransformTests` | 19 | 四个朝向的坐标与半开 bounds、keepout 变换、facing 与红石粉属性旋转、垂直属性不变、引脚位置与面、初始化坐标变换、不修改入参、朝向与原点参数校验、`worldYRange` 越界 |
| `UnknownPropertyTests` | 3 | 未知属性不被丢弃、未知属性与未知取值禁止旋转 |
| `RotationMathTests` | 7 | 与规范一致的坐标变换、四次旋转回到恒等、方向映射与坐标变换自洽、半开盒变换等于占用格集合的像、变换是单射 |

半开盒变换用穷举占用格集合与变换结果逐格比对，能直接排除“旋转 max 点”这一常见错误：
基准组件 `[0,0,0)..[2,2,2)` 在 `east` 下正确结果是 `[-1,0,0)..[1,2,2)`，
旋转 max 点会得到 `[-2,0,0)..[0,2,2)` 并把 x=0 的方块排除在外。

未做的验证：没有跑过 Minecraft、没有跑过 simulator、没有做红石行为或时序差分、没有性能声明、
非 Windows 平台与实际 C++ 集成的最终验证结果另见[闭环实施记录](physicalClosureStatus.md)。
