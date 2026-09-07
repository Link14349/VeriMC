import { memo } from "react";
import {
  BaseEdge,
  EdgeLabelRenderer,
  Handle,
  Position,
  getSmoothStepPath,
  type Edge,
  type EdgeProps,
  type Node,
  type NodeProps,
} from "@xyflow/react";
import {
  ArrowDownToLine,
  ArrowUpFromLine,
  Binary,
  Braces,
  GitBranch,
  Layers3,
} from "lucide-react";
import {
  category,
  categoryColors,
  nodeName,
  operationLabels,
  typeLabel,
  type LogicNode,
} from "./model";
import type { LayoutNode, LayoutLink } from "./layout";
import { roundedPath } from "./geometry";
export type CircuitNode = Node<
  { logical: LogicNode; layout: LayoutNode; muted: boolean },
  "circuit"
>;
export type CircuitEdge = Edge<
  { link: LayoutLink; muted: boolean; showLabels: boolean },
  "circuit"
>;
export const CircuitNodeView = memo(function CircuitNodeView({
  data,
  selected,
}: NodeProps<CircuitNode>) {
  const n = data.logical;
  const kind = category(n);
  const l = data.layout;
  const Icon = {
    input: ArrowDownToLine,
    output: ArrowUpFromLine,
    register: Layers3,
    constant: Binary,
    signal: GitBranch,
    operation: Braces,
  }[kind];
  return (
    <div
      className={`circuitNode ${kind} ${selected ? "chosen" : ""} ${data.muted ? "muted" : ""}`}
      style={
        {
          width: l.width,
          height: l.height,
          "--nodeColor": categoryColors[kind],
        } as React.CSSProperties
      }
    >
      <div className="nodeCaption">
        <Icon size={12} />
        <span>{kind === "output" ? "OUTPUT" : operationLabels[n.op]}</span>
        <small>#{n.id}</small>
      </div>
      <div className="nodeBody">
        <strong title={nodeName(n)}>{nodeName(n)}</strong>
        <span>{typeLabel(n.type)}</span>
      </div>
      {l.ports.map((port) => (
        <div key={port.id}>
          <Handle
            type="target"
            position={Position.Left}
            id={port.id}
            style={{ top: port.y }}
            isConnectable={false}
          />
          <span className="handleLabel" style={{ top: port.y - 6 }}>
            {port.label}
          </span>
        </div>
      ))}
      <Handle
        type="source"
        position={Position.Right}
        id="out"
        style={{ top: l.outputY }}
        isConnectable={false}
      />
    </div>
  );
});
export function CircuitEdgeView(props: EdgeProps<CircuitEdge>) {
  const {
    sourceX,
    sourceY,
    targetX,
    targetY,
    sourcePosition,
    targetPosition,
    data,
    markerEnd,
    selected,
  } = props;
  const link = data!.link;
  const points = link.points;
  const first = points[0],
    last = points.at(-1);
  // ELK routes apply until a node is dragged. React Flow reroutes moved endpoints immediately.
  const routeMatches =
    first &&
    last &&
    Math.abs(first.x - sourceX) < 2 &&
    Math.abs(first.y - sourceY) < 2 &&
    Math.abs(last.x - targetX) < 2 &&
    Math.abs(last.y - targetY) < 2;
  const [fallback, x, y] = getSmoothStepPath({
    sourceX,
    sourceY,
    targetX,
    targetY,
    sourcePosition,
    targetPosition,
    borderRadius: 9,
    offset: 32,
  });
  const path = routeMatches ? roundedPath(points) : fallback;
  let labelX = x,
    labelY = y;
  if (routeMatches) {
    let longest = 0;
    for (let i = 1; i < points.length; i++) {
      const distance = Math.abs(points[i].x - points[i - 1].x);
      if (distance > longest) {
        longest = distance;
        labelX = (points[i].x + points[i - 1].x) / 2;
        labelY = (points[i].y + points[i - 1].y) / 2;
      }
    }
  }
  const color = selected
    ? "#bb4938"
    : link.kind === "clock"
      ? "#b48b43"
      : link.kind === "reset"
        ? "#b86b78"
        : "#8997a5";
  return (
    <g className={data?.muted ? "mutedEdge" : ""}>
      <BaseEdge
        id={props.id}
        path={path}
        markerEnd={markerEnd}
        interactionWidth={18}
        style={{
          stroke: color,
          strokeWidth: selected ? 2.5 : 1.6,
          strokeDasharray: link.kind === "data" ? undefined : "6 4",
        }}
      />
      {data?.showLabels &&
        (link.width > 1 || link.kind !== "data") &&
        !data.muted && (
          <EdgeLabelRenderer>
            <div
              className="wireLabel nodrag nopan"
              style={{
                transform: `translate(-50%, -50%) translate(${labelX}px,${labelY}px)`,
                color,
              }}
            >
              {link.kind === "clock"
                ? "CLK"
                : link.kind === "reset"
                  ? "RST"
                  : `${link.width} bit`}
            </div>
          </EdgeLabelRenderer>
        )}
    </g>
  );
}
