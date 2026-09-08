"""VeriMC 物理组件清单读取与固定放置：独立研究/验证工具，不是编译器后端。

本模块只用 Python 标准库，严格按 docs/physicalSpec.md 校验 `*.vmcell.json` 组件清单，
并把已校验的组件按给定原点与朝向做刚性变换。它**不**编译 VeriMC 源码、不做器件映射、
不做自动布线、不执行仿真、不做时序签核，也不运行清单引用的任何代码。C++ 实现仍是
方块注册表、仿真机制与最终构建的权威；这里的支持子集见 docs/physicalCellsSubset.md。

诊断码（`PhysicalError.code`，同时作为消息前缀）：

- `EPath`            路径为绝对路径、网络地址，或解析后（含符号链接）逃出项目根。
- `EResourceMissing` 引用的文件不存在或不是普通文件。
- `EFormat`          JSON 结构、类型、取值范围、排序、唯一性或覆盖关系不符合规范。
- `EArtifactVersion` format/formatVersion 不是本工具支持的版本。
- `ETarget`          清单 targetId 与调用方给出的构建目标不一致。
- `ECapability`      规范允许但本工具没有能力核验/实现的特性，一律拒绝而不猜测。
- `EPlacement`       朝向不在允许集合、原点不在支持域，或变换后的几何不自洽。
- `EInitialization`  初始化计划与方块清单的覆盖关系不成立。
"""

import hashlib
import json
from pathlib import Path
import re

formatName = "verimc.cell"
formatVersion = 1
initPlanFormatName = "verimc.initPlan"
initPlanFormatVersion = 1

horizontalDirections = ("north", "east", "south", "west")
allDirections = horizontalDirections + ("up", "down")

supportedPortTypes = frozenset({"bit"})
supportedEncodingKinds = frozenset({"digital"})
supportedStrategies = frozenset({"orderedPlacement"})
knownStrategies = frozenset({"orderedPlacement", "bulkThenUpdates"})
supportedEvidenceStatuses = frozenset({"unverified"})
knownEvidenceStatuses = frozenset({"unverified", "scenarioVerified"})
inputChannels = frozenset({"receivePower"})
outputChannels = frozenset({"weakOutput", "strongOutput"})

chunkSize = 16
signalRange = (0, 15)

# 属性的旋转语义分类。invariant：绕 y 轴旋转不变；facingValue：值是水平方向；
# directionName：属性名本身是水平方向（红石粉四向连接）。
propertyInvariant = "invariant"
propertyFacingValue = "facingValue"
propertyDirectionName = "directionName"

_booleanValues = frozenset({"true", "false"})
_horizontalValues = frozenset(horizontalDirections)
_wireConnections = frozenset({"none", "side", "up"})
_powerValues = frozenset(str(level) for level in range(signalRange[0], signalRange[1] + 1))

# 本工具声明支持的方块子集及其属性旋转语义。该表未依据 26.2 注册表核验，只是保守子集：
# 表内属性按此语义变换，表外属性名或表外取值一律原样保留并禁止非 north 放置。
supportedBlocks = {
    "minecraft:stone": {},
    "minecraft:redstone_wire": {
        "east": (_wireConnections, propertyDirectionName),
        "north": (_wireConnections, propertyDirectionName),
        "power": (_powerValues, propertyInvariant),
        "south": (_wireConnections, propertyDirectionName),
        "west": (_wireConnections, propertyDirectionName),
    },
    "minecraft:repeater": {
        "delay": (frozenset({"1", "2", "3", "4"}), propertyInvariant),
        "facing": (_horizontalValues, propertyFacingValue),
        "locked": (_booleanValues, propertyInvariant),
        "powered": (_booleanValues, propertyInvariant),
    },
    "minecraft:lever": {
        "face": (frozenset({"floor", "wall", "ceiling"}), propertyInvariant),
        "facing": (_horizontalValues, propertyFacingValue),
        "powered": (_booleanValues, propertyInvariant),
    },
    "minecraft:redstone_lamp": {
        "lit": (_booleanValues, propertyInvariant),
    },
}
supportedBlockNames = frozenset(supportedBlocks)

manifestFields = (
    "format", "formatVersion", "id", "version", "targetId", "ports", "pins",
    "blocks", "blockEntities", "bounds", "keepouts", "transforms",
    "placementDomain", "initialization", "model", "timing",
    "requiredCapabilities", "evidence",
)

_identifierPattern = re.compile(r"^[A-Za-z][A-Za-z0-9_]*$")
_resourcePattern = re.compile(r"^[a-z0-9_.-]+:[a-z0-9_./-]+$")
_sha256Pattern = re.compile(r"^[0-9a-f]{64}$")
_instancePattern = re.compile(r"^[^\s\x00-\x1f]+$")
_drivePattern = re.compile(r"^[A-Za-z]:")


class PhysicalError(ValueError):
    """物理组件清单读取或放置失败；`code` 是诊断码，也是消息前缀。"""

    def __init__(self, code, detail):
        super().__init__(f"{code}: {detail}")
        self.code = code
        self.detail = detail


def _fail(code, detail):
    raise PhysicalError(code, detail)


# ---------------------------------------------------------------- 基础类型检查


def _requireObject(value, where):
    if not isinstance(value, dict):
        _fail("EFormat", f"{where} 必须是 JSON 对象")
    return value


def _requireArray(value, where):
    if not isinstance(value, list):
        _fail("EFormat", f"{where} 必须是 JSON 数组")
    return value


