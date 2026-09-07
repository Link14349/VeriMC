import { parse, visit, type ParseError } from "jsonc-parser";
import {
  operations,
  typeBits,
  type LogicGraph,
  type LogicNode,
  type LogicType,
  type Operation,
  type SourceSpan,
  type Attributes,
} from "./model";

export const viewerLimits = {
  fileBytes: 16 * 1024 * 1024,
  nodes: 3000,
  links: 15000,
  bits: 10000000,
};
type ObjectValue = Record<string, unknown>;
function invalid(message: string): never {
  throw new Error(message);
}
function object(value: unknown, label: string, keys?: string[]): ObjectValue {
  if (value === null || typeof value !== "object" || Array.isArray(value))
    invalid(`${label} 必须是对象。`);
  const result = value as ObjectValue;
  if (
    keys &&
    (Object.keys(result).length !== keys.length ||
      keys.some((key) => !Object.hasOwn(result, key)))
  )
    invalid(`${label} 有缺失或未知字段。`);
  return result;
}
function list(value: unknown, label: string, max = 100000): unknown[] {
  if (!Array.isArray(value) || value.length > max)
    invalid(`${label} 不是有效数组或超出显示预算（${max}）。`);
  return value;
}
function string(value: unknown, label: string): string {
  if (typeof value !== "string") invalid(`${label} 必须是字符串。`);
  return value;
}
function integer(
  value: unknown,
  label: string,
  max = 1000000000,
  min = 0,
): number {
  if (
    typeof value !== "number" ||
    !Number.isSafeInteger(value) ||
    value < min ||
    value > max
  )
    invalid(`${label} 必须是 ${min}–${max} 内的整数。`);
  return value;
}
function decimal(value: unknown, label: string): bigint {
  const text = string(value, label);
  if (text.length > 130000 || !/^(0|[1-9][0-9]*|-[1-9][0-9]*)$/.test(text))
    invalid(`${label} 必须是规范十进制字符串。`);
  return BigInt(text);
}
function readType(value: unknown, isElement = false): LogicType {
  const t = object(value, "类型");
  const kind = string(t.kind, "类型 kind");
  if (kind === "array") {
    object(t, "数组类型", ["kind", "width", "length", "element"]);
    if (isElement || t.width !== 0) invalid("不支持嵌套数组或非零数组 width。");
    const element = readType(t.element, true);
    if (element.kind === "clock") invalid("不支持时钟数组。");
    return {
      kind,
      width: 0,
      length: integer(t.length, "数组长度", 65536, 1),
      element,
    };
  }
  const width = integer(t.width, "位宽", 4096, 1);
  if (kind === "enum") {
    object(t, "枚举类型", ["kind", "width", "identity", "members"]);
    const members = list(t.members, "枚举成员", 65536).map((item) =>
      string(item, "枚举成员"),
    );
    if (
      members.length < 2 ||
      new Set(members).size !== members.length ||
      width !== Math.ceil(Math.log2(members.length))
    )
      invalid("枚举成员或编码位宽无效。");
    const identity = string(t.identity, "枚举身份");
    if (!identity) invalid("枚举身份为空。");
    return { kind, width, identity, members };
  }
  object(t, "标量类型", ["kind", "width"]);
  if (!["bit", "bits", "uint", "int", "level", "clock"].includes(kind))
    invalid(`不支持类型「${kind}」。`);
  if (
    (["bit", "clock"].includes(kind) && width !== 1) ||
    (kind === "level" && width !== 4)
  )
    invalid(`${kind} 的位宽无效。`);
  return {
    kind: kind as "bit" | "bits" | "uint" | "int" | "level" | "clock",
    width,
  };
}
function equalType(a: LogicType, b: LogicType): boolean {
  if (a.kind !== b.kind || a.width !== b.width) return false;
  if (a.kind === "array" && b.kind === "array")
    return a.length === b.length && equalType(a.element, b.element);
  if (a.kind === "enum" && b.kind === "enum")
    return (
      a.identity === b.identity &&
      a.members.length === b.members.length &&
      a.members.every((m, i) => m === b.members[i])
    );
  return true;
}
function acceptsRange(
  container: LogicType,
  view: LogicType,
  offset: number,
  width: number,
): boolean {
  if (offset === 0 && equalType(container, view)) return true;
  if (["bits", "uint", "int"].includes(container.kind))
    return (
      (view.kind === "bit" && width === 1) ||
      (view.kind === "bits" && width === view.width)
    );
  if (container.kind === "array") {
    const bits = typeBits(container.element);
    return (
      (offset % bits) + width <= bits &&
      acceptsRange(container.element, view, offset % bits, width)
    );
  }
  return false;
}
function checkConstant(value: bigint, type: LogicType) {
  const width = BigInt(typeBits(type));
  const min = type.kind === "int" ? -(1n << (width - 1n)) : 0n;
  const max =
    type.kind === "int"
      ? (1n << (width - 1n)) - 1n
      : type.kind === "enum"
        ? BigInt(type.members.length - 1)
        : (1n << width) - 1n;
  if (type.kind === "clock" || value < min || value > max)
    invalid("常量或复位值超出类型范围。");
  if (type.kind === "array" && type.element.kind === "enum") {
    const mask = (1n << BigInt(type.element.width)) - 1n;
    for (let i = 0; i < type.length; i++)
      if (
        ((value >> BigInt(i * type.element.width)) & mask) >=
        BigInt(type.element.members.length)
      )
        invalid("数组含有未定义的枚举编码。");
  }
}
function readAttributes(
  op: Operation,
  raw: unknown,
  type: LogicType,
): Attributes {
  const a = object(raw, `${op} 属性`);
  if (op === "input" || op === "signal") {
    object(a, "信号属性", ["name", "role"]);
    const role = string(a.role, "信号角色");
    if (
      !["input", "output", "wire"].includes(role) ||
      (op === "input" && role !== "input")
    )
      invalid("信号角色无效。");
    return {
      kind: "signal",
      name: string(a.name, "信号名"),
      role: role as "input" | "output" | "wire",
    };
  }
  if (op === "register") {
    object(a, "寄存器属性", ["name", "role", "resetValue", "initialState"]);
    if (a.role !== "register" || a.initialState !== "invalid")
      invalid("寄存器状态协议无效。");
    const resetValue = decimal(a.resetValue, "复位值");
    checkConstant(resetValue, type);
    return { kind: "register", name: string(a.name, "寄存器名"), resetValue };
  }
  if (op === "constant") {
    object(a, "常量属性", ["value"]);
    const value = decimal(a.value, "常量");
    checkConstant(value, type);
    return { kind: "constant", value };
  }
  if (op === "slice") {
    object(a, "切片属性", ["offset"]);
    return {
      kind: "slice",
      offset: integer(a.offset, "切片偏移", viewerLimits.bits),
    };
  }
  if (op === "shiftLeft" || op === "shiftRight") {
    object(a, "移位属性", ["amount"]);
    return {
      kind: "shift",
      amount: integer(a.amount, "移位量", typeBits(type)),
    };
  }
  object(a, "运算属性", []);
  return { kind: "empty" };
}
function signatures(nodes: LogicNode[]) {
  const numeric = (t: LogicType) => t.kind === "uint" || t.kind === "int";
  const word = (t: LogicType) => numeric(t) || t.kind === "bits";
  const digital = (t: LogicType) => word(t) || t.kind === "bit";
  const selectable = (t: LogicType) =>
    t.kind !== "clock" &&
    t.kind !== "level" &&
    !(t.kind === "array" && t.element.kind === "level");
  for (const n of nodes) {
    const inputs = n.inputs.map((id) => nodes[id].type);
    const t = n.type;
    const op = n.op;
    const arity = ["input", "signal", "constant"].includes(op)
      ? 0
      : ["register", "mux"].includes(op)
        ? 3
        : [
              "slice",
              "shiftLeft",
              "shiftRight",
              "bitNot",
              "logicalNot",
              "neg",
              "reinterpret",
              "widen",
              "low",
            ].includes(op)
          ? 1
          : ["concat", "array"].includes(op)
            ? null
            : 2;
    if (arity !== null && inputs.length !== arity)
      invalid(`节点 #${n.id}（${op}）的输入数量错误。`);
    const same = (...indices: number[]) =>
      indices.every((i) => equalType(inputs[i], t));
    let valid = true;
    switch (op) {
      case "register":
        valid =
          same(0) &&
          inputs[1].kind === "clock" &&
          inputs[2].kind === "bit" &&
          selectable(t);
        break;
      case "mux":
        valid = same(1, 2) && inputs[0].kind === "bit" && selectable(t);
        break;
      case "add":
      case "sub":
        valid =
          numeric(inputs[0]) &&
          equalType(inputs[0], inputs[1]) &&
          t.kind === (op === "sub" ? "int" : inputs[0].kind) &&
          t.width === inputs[0].width + 1;
        break;
      case "wrapAdd":
      case "wrapSub":
        valid = numeric(t) && same(0, 1);
        break;
      case "and":
      case "or":
      case "xor":
        valid = digital(t) && same(0, 1);
        break;
      case "logicalAnd":
      case "logicalOr":
        valid = t.kind === "bit" && same(0, 1);
        break;
      case "eq":
      case "ne":
      case "lt":
      case "le":
      case "gt":
      case "ge":
        valid =
          t.kind === "bit" &&
          equalType(inputs[0], inputs[1]) &&
          (["eq", "ne"].includes(op)
            ? digital(inputs[0]) || inputs[0].kind === "enum"
            : numeric(inputs[0]));
        break;
      case "bitNot":
      case "logicalNot":
        valid = same(0) && (op === "bitNot" ? digital(t) : t.kind === "bit");
        break;
      case "shiftLeft":
      case "shiftRight":
        valid = same(0) && word(t);
        break;
      case "slice": {
        const offset = n.attributes.kind === "slice" ? n.attributes.offset : 0;
        valid =
          offset + typeBits(t) <= typeBits(inputs[0]) &&
          inputs[0].kind !== "clock" &&
          acceptsRange(inputs[0], t, offset, typeBits(t));
        break;
      }
      case "neg":
        valid =
          numeric(inputs[0]) &&
          t.kind === "int" &&
          t.width === inputs[0].width + 1;
        break;
      case "reinterpret":
        valid =
          digital(t) &&
          digital(inputs[0]) &&
          typeBits(t) === typeBits(inputs[0]);
        break;
      case "widen":
        valid =
          numeric(t) && t.kind === inputs[0].kind && t.width >= inputs[0].width;
        break;
      case "low":
        valid =
          t.kind === "bits" && word(inputs[0]) && t.width <= inputs[0].width;
        break;
      case "concat":
        valid =
          inputs.length >= 2 &&
          t.kind === "bits" &&
          inputs.every((i) => ["bit", "bits"].includes(i.kind)) &&
          inputs.reduce((sum, i) => sum + typeBits(i), 0) === typeBits(t);
        break;
      case "array":
        valid =
          t.kind === "array" &&
          t.length === inputs.length &&
          inputs.every((i) => equalType(i, t.element));
        break;
    }
    if (!valid) invalid(`节点 #${n.id}（${op}）的输入/输出类型不匹配。`);
  }
}
export function readVmcl(text: string): LogicGraph {
  if (new TextEncoder().encode(text).length > viewerLimits.fileBytes)
    invalid("文件超过 16 MiB；请先导出较小的模块。");
  const stack: (Set<string> | null)[] = [];
  visit(
    text,
    {
      onObjectBegin: () => {
        stack.push(new Set());
        if (stack.length > 128) invalid("JSON 嵌套超过 128 层。");
      },
      onObjectProperty: (property) => {
        const keys = stack.at(-1)!;
        if (keys?.has(property)) invalid(`JSON 含有重复字段「${property}」。`);
        keys?.add(property);
      },
      onObjectEnd: () => {
        stack.pop();
      },
      onArrayBegin: () => {
        stack.push(null);
        if (stack.length > 128) invalid("JSON 嵌套超过 128 层。");
      },
      onArrayEnd: () => {
        stack.pop();
      },
    },
    { disallowComments: true, allowTrailingComma: false },
  );
  const errors: ParseError[] = [];
  const raw: unknown = parse(text, errors, {
    disallowComments: true,
    allowTrailingComma: false,
  });
  if (errors.length) invalid(`JSON 解析失败，位置 ${errors[0].offset}。`);
  const g = object(raw, "文件");
  if (
    g.format !== "verimc.logic" ||
    g.formatVersion !== 1 ||
    g.languageVersion !== "0.1"
  )
    invalid("仅支持 verimc.logic 格式、.vmcl v1、语言版本 0.1。");
  object(g, "文件", [
    "format",
    "formatVersion",
    "languageVersion",
    "compilerVersion",
    "top",
    "sources",
    "instances",
    "ports",
    "nodes",
    "connections",
    "semantics",
    "limits",
    "validation",
  ]);
  const semantics = object(g.semantics, "状态协议", [
    "clockEdge",
    "reset",
    "stateUpdate",
    "initialRegisters",
  ]);
  if (
    semantics.clockEdge !== "rising" ||
    semantics.reset !== "synchronousHigh" ||
    semantics.stateUpdate !== "simultaneous" ||
    semantics.initialRegisters !== "invalid"
  )
    invalid("不支持该寄存器状态协议。");
  const validation = object(g.validation, "产物类别", [
    "stage",
    "physicalVerified",
    "sourceTestsExecuted",
  ]);
  if (
    validation.stage !== "logicalGraph" ||
    validation.physicalVerified !== false ||
    validation.sourceTestsExecuted !== false
  )
    invalid("不支持该产物类别声明。");
  const limits = object(g.limits, "编译预算", [
    "maxNodes",
    "maxBits",
    "maxInstances",
    "maxSteps",
    "maxDepth",
    "maxSourceBytes",
    "maxWidth",
    "maxArrayLength",
    "maxIntegerBits",
  ]);
  for (const [name, value] of Object.entries(limits))
    integer(value, name, 1000000000, 1);
  if (
    limits.maxWidth !== 4096 ||
    limits.maxArrayLength !== 65536 ||
    limits.maxIntegerBits !== 65536
  )
    invalid("类型预算与 v1 不一致。");
  const sourcePaths = new Map<string, number>();
  const sources = list(g.sources, "源文件").map((rawSource) => {
    const s = object(rawSource, "源文件", ["path", "sha256", "byteLength"]);
    const path = string(s.path, "源文件路径");
    if (
      !path ||
      path.startsWith("/") ||
      path.includes("\\") ||
      path.split("/").some((part) => !part || part === ".." || part === ".") ||
      sourcePaths.has(path)
    )
      invalid("源文件路径无效或重复。");
    const sha256 = string(s.sha256, "源文件哈希");
    if (!/^[0-9a-f]{64}$/.test(sha256)) invalid("源文件 SHA-256 无效。");
    const byteLength = integer(s.byteLength, "源码长度", 64 * 1024 * 1024);
    sourcePaths.set(path, byteLength);
    return { path, sha256, byteLength };
  });
  const span = (value: unknown): SourceSpan => {
    const s = object(value, "源码位置", [
      "file",
      "start",
      "end",
      "line",
      "column",
    ]);
    const file = string(s.file, "源码文件");
    if (!sourcePaths.has(file))
      invalid(`源码位置引用不存在的文件「${file}」。`);
    const start = integer(s.start, "源码起点", sourcePaths.get(file));
    const end = integer(s.end, "源码终点", sourcePaths.get(file), start);
    return {
      file,
      start,
      end,
      line: integer(s.line, "行号", 64 * 1024 * 1024, 1),
      column: integer(s.column, "列号", 64 * 1024 * 1024, 1),
    };
  };
  const instancePaths = new Set<string>();
  const instances = list(g.instances, "实例").map((rawInstance) => {
    const i = object(rawInstance, "实例", [
      "path",
      "module",
      "parameters",
      "source",
    ]);
    const path = string(i.path, "实例路径");
    if (instancePaths.has(path)) invalid("模块实例路径重复。");
    instancePaths.add(path);
    const parameters = Object.fromEntries(
      Object.entries(object(i.parameters, "实例参数")).map(([key, value]) => [
        key,
        decimal(value, "实例参数"),
      ]),
    );
    return {
      path,
      module: string(i.module, "模块名"),
      parameters,
      source: span(i.source),
    };
  });
  const rawNodes = list(g.nodes, "节点", viewerLimits.nodes);
  let bits = 0;
  let linkCount = 0;
  const nodes = rawNodes.map((rawNode, index): LogicNode => {
    const n = object(rawNode, "节点", [
      "id",
      "op",
      "type",
      "inputs",
      "attributes",
      "source",
      "instance",
    ]);
    if (n.id !== index) invalid("节点 ID 必须连续且与数组下标一致。");
    const op = string(n.op, "操作");
    if (!(operations as readonly string[]).includes(op))
      invalid(`未知操作「${op}」。`);
    const type = readType(n.type);
    bits += typeBits(type);
    if (bits > viewerLimits.bits || bits > Number(limits.maxBits))
      invalid("逻辑图总位数超出预算。");
    const inputs = list(n.inputs, "节点输入", viewerLimits.links).map((id) =>
      integer(id, "输入节点引用", rawNodes.length - 1),
    );
    linkCount += inputs.length;
    return {
      id: index,
      op: op as Operation,
      type,
      inputs,
      attributes: readAttributes(op as Operation, n.attributes, type),
      source: span(n.source),
      instance: string(n.instance, "展开路径"),
    };
  });
  if (
    nodes.length > Number(limits.maxNodes) ||
    instances.length > Number(limits.maxInstances)
  )
    invalid("图规模超出声明的编译预算。");
  signatures(nodes);
  const ranges: { start: number; end: number }[][] = nodes.map(() => []);
  const connections = list(g.connections, "显式连接", viewerLimits.links).map(
    (rawEdge) => {
      const e = object(rawEdge, "连接", [
        "source",
        "target",
        "sourceOffset",
        "targetOffset",
        "width",
        "type",
        "sourceSpan",
      ]);
      const source = integer(e.source, "连接源", nodes.length - 1),
        target = integer(e.target, "连接目标", nodes.length - 1);
      const sourceOffset = integer(e.sourceOffset, "源偏移", 0),
        targetOffset = integer(
          e.targetOffset,
          "目标偏移",
          typeBits(nodes[target].type),
        );
      const width = integer(
        e.width,
        "连接位数",
        typeBits(nodes[target].type) - targetOffset,
        1,
      );
      const type = readType(e.type);
      if (
        nodes[target].op !== "signal" ||
        typeBits(type) !== width ||
        !equalType(type, nodes[source].type) ||
        !acceptsRange(nodes[target].type, type, targetOffset, width)
      )
        invalid("连接范围或信号类型不匹配。");
      ranges[target].push({ start: targetOffset, end: targetOffset + width });
      return {
        source,
        target,
        sourceOffset,
        targetOffset,
        width,
        type,
        sourceSpan: span(e.sourceSpan),
      };
    },
  );
  for (const n of nodes)
    if (n.op === "signal") {
      let end = 0;
      for (const range of ranges[n.id].sort((a, b) => a.start - b.start)) {
        if (range.start !== end) invalid(`信号 #${n.id} 存在缺失或重叠驱动。`);
        end = range.end;
      }
      if (end !== typeBits(n.type)) invalid(`信号 #${n.id} 未完整驱动。`);
    }
  linkCount += connections.length;
  if (linkCount > viewerLimits.links)
    invalid(`超过 ${viewerLimits.links} 条连线的显示预算。`);
  const portNames = new Set<string>(),
    topInputs = new Set<number>();
  const ports = list(g.ports, "端口").map((rawPort) => {
    const p = object(rawPort, "端口", ["name", "direction", "node"]);
    const name = string(p.name, "端口名");
    if (portNames.has(name)) invalid("顶层端口名重复。");
    portNames.add(name);
    const node = integer(p.node, "端口引用", nodes.length - 1);
    const direction = string(p.direction, "端口方向");
    if (!["input", "output"].includes(direction)) invalid("端口方向无效。");
    const attrs = nodes[node].attributes;
    if (
      direction === "input"
        ? nodes[node].op !== "input" || topInputs.has(node)
        : nodes[node].op !== "signal" ||
          attrs.kind !== "signal" ||
          attrs.role !== "output"
    )
      invalid("端口与节点角色不匹配。");
    if (direction === "input") topInputs.add(node);
    return { name, direction: direction as "input" | "output", node };
  });
  if (nodes.some((n) => n.op === "input" && !topInputs.has(n.id)))
    invalid("存在未声明的顶层输入。");
  return {
    top: string(g.top, "顶层模块"),
    compilerVersion: string(g.compilerVersion, "编译器版本"),
    languageVersion: "0.1",
    nodes,
    connections,
    ports,
    sources,
    instances,
  };
}
