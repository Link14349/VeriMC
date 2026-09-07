// Browser-side logical model. File decoding and screen coordinates are separate adapters.
export const operations = [
  "input",
  "signal",
  "register",
  "constant",
  "slice",
  "shiftLeft",
  "shiftRight",
  "mux",
  "add",
  "sub",
  "wrapAdd",
  "wrapSub",
  "and",
  "or",
  "xor",
  "logicalAnd",
  "logicalOr",
  "eq",
  "ne",
  "lt",
  "le",
  "gt",
  "ge",
  "bitNot",
  "logicalNot",
  "neg",
  "reinterpret",
  "widen",
  "low",
  "concat",
  "array",
] as const;
export type Operation = (typeof operations)[number];
export type LogicType =
  | { kind: "bit" | "bits" | "uint" | "int" | "level" | "clock"; width: number }
  | { kind: "enum"; width: number; identity: string; members: string[] }
  | { kind: "array"; width: 0; length: number; element: LogicType };
export interface SourceSpan {
  file: string;
  start: number;
  end: number;
  line: number;
  column: number;
}
export type Attributes =
  | { kind: "signal"; name: string; role: "input" | "output" | "wire" }
  | { kind: "register"; name: string; resetValue: bigint }
  | { kind: "constant"; value: bigint }
  | { kind: "slice"; offset: number }
  | { kind: "shift"; amount: number }
  | { kind: "empty" };
export interface LogicNode {
  id: number;
  op: Operation;
  type: LogicType;
  inputs: number[];
  attributes: Attributes;
  source: SourceSpan;
  instance: string;
}
export interface Connection {
  source: number;
  target: number;
  sourceOffset: number;
  targetOffset: number;
  width: number;
  type: LogicType;
  sourceSpan: SourceSpan;
}
export interface LogicGraph {
  top: string;
  compilerVersion: string;
  languageVersion: string;
  nodes: LogicNode[];
  connections: Connection[];
  ports: { name: string; direction: "input" | "output"; node: number }[];
  instances: {
    path: string;
    module: string;
    parameters: Record<string, bigint>;
    source: SourceSpan;
  }[];
  sources: { path: string; sha256: string; byteLength: number }[];
}
export type NodeCategory =
  | "input"
  | "output"
  | "register"
  | "constant"
  | "signal"
  | "operation";
export function category(node: LogicNode): NodeCategory {
  if (node.op === "signal")
    return node.attributes.kind === "signal" &&
      node.attributes.role === "output"
      ? "output"
      : "signal";
  return ["input", "register", "constant"].includes(node.op)
    ? (node.op as NodeCategory)
    : "operation";
}
export const categoryLabels: Record<NodeCategory, string> = {
  input: "输入",
  output: "输出",
  register: "寄存器",
  constant: "常量",
  signal: "信号",
  operation: "运算",
};
export const categoryColors: Record<NodeCategory, string> = {
  input: "#477b72",
  output: "#4274a1",
  register: "#b44b39",
  constant: "#9a8057",
  signal: "#738091",
  operation: "#6e7191",
};
export const operationLabels: Record<Operation, string> = {
  input: "INPUT",
  signal: "SIGNAL",
  register: "REGISTER",
  constant: "CONST",
  slice: "SLICE",
  shiftLeft: "SHL",
  shiftRight: "SHR",
  mux: "MUX",
  add: "ADD",
  sub: "SUB",
  wrapAdd: "WRAP ADD",
  wrapSub: "WRAP SUB",
  and: "AND",
  or: "OR",
  xor: "XOR",
  logicalAnd: "LOGICAL AND",
  logicalOr: "LOGICAL OR",
  eq: "EQ",
  ne: "NE",
  lt: "LT",
  le: "LE",
  gt: "GT",
  ge: "GE",
  bitNot: "NOT",
  logicalNot: "LOGICAL NOT",
  neg: "NEG",
  reinterpret: "REINTERPRET",
  widen: "WIDEN",
  low: "LOW",
  concat: "CONCAT",
  array: "ARRAY",
};
export function typeBits(type: LogicType): number {
  return type.kind === "array"
    ? type.length * typeBits(type.element)
    : type.width;
}
export function typeLabel(type: LogicType): string {
  if (type.kind === "array")
    return `[${typeLabel(type.element)}; ${type.length}]`;
  if (type.kind === "enum") return type.identity.split("::").at(-1)!;
  return ["uint", "int", "bits"].includes(type.kind)
    ? `${type.kind}<${type.width}>`
    : type.kind;
}
export function nodeName(node: LogicNode): string {
  const a = node.attributes;
  if (a.kind === "signal" || a.kind === "register")
    return a.name.split(".").at(-1)!;
  if (a.kind === "constant") {
    const value = a.value.toString();
    return value.length > 18 ? `${value.slice(0, 15)}…` : value;
  }
  return (
    {
      wrapAdd: "回绕加法",
      wrapSub: "回绕减法",
      mux: "选择器",
      add: "加法",
      sub: "减法",
      slice: "位段提取",
      concat: "位拼接",
      bitNot: "按位取反",
      array: "数组构造",
    }[node.op as string] ?? operationLabels[node.op]
  );
}
export function inputLabel(op: Operation, index: number): string {
  if (op === "register") return ["D", "CLK", "RST"][index];
  if (op === "mux") return ["SEL", "TRUE", "FALSE"][index];
  if (op === "concat" || op === "array") return `[${index}]`;
  return ["A", "B"][index] ?? `IN ${index}`;
}
export interface VisualLink {
  id: string;
  source: number;
  target: number;
  targetHandle: string;
  label: string;
  width: number;
  kind: "data" | "clock" | "reset";
  connection?: number;
  inputIndex?: number;
}
export function graphLinks(graph: LogicGraph): VisualLink[] {
  return [
    ...graph.nodes.flatMap((node) =>
      node.inputs.map((source, i) => ({
        id: `input-${node.id}-${i}`,
        source,
        target: node.id,
        targetHandle: `in-${i}`,
        label: inputLabel(node.op, i),
        width: typeBits(graph.nodes[source].type),
        kind:
          node.op === "register" && i === 1
            ? ("clock" as const)
            : node.op === "register" && i === 2
              ? ("reset" as const)
              : ("data" as const),
        inputIndex: i,
      })),
    ),
    ...graph.connections.map((edge, i) => ({
      id: `connection-${i}`,
      source: edge.source,
      target: edge.target,
      targetHandle: `drive-${i}`,
      label:
        edge.width === typeBits(graph.nodes[edge.target].type)
          ? "IN"
          : edge.width === 1
            ? `[${edge.targetOffset}]`
            : `[${edge.targetOffset + edge.width - 1}:${edge.targetOffset}]`,
      width: edge.width,
      kind:
        graph.nodes[edge.source].type.kind === "clock"
          ? ("clock" as const)
          : ("data" as const),
      connection: i,
    })),
  ];
}
export function neighbors(
  links: VisualLink[],
  id: number,
  direction: "both" | "upstream" | "downstream",
): Set<number> {
  const result = new Set([id]);
  for (const edge of links) {
    if (direction !== "downstream" && edge.target === id)
      result.add(edge.source);
    if (direction !== "upstream" && edge.source === id) result.add(edge.target);
  }
  return result;
}