def _requireString(value, where, pattern=None):
    if not isinstance(value, str) or not value:
        _fail("EFormat", f"{where} 必须是非空字符串")
    if pattern is not None and not pattern.match(value):
        _fail("EFormat", f"{where} 取值不合法：{value!r}")
    return value


def _requireInteger(value, where):
    # 布尔是 Python 的 int 子类，但 JSON 的 true/false 不是整数，必须显式拒绝。
    if isinstance(value, bool) or not isinstance(value, int):
        _fail("EFormat", f"{where} 必须是 JSON 整数")
    return value


def _requireNonNegative(value, where):
    if _requireInteger(value, where) < 0:
        _fail("EFormat", f"{where} 必须是非负整数")
    return value


def _requireKeys(value, required, optional, where):
    _requireObject(value, where)
    missing = sorted(set(required) - set(value))
    if missing:
        _fail("EFormat", f"{where} 缺少必需字段：{', '.join(missing)}")
    unknown = sorted(set(value) - set(required) - set(optional))
    if unknown:
        _fail("EFormat", f"{where} 含未知字段：{', '.join(unknown)}")
    return value


def _requireCoordinate(value, where):
    _requireArray(value, where)
    if len(value) != 3:
        _fail("EFormat", f"{where} 必须是 3 个整数的坐标")
    return tuple(_requireInteger(item, f"{where}[{index}]") for index, item in enumerate(value))


def _requireBox(value, where):
    _requireKeys(value, ("min", "max"), (), where)
    low = _requireCoordinate(value["min"], f"{where}.min")
    high = _requireCoordinate(value["max"], f"{where}.max")
    for axis, name in enumerate("xyz"):
        if low[axis] >= high[axis]:
            _fail("EFormat", f"{where} 的 {name} 轴半开区间为空：{low[axis]} >= {high[axis]}")
    return low, high


def _inBox(point, box):
    low, high = box
    return all(low[axis] <= point[axis] < high[axis] for axis in range(3))


# ---------------------------------------------------------------- 变换基础运算


def floorMod(value, modulus):
    """数学 floor 取模；负坐标结果为非负，供区块余数判断使用。"""
    if modulus <= 0:
        _fail("EFormat", f"取模的模数必须为正：{modulus}")
    return value - modulus * (value // modulus)


def rotateDirection(direction, facing):
    """按放置朝向旋转一个方向；up/down 不随水平旋转改变。"""
    if direction in ("up", "down"):
        return direction
    steps = horizontalDirections.index(facing)
    return horizontalDirections[(horizontalDirections.index(direction) + steps) % 4]


def transformPoint(point, facing):
    """绕局部 (0,0,0) 的整数方块坐标旋转；east 为 (x,y,z) -> (-z,y,x)，其余依次复合。"""
    x, y, z = point
    for _ in range(horizontalDirections.index(facing)):
        x, z = -z, x
    return (x, y, z)


def transformBox(box, facing, origin):
    """变换半开盒。不能直接旋转 max 点：先取整数占用区域的上下界再旋转再复原半开。"""
    low, high = box
    lowCell = transformPoint(low, facing)
    highCell = transformPoint(tuple(high[axis] - 1 for axis in range(3)), facing)
    newLow = tuple(min(lowCell[axis], highCell[axis]) + origin[axis] for axis in range(3))
    newHigh = tuple(max(lowCell[axis], highCell[axis]) + 1 + origin[axis] for axis in range(3))
    return newLow, newHigh


def transformBlockPosition(position, facing, origin):
    rotated = transformPoint(position, facing)
    return tuple(rotated[axis] + origin[axis] for axis in range(3))


# ---------------------------------------------------------------- 路径与 JSON


def _resolveRoot(projectRoot):
    root = Path(projectRoot).resolve()
    if not root.is_dir():
        _fail("EPath", f"项目根不是目录：{projectRoot}")
    return root


def _requireInsideRoot(path, root, where):
    resolved = path.resolve()
    if not resolved.is_relative_to(root):
        _fail("EPath", f"{where} 解析后不在项目根内：{resolved}")
    if not resolved.is_file():
        _fail("EResourceMissing", f"{where} 不是可读的普通文件：{resolved}")
    return resolved


def _resolveReference(reference, baseDir, root, where):
    """解析清单内的相对路径引用；绝对路径、网络地址与逃逸项目根一律拒绝。"""
    _requireString(reference, where)
    if any(character < " " or character == "\x7f" for character in reference):
        _fail("EPath", f"{where} 含控制字符")
    if "://" in reference:
        _fail("EPath", f"{where} 不接受网络地址：{reference}")
    if "\\" in reference:
        _fail("EPath", f"{where} 必须使用 / 分隔：{reference}")
    if reference.startswith("/") or _drivePattern.match(reference):
        _fail("EPath", f"{where} 必须是相对路径：{reference}")
    if "" in reference.split("/"):
        _fail("EPath", f"{where} 含空路径段：{reference}")
    return _requireInsideRoot(baseDir / reference, root, where)


def _duplicateKeyHook(where):
    def hook(pairs):
        seen = set()
        for key, _ in pairs:
            if key in seen:
                _fail("EFormat", f"{where} 存在重复 JSON 键：{key}")
            seen.add(key)
        return dict(pairs)

    return hook


def _rejectConstant(name):
    _fail("EFormat", f"JSON 不接受特殊常量：{name}")


def _readJson(path, root, where):
    """读取 UTF-8 JSON 并返回 (值, 文件 SHA-256)；重复键、BOM、NaN 与超深嵌套都拒绝。"""
    data = path.read_bytes()
    if data.startswith(b"\xef\xbb\xbf"):
        _fail("EFormat", f"{where} 含 UTF-8 BOM")
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as error:
        _fail("EFormat", f"{where} 不是合法 UTF-8：{error}")
    try:
        value = json.loads(text, object_pairs_hook=_duplicateKeyHook(where),
                           parse_constant=_rejectConstant)
    except json.JSONDecodeError as error:
        _fail("EFormat", f"{where} JSON 语法错误：第 {error.lineno} 行第 {error.colno} 列")
    except RecursionError:
        _fail("EFormat", f"{where} JSON 嵌套过深")
    return value, hashlib.sha256(data).hexdigest()


def _relativeKey(path, root):
    return path.relative_to(root).as_posix()


# ---------------------------------------------------------------- 清单各部分校验


def _validatePorts(manifest, where):
    ports = _requireArray(manifest["ports"], f"{where}.ports")
    if not ports:
        _fail("EFormat", f"{where}.ports 不能为空")
    result = {}
    for index, entry in enumerate(ports):
        location = f"{where}.ports[{index}]"
        _requireKeys(entry, ("name", "direction", "type"), (), location)
        name = _requireString(entry["name"], f"{location}.name", _identifierPattern)
        if name in result:
            _fail("EFormat", f"{where}.ports 端口名重复：{name}")
        direction = _requireString(entry["direction"], f"{location}.direction")
        if direction not in ("input", "output"):
            _fail("EFormat", f"{location}.direction 必须是 input 或 output")
        typeText = _requireString(entry["type"], f"{location}.type")
        if typeText not in supportedPortTypes:
            _fail("ECapability", f"{location} 端口类型 {typeText} 超出本工具支持的 "
                                 f"{sorted(supportedPortTypes)}")
        result[name] = direction
    return result


def _validateEncoding(value, where):
    _requireObject(value, where)
    if "kind" not in value:
        _fail("EFormat", f"{where} 缺少必需字段：kind")
    kind = _requireString(value["kind"], f"{where}.kind")
    if kind not in ("digital", "strength", "clock"):
        _fail("EFormat", f"{where}.kind 不是规范定义的编码种类：{kind}")
    if kind not in supportedEncodingKinds:
        # strength/clock 的字段形状与 digital 不同，先按能力拒绝而不是报字段缺失。
        _fail("ECapability", f"{where} 的 {kind} 编码超出本工具支持的 "
                             f"{sorted(supportedEncodingKinds)}")
    _requireKeys(value, ("kind", "low", "high"), (), where)
    ranges = {}
    for field in ("low", "high"):
        bound = _requireArray(value[field], f"{where}.{field}")
        if len(bound) != 2:
            _fail("EFormat", f"{where}.{field} 必须是闭区间 [a,b]")
        first = _requireInteger(bound[0], f"{where}.{field}[0]")
        second = _requireInteger(bound[1], f"{where}.{field}[1]")
        if not signalRange[0] <= first <= second <= signalRange[1]:
            _fail("EFormat", f"{where}.{field} 必须是 {signalRange[0]}–{signalRange[1]} 内的有序闭区间")
        ranges[field] = (first, second)
    low, high = ranges["low"], ranges["high"]
    if low[0] <= high[1] and high[0] <= low[1]:
        _fail("EFormat", f"{where} 的 low 与 high 区间重叠")
    return value


def _validatePins(manifest, ports, blockPositions, bounds, where):
    pins = _requireArray(manifest["pins"], f"{where}.pins")
    covered = {}
    faces = set()
    for index, entry in enumerate(pins):
        location = f"{where}.pins[{index}]"
        _requireKeys(entry, ("port", "leaf", "pos", "face", "channel", "encoding", "maxLoads"),
                     (), location)
        port = _requireString(entry["port"], f"{location}.port", _identifierPattern)
        if port not in ports:
            _fail("EFormat", f"{location}.port 未在 ports 中声明：{port}")
        leaf = _requireArray(entry["leaf"], f"{location}.leaf")
        for leafIndex, item in enumerate(leaf):
            _requireNonNegative(item, f"{location}.leaf[{leafIndex}]")
        if leaf:
            # 本工具只支持标量 bit 端口，其叶子路径固定为空数组。
            _fail("EFormat", f"{location}.leaf 对标量 bit 端口必须是空数组")
        key = (port, tuple(leaf))
        if key in covered:
            _fail("EFormat", f"{where}.pins 端口叶子重复：{port}{list(leaf)}")
        position = _requireCoordinate(entry["pos"], f"{location}.pos")
        if not _inBox(position, bounds):
            _fail("EFormat", f"{location}.pos 不在 bounds 内：{list(position)}")
        if position not in blockPositions:
            _fail("EFormat", f"{location}.pos 没有对应的组件方块：{list(position)}")
        face = _requireString(entry["face"], f"{location}.face")
        if face not in allDirections:
            _fail("EFormat", f"{location}.face 必须是六向之一：{face}")
        if (position, face) in faces:
            _fail("EFormat", f"{where}.pins 位置与面重复：{list(position)} {face}")
        faces.add((position, face))
        channel = _requireString(entry["channel"], f"{location}.channel")
        direction = ports[port]
        allowed = inputChannels if direction == "input" else outputChannels
        if channel not in allowed:
            _fail("EFormat", f"{location}.channel 与 {direction} 端口不匹配，只能是 {sorted(allowed)}")
        _validateEncoding(_requireObject(entry["encoding"], f"{location}.encoding"),
                          f"{location}.encoding")
        loads = entry["maxLoads"]
        if direction == "input":
            if loads is not None:
                _fail("EFormat", f"{location}.maxLoads 对输入端口必须是 null")
        else:
            _requireNonNegative(loads, f"{location}.maxLoads")
        covered[key] = True
    missing = sorted(name for name in ports if (name, ()) not in covered)
    if missing:
        _fail("EFormat", f"{where}.pins 未覆盖端口叶子：{', '.join(missing)}")
    return pins


def _validateBlocks(blocks, bounds, where):
    _requireArray(blocks, where)
    if not blocks:
        _fail("EFormat", f"{where} 不能为空")
    positions = {}
    previous = None
    identityOnly = {}
    for index, entry in enumerate(blocks):
        location = f"{where}[{index}]"
        _requireKeys(entry, ("pos", "name", "properties"), (), location)
        position = _requireCoordinate(entry["pos"], f"{location}.pos")
        if previous is not None:
            if position == previous:
                _fail("EFormat", f"{where} 坐标重复：{list(position)}")
            if position < previous:
                _fail("EFormat", f"{location}.pos 未按 x,y,z 字典序规范化：{list(position)}")
        previous = position
        if not _inBox(position, bounds):
            _fail("EFormat", f"{location}.pos 不在 bounds 内：{list(position)}")
        name = _requireString(entry["name"], f"{location}.name", _resourcePattern)
        if name not in supportedBlocks:
            _fail("ECapability", f"{location} 方块 {name} 超出本工具支持的 "
                                 f"{sorted(supportedBlockNames)}")
        reason = _validateProperties(entry["properties"], name, f"{location}.properties")
        if reason is not None:
            identityOnly[position] = reason
        positions[position] = entry
    return positions, identityOnly


def _validateProperties(properties, blockName, where):
    """校验方块属性并返回“禁止旋转的原因”；None 表示全部属性的旋转语义已知。"""
    _requireObject(properties, where)
    table = supportedBlocks[blockName]
    for key in sorted(properties):
        if not isinstance(properties[key], str) or not properties[key]:
            _fail("EFormat", f"{where}.{key} 方块状态属性值必须是非空字符串")
    missing = sorted(set(table) - set(properties))
    if missing:
        _fail("EFormat", f"{where} 缺少方块状态属性：{', '.join(missing)}")
    if list(properties) != sorted(properties):
        _fail("EFormat", f"{where} 属性键未排序")
    for key in sorted(properties):
        if key not in table:
            return f"{where} 含未知属性 {key}，其旋转语义无法确定"
        if properties[key] not in table[key][0]:
            return f"{where}.{key} 取值 {properties[key]} 的旋转语义无法确定"
    return None


def rotateProperties(blockName, properties, facing):
    """按朝向旋转已知方块状态属性；属性名为方向的整体改名，facing 值整体旋转。

    旋转语义未知的属性（表外属性名或表外取值）原样保留，且只允许 north（恒等）放置。
    """
    table = supportedBlocks.get(blockName, {})
    result = {}
    for key, value in properties.items():
        entry = table.get(key)
        if entry is None or value not in entry[0]:
            if facing != "north":
                _fail("ECapability", f"{blockName} 的属性 {key}={value} 旋转语义未知，"
                                     f"不能按 {facing} 放置")
            result[key] = value
            continue
        kind = entry[1]
        if kind == propertyDirectionName:
            result[rotateDirection(key, facing)] = value
        elif kind == propertyFacingValue:
            result[key] = rotateDirection(value, facing)
        else:
            result[key] = value
    return dict(sorted(result.items()))


def _validatePlacementDomain(value, where):
    _requireKeys(value, ("originMin", "originMax", "chunkResidues", "dimension"), (), where)
    low = _requireCoordinate(value["originMin"], f"{where}.originMin")
    high = _requireCoordinate(value["originMax"], f"{where}.originMax")
    for axis, name in enumerate("xyz"):
        if low[axis] > high[axis]:
            _fail("EFormat", f"{where} 的 {name} 轴原点范围颠倒：{low[axis]} > {high[axis]}")
    residues = _requireArray(value["chunkResidues"], f"{where}.chunkResidues")
    seen = set()
    for index, entry in enumerate(residues):
        location = f"{where}.chunkResidues[{index}]"
        _requireArray(entry, location)
        if len(entry) != 2:
            _fail("EFormat", f"{location} 必须是 [x 余数, z 余数]")
        pair = tuple(_requireNonNegative(item, f"{location}[{axis}]")
                     for axis, item in enumerate(entry))
        if any(item >= chunkSize for item in pair):
            _fail("EFormat", f"{location} 余数必须小于 {chunkSize}")
        if pair in seen:
            _fail("EFormat", f"{where}.chunkResidues 余数对重复：{list(pair)}")
        seen.add(pair)
    _requireString(value["dimension"], f"{where}.dimension", _resourcePattern)
    return low, high, seen


def _validateTransforms(value, where):
    _requireArray(value, where)
    if not value:
        _fail("EFormat", f"{where} 不能为空")
    result = []
    for index, entry in enumerate(value):
        facing = _requireString(entry, f"{where}[{index}]")
        if facing not in horizontalDirections:
            _fail("ECapability", f"{where}[{index}] 只允许 {list(horizontalDirections)}，"
                                 f"0.1 无镜像和俯仰：{facing}")
        if facing in result:
            _fail("EFormat", f"{where} 朝向重复：{facing}")
        result.append(facing)
    return result


def _validateTiming(value, ports, baseDir, root, assets, where):
    _requireKeys(value, ("arcs", "clockChecks", "settling", "characterization"), (), where)
    arcs = _requireArray(value["arcs"], f"{where}.arcs")
    seenArcs = set()
    for index, entry in enumerate(arcs):
        location = f"{where}.arcs[{index}]"
        _requireKeys(entry, ("from", "to", "minGt", "maxGt"), (), location)
        source = _requireString(entry["from"], f"{location}.from", _identifierPattern)
        sink = _requireString(entry["to"], f"{location}.to", _identifierPattern)
        if ports.get(source) != "input":
            _fail("EFormat", f"{location}.from 必须是已声明的输入端口：{source}")
        if ports.get(sink) != "output":
            _fail("EFormat", f"{location}.to 必须是已声明的输出端口：{sink}")
        if (source, sink) in seenArcs:
            _fail("EFormat", f"{where}.arcs 端口对重复：{source} -> {sink}")
        seenArcs.add((source, sink))
        minimum = _requireNonNegative(entry["minGt"], f"{location}.minGt")
        if entry["maxGt"] is not None:
            maximum = _requireNonNegative(entry["maxGt"], f"{location}.maxGt")
            if maximum < minimum:
                _fail("EFormat", f"{location} 的 maxGt 小于 minGt")
    if _requireArray(value["clockChecks"], f"{where}.clockChecks"):
        _fail("ECapability", f"{where}.clockChecks 需要 clock 端口能力，本工具只支持 bit 端口")
    settling = _requireArray(value["settling"], f"{where}.settling")
    boundedSettling = False
    covered = set()
    for index, entry in enumerate(settling):
        location = f"{where}.settling[{index}]"
        _requireKeys(entry, ("output", "maxGt"), (), location)
        output = _requireString(entry["output"], f"{location}.output", _identifierPattern)
        if ports.get(output) != "output":
            _fail("EFormat", f"{location}.output 必须是已声明的输出端口：{output}")
        if output in covered:
            _fail("EFormat", f"{where}.settling 输出端口重复：{output}")
        covered.add(output)
        if entry["maxGt"] is not None:
            _requireNonNegative(entry["maxGt"], f"{location}.maxGt")
            boundedSettling = True
    missing = sorted(name for name, direction in ports.items()
                     if direction == "output" and name not in covered)
    if missing:
        _fail("EFormat", f"{where}.settling 未覆盖输出端口（未知上界写 null）：{', '.join(missing)}")
    characterization = value["characterization"]
    if characterization is None:
        if arcs or boundedSettling:
            _fail("EFormat", f"{where} 声明了时序界就必须提供 characterization 记录")
        return
    path = _resolveReference(characterization, baseDir, root, f"{where}.characterization")
    record, digest = _readJson(path, root, f"{where}.characterization")
    assets[_relativeKey(path, root)] = digest
    _requireKeys(record, ("profileId", "conditions", "caseIds", "limitations"), (),
                 f"{where}.characterization 文件")
    _requireString(record["profileId"], f"{where}.characterization.profileId")
    for field in ("conditions", "limitations"):
        entries = _requireArray(record[field], f"{where}.characterization.{field}")
        for index, item in enumerate(entries):
            _requireString(item, f"{where}.characterization.{field}[{index}]")
    if _requireArray(record["caseIds"], f"{where}.characterization.caseIds"):
        _fail("EFormat", f"{where}.characterization.caseIds 引用了证据用例，"
                         f"但 evidence 只允许 unverified 且 cases 为空")


def _validateEvidence(value, where):
    _requireKeys(value, ("status", "cases"), (), where)
    status = _requireString(value["status"], f"{where}.status")
    if status not in knownEvidenceStatuses:
        _fail("EFormat", f"{where}.status 只能是 {sorted(knownEvidenceStatuses)}：{status}")
    cases = _requireArray(value["cases"], f"{where}.cases")
    if status not in supportedEvidenceStatuses:
        _fail("ECapability", f"{where}.status 为 {status}，本工具无法核验场景证据记录，拒绝而不接受")
    if cases:
        _fail("EFormat", f"{where}.status 为 unverified 时 cases 必须为空")


def _validateModel(value, baseDir, root, assets, where):
    if value is None:
        # 无模型：保留 null，不编造等价逻辑，也不给出任何逻辑结论。
        return
    _requireKeys(value, ("source", "module", "parameters", "sourceSha256"), (), where)
    path = _resolveReference(value["source"], baseDir, root, f"{where}.source")
    _requireString(value["module"], f"{where}.module", _identifierPattern)
    parameters = _requireObject(value["parameters"], f"{where}.parameters")
    for key in sorted(parameters):
        _requireString(key, f"{where}.parameters 的键", _identifierPattern)
        if not isinstance(parameters[key], (bool, int)):
            _fail("EFormat", f"{where}.parameters.{key} 只能是 JSON 整数或布尔值")
    declared = _requireString(value["sourceSha256"], f"{where}.sourceSha256", _sha256Pattern)
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if digest != declared:
        _fail("EFormat", f"{where}.sourceSha256 与文件实际哈希不一致：{digest}")
    assets[_relativeKey(path, root)] = digest


def _validateInitialization(plan, blockPositions, ports, supportedUpdateKinds, where):
    _requireKeys(plan, ("format", "formatVersion", "strategy", "steps", "externalRequirements"),
                 (), where)
    if plan["format"] != initPlanFormatName:
        _fail("EFormat", f"{where}.format 必须是 {initPlanFormatName}")
    if type(plan["formatVersion"]) is not int or plan["formatVersion"] != initPlanFormatVersion:
        _fail("EArtifactVersion", f"{where}.formatVersion 只支持 {initPlanFormatVersion}")
    strategy = _requireString(plan["strategy"], f"{where}.strategy")
    if strategy not in knownStrategies:
        _fail("EFormat", f"{where}.strategy 只能是 {sorted(knownStrategies)}：{strategy}")
    if strategy not in supportedStrategies:
        _fail("ECapability", f"{where}.strategy 为 {strategy}，本工具只支持 "
                             f"{sorted(supportedStrategies)}")
    steps = _requireArray(plan["steps"], f"{where}.steps")
    placed = set()
    for index, entry in enumerate(steps):
        location = f"{where}.steps[{index}]"
        _requireObject(entry, location)
        operation = entry.get("op")
        if operation == "place":
            _requireKeys(entry, ("op", "pos"), (), location)
            position = _requireCoordinate(entry["pos"], f"{location}.pos")
            if position not in blockPositions:
                _fail("EInitialization", f"{location}.pos 不是 blocks 中的方块：{list(position)}")
            if position in placed:
                _fail("EInitialization", f"{location}.pos 重复放置：{list(position)}")
            placed.add(position)
        elif operation == "update":
            _requireKeys(entry, ("op", "pos", "kind"), (), location)
            position = _requireCoordinate(entry["pos"], f"{location}.pos")
            kind = _requireString(entry["kind"], f"{location}.kind")
            if kind not in (supportedUpdateKinds or ()):
                _fail("ECapability", f"{location}.kind 为 {kind}，不在调用方给出的受支持更新类型内")
            if position not in blockPositions:
                _fail("EInitialization", f"{location}.pos 不是 blocks 中的方块：{list(position)}")
        elif operation == "wait":
            _requireKeys(entry, ("op", "ticks"), (), location)
            _requireNonNegative(entry["ticks"], f"{location}.ticks")
        else:
            _fail("EFormat", f"{location}.op 只能是 place/update/wait：{operation!r}")
    if strategy == "orderedPlacement":
        missing = sorted(set(blockPositions) - placed)
        if missing:
            _fail("EInitialization", f"{where} 的 orderedPlacement 未覆盖方块："
                                     f"{[list(item) for item in missing]}")
    requirements = _requireArray(plan["externalRequirements"], f"{where}.externalRequirements")
    for index, entry in enumerate(requirements):
        location = f"{where}.externalRequirements[{index}]"
        _requireKeys(entry, ("port", "purpose"), (), location)
        port = _requireString(entry["port"], f"{location}.port", _identifierPattern)
        if port not in ports:
            _fail("EFormat", f"{location}.port 未在 ports 中声明：{port}")
        _requireString(entry["purpose"], f"{location}.purpose")
    return plan


# ---------------------------------------------------------------- 公开接口


def loadCell(manifestPath, projectRoot, targetId, *,
             supportedUpdateKinds=None, providedCapabilities=None):
    """严格读取并校验组件清单，锁定清单及全部引用文件的 SHA-256。

    参数：
      manifestPath  `*.vmcell.json` 清单路径，解析后必须位于 projectRoot 内。
      projectRoot   项目根目录；所有引用解析（含符号链接）后都必须仍在其中。
      targetId      构建目标 id，必须与清单 targetId 完全一致。
      supportedUpdateKinds  调用方（目标配置）声明的受支持 update 类型集合；
                    默认 None 表示没有任何受支持类型，任何 update 步骤都报 ECapability。
      providedCapabilities  调用方（目标配置）声明已提供的能力键集合；默认 None 表示
                    没有提供任何能力，因此清单的 requiredCapabilities 必须为空。

    返回 `{manifest, blocks, blockEntities, initialization, assets}`；
    assets 是相对于 projectRoot 的 POSIX 路径到 SHA-256 十六进制串的字典。
    失败抛 PhysicalError。本函数不执行清单引用的任何代码。
    """
    root = _resolveRoot(projectRoot)
    manifestFile = _requireInsideRoot(Path(manifestPath), root, "清单路径")
    if not manifestFile.name.endswith(".vmcell.json"):
        _fail("EFormat", f"清单文件名必须以 .vmcell.json 结尾：{manifestFile.name}")
    where = "清单"
    manifest, manifestDigest = _readJson(manifestFile, root, where)
    assets = {_relativeKey(manifestFile, root): manifestDigest}
    baseDir = manifestFile.parent

    _requireKeys(manifest, manifestFields, ("annotations",), where)
    if manifest["format"] != formatName:
        _fail("EFormat", f"{where}.format 必须是 {formatName}")
    if type(manifest["formatVersion"]) is not int or manifest["formatVersion"] != formatVersion:
        _fail("EArtifactVersion", f"{where}.formatVersion 只支持 {formatVersion}")
    _requireString(manifest["id"], f"{where}.id")
    _requireString(manifest["version"], f"{where}.version")
    declaredTarget = _requireString(manifest["targetId"], f"{where}.targetId")
    if declaredTarget != _requireString(targetId, "构建目标 id"):
        _fail("ETarget", f"{where}.targetId 为 {declaredTarget}，与构建目标 {targetId} 不一致")
    if "annotations" in manifest:
        _requireObject(manifest["annotations"], f"{where}.annotations")

    capabilities = _requireArray(manifest["requiredCapabilities"], f"{where}.requiredCapabilities")
    seenCapabilities = set()
    for index, entry in enumerate(capabilities):
        key = _requireString(entry, f"{where}.requiredCapabilities[{index}]")
        if key in seenCapabilities:
            _fail("EFormat", f"{where}.requiredCapabilities 能力键重复：{key}")
        seenCapabilities.add(key)
    unmet = sorted(seenCapabilities - set(providedCapabilities or ()))
    if unmet:
        _fail("ECapability", f"{where}.requiredCapabilities 未被调用方确认提供：{', '.join(unmet)}")

    bounds = _requireBox(manifest["bounds"], f"{where}.bounds")
    keepouts = []
    for index, entry in enumerate(_requireArray(manifest["keepouts"], f"{where}.keepouts")):
        keepouts.append(_requireBox(entry, f"{where}.keepouts[{index}]"))
    ports = _validatePorts(manifest, where)
    _validateTransforms(manifest["transforms"], f"{where}.transforms")
    _validatePlacementDomain(_requireObject(manifest["placementDomain"],
                                            f"{where}.placementDomain"),
                             f"{where}.placementDomain")

    blocksPath = _resolveReference(manifest["blocks"], baseDir, root, f"{where}.blocks")
    blocks, blocksDigest = _readJson(blocksPath, root, "方块数组文件")
    assets[_relativeKey(blocksPath, root)] = blocksDigest
    # identityOnly 记录旋转语义未知的方块；加载阶段只确认取值合法，禁转由 placeCell 判定。
    blockPositions, _ = _validateBlocks(blocks, bounds, "方块数组")
    for index, keepout in enumerate(keepouts):
        collisions = sorted(position for position in blockPositions if _inBox(position, keepout))
        if collisions:
            _fail("EPlacement", f"{where}.keepouts[{index}] 与自身方块重叠："
                                f"{[list(item) for item in collisions]}")

    entitiesPath = _resolveReference(manifest["blockEntities"], baseDir, root,
                                     f"{where}.blockEntities")
    blockEntities, entitiesDigest = _readJson(entitiesPath, root, "方块实体文件")
    assets[_relativeKey(entitiesPath, root)] = entitiesDigest
    if _requireArray(blockEntities, "方块实体数组"):
        _fail("ECapability", "方块实体数组非空：本工具没有无损 NBT 表示能力，按规范拒绝该组件")

    _validatePins(manifest, ports, blockPositions, bounds, where)
    _validateModel(manifest["model"], baseDir, root, assets, f"{where}.model")
    _validateTiming(_requireObject(manifest["timing"], f"{where}.timing"), ports,
                    baseDir, root, assets, f"{where}.timing")
    _validateEvidence(_requireObject(manifest["evidence"], f"{where}.evidence"),
                      f"{where}.evidence")

    planPath = _resolveReference(manifest["initialization"], baseDir, root,
                                 f"{where}.initialization")
    plan, planDigest = _readJson(planPath, root, "初始化计划")
    assets[_relativeKey(planPath, root)] = planDigest
    _validateInitialization(_requireObject(plan, "初始化计划"), blockPositions, ports,
                            supportedUpdateKinds, "初始化计划")

    return {
        "manifest": manifest,
        "blocks": blocks,
        "blockEntities": blockEntities,
        "initialization": plan,
        "assets": dict(sorted(assets.items())),
    }


def _cellView(cell):
    """从 loadCell 结果重新导出放置所需的几何数据，同时防止调用方传入残缺对象。"""
    if not isinstance(cell, dict):
        _fail("EFormat", "cell 必须是 loadCell 的返回值")
    missing = sorted({"manifest", "blocks", "initialization"} - set(cell))
    if missing:
        _fail("EFormat", f"cell 缺少 {', '.join(missing)}，必须是 loadCell 的返回值")
    manifest = _requireObject(cell["manifest"], "cell.manifest")
    _requireKeys(manifest, manifestFields, ("annotations",), "cell.manifest")
    bounds = _requireBox(manifest["bounds"], "cell.manifest.bounds")
    keepouts = [_requireBox(entry, f"cell.manifest.keepouts[{index}]")
                for index, entry in enumerate(_requireArray(manifest["keepouts"],
                                                            "cell.manifest.keepouts"))]
    positions, identityOnly = _validateBlocks(cell["blocks"], bounds, "cell.blocks")
    ports = _validatePorts(manifest, "cell.manifest")
    _validateTransforms(manifest["transforms"], "cell.manifest.transforms")
    _validatePins(manifest, ports, positions, bounds, "cell.manifest")
    _requireKeys(cell["initialization"],
                 ("format", "formatVersion", "strategy", "steps", "externalRequirements"),
                 (), "cell.initialization")
    _requireArray(cell["initialization"]["steps"], "cell.initialization.steps")
    _requireArray(cell["initialization"]["externalRequirements"],
                  "cell.initialization.externalRequirements")
    return manifest, bounds, keepouts, identityOnly


def placeCell(cell, instanceId, origin, facing, *, worldYRange=None):
    """把已校验组件按固定原点与朝向做刚性变换，返回世界坐标下的放置结果。

    参数：
      cell        loadCell 的返回值。
      instanceId  实例标识，非空且不含空白或控制字符。
      origin      构建根坐标系下的整数原点 [x, y, z]。
      facing      north/east/south/west，且必须在清单 transforms 内。
      worldYRange 可选 (最小含, 最大不含) 世界 y 界；默认 None 表示本工具不知道目标
                  世界高度，不做该项检查，由 C++ 目标适配器负责。

    返回 `{blocks, pins, bounds, keepouts, initialization, instanceId}`。
    """
    manifest, cellBounds, cellKeepouts, identityOnly = _cellView(cell)
    _requireString(instanceId, "instanceId", _instancePattern)
    if not isinstance(origin, (list, tuple)):
        _fail("EFormat", "origin 必须是 3 个整数的数组")
    origin = _requireCoordinate(list(origin), "origin")
    _requireString(facing, "facing")
    if facing not in horizontalDirections:
        _fail("EFormat", f"facing 只能是 {list(horizontalDirections)}：{facing}")
    if facing not in manifest["transforms"]:
        _fail("EPlacement", f"朝向 {facing} 不在清单 transforms {manifest['transforms']} 内")

    domainLow, domainHigh, residues = _validatePlacementDomain(manifest["placementDomain"],
                                                               "清单.placementDomain")
    for axis, name in enumerate("xyz"):
        if not domainLow[axis] <= origin[axis] < domainHigh[axis]:
            _fail("EPlacement", f"原点 {name}={origin[axis]} 不在 placementDomain 的半开范围 "
                                f"[{domainLow[axis]}, {domainHigh[axis]}) 内")
    residue = (floorMod(origin[0], chunkSize), floorMod(origin[2], chunkSize))
    if residue not in residues:
        _fail("EPlacement", f"原点区块余数 {list(residue)} 不在 placementDomain.chunkResidues "
                            f"{[list(item) for item in sorted(residues)]} 内")

    if facing != "north" and identityOnly:
        position = sorted(identityOnly)[0]
        _fail("ECapability", f"朝向 {facing} 需要旋转语义未知的属性：{identityOnly[position]}")

    bounds = transformBox(cellBounds, facing, origin)
    keepouts = [transformBox(box, facing, origin) for box in cellKeepouts]

    blocks = []
    seen = set()
    for entry in cell["blocks"]:
        position = transformBlockPosition(tuple(entry["pos"]), facing, origin)
        if position in seen:
            _fail("EPlacement", f"变换后方块坐标重复：{list(position)}")
        seen.add(position)
        if not _inBox(position, bounds):
            _fail("EPlacement", f"变换后方块 {list(position)} 超出变换后的 bounds")
        blocks.append({"pos": list(position), "name": entry["name"],
                       "properties": rotateProperties(entry["name"], entry["properties"], facing)})
    blocks.sort(key=lambda item: tuple(item["pos"]))

    pins = []
    for entry in manifest["pins"]:
        position = transformBlockPosition(tuple(entry["pos"]), facing, origin)
        encoding = entry["encoding"]
        pins.append({"port": entry["port"], "leaf": list(entry["leaf"]), "pos": list(position),
                     "face": rotateDirection(entry["face"], facing), "channel": entry["channel"],
                     "encoding": {"kind": encoding["kind"], "low": list(encoding["low"]),
                                  "high": list(encoding["high"])},
                     "maxLoads": entry["maxLoads"]})

    if worldYRange is not None:
        if not isinstance(worldYRange, (list, tuple)) or len(worldYRange) != 2:
            _fail("EFormat", "worldYRange 必须是 (最小含, 最大不含) 两个整数")
        low = _requireInteger(worldYRange[0], "worldYRange[0]")
        high = _requireInteger(worldYRange[1], "worldYRange[1]")
        if low >= high:
            _fail("EFormat", f"worldYRange 必须是非空半开区间：[{low}, {high})")
        spans = [(bounds[0][1], bounds[1][1], "bounds")]
        spans += [(box[0][1], box[1][1], "keepout") for box in keepouts]
        spans += [(item["pos"][1], item["pos"][1] + 1, "方块") for item in blocks]
        for spanLow, spanHigh, name in spans:
            if spanLow < low or spanHigh > high:
                _fail("EPlacement", f"{name} 的 y 区间 [{spanLow}, {spanHigh}) 超出世界 y 界 "
                                    f"[{low}, {high})")

    plan = cell["initialization"]
    steps = []
    for index, entry in enumerate(plan["steps"]):
        step = dict(_requireObject(entry, f"cell.initialization.steps[{index}]"))
        if "pos" in step:
            position = _requireCoordinate(step["pos"], f"cell.initialization.steps[{index}].pos")
            step["pos"] = list(transformBlockPosition(position, facing, origin))
        steps.append(step)
    requirements = []
    for index, entry in enumerate(plan["externalRequirements"]):
        location = f"cell.initialization.externalRequirements[{index}]"
        requirements.append(dict(_requireObject(entry, location)))
    initialization = {
        "format": plan["format"], "formatVersion": plan["formatVersion"],
        "strategy": plan["strategy"], "steps": steps,
        "externalRequirements": requirements,
        "instanceId": instanceId,
    }

    return {
        "instanceId": instanceId,
        "blocks": blocks,
        "pins": pins,
        "bounds": {"min": list(bounds[0]), "max": list(bounds[1])},
        "keepouts": [{"min": list(box[0]), "max": list(box[1])} for box in keepouts],
        "initialization": initialization,
    }
