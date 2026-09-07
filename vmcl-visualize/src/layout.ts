import type { ELK, ElkNode } from "elkjs/lib/elk-api";
import {
  category,
  graphLinks,
  type LogicGraph,
  type VisualLink,
} from "./model";
export interface Point {
  x: number;
  y: number;
}
export interface LayoutNode {
  id: number;
  x: number;
  y: number;
  width: number;
  height: number;
  ports: { id: string; label: string; y: number }[];
  outputY: number;
}
export interface LayoutLink extends VisualLink {
  points: Point[];
}
export interface GraphLayout {
  nodes: LayoutNode[];
  links: LayoutLink[];
}
export async function layoutGraph(
  graph: LogicGraph,
  elk: Pick<ELK, "layout">,
): Promise<GraphLayout> {
  const links = graphLinks(graph);
  const outgoing = new Set(links.map((link) => link.source));
  const incoming = new Map<number, VisualLink[]>();
  for (const link of links) {
    const list = incoming.get(link.target) ?? [];
    list.push(link);
    incoming.set(link.target, list);
  }
  const nodes: LayoutNode[] = graph.nodes.map((n) => {
    const inputs = incoming.get(n.id) ?? [];
    const height = Math.max(108, 46 + inputs.length * 24);
    return {
      id: n.id,
      x: 0,
      y: 0,
      width: 184,
      height,
      outputY: height / 2,
      ports: inputs.map((link, i) => ({
        id: link.targetHandle,
        label: link.label,
        y: height / 2 + (i - (inputs.length - 1) / 2) * 24,
      })),
    };
  });
  if (!nodes.length) return { nodes: [], links: [] };
  const result = await elk.layout<ElkNode>({
    id: "graph",
    layoutOptions: {
      "elk.algorithm": "layered",
      "elk.direction": "RIGHT",
      "elk.edgeRouting": "ORTHOGONAL",
      "elk.spacing.nodeNode": "48",
      "elk.layered.spacing.nodeNodeBetweenLayers": "88",
      "elk.layered.spacing.edgeNodeBetweenLayers": "28",
      "elk.spacing.edgeNode": "24",
      "elk.padding": "[top=40,left=60,bottom=40,right=60]",
      "elk.randomSeed": "7",
      "elk.layered.considerModelOrder.strategy": "NODES_AND_EDGES",
    },
    children: nodes.map((n) => ({
      id: String(n.id),
      width: n.width,
      height: n.height,
      layoutOptions: {
        "elk.portConstraints": "FIXED_POS",
        ...(category(graph.nodes[n.id]) === "input"
          ? { "elk.layered.layering.layerConstraint": "FIRST" }
          : category(graph.nodes[n.id]) === "output" && !outgoing.has(n.id)
            ? { "elk.layered.layering.layerConstraint": "LAST" }
            : {}),
      },
      ports: [
        ...n.ports.map((p) => ({
          id: `${n.id}-${p.id}`,
          x: 0,
          y: p.y,
          width: 0,
          height: 0,
          layoutOptions: { "elk.port.side": "WEST" },
        })),
        {
          id: `${n.id}-out`,
          x: n.width,
          y: n.outputY,
          width: 0,
          height: 0,
          layoutOptions: { "elk.port.side": "EAST" },
        },
      ],
    })),
    edges: links.map((link) => ({
      id: link.id,
      sources: [`${link.source}-out`],
      targets: [`${link.target}-${link.targetHandle}`],
    })),
  });
  const routes = new Map(
    result.edges?.map((edge) => [edge.id, edge.sections?.[0]]),
  );
  return {
    nodes: nodes.map((n, i) => ({
      ...n,
      x: result.children![i].x ?? 0,
      y: result.children![i].y ?? 0,
    })),
    links: links.map((link) => {
      const route = routes.get(link.id);
      return {
        ...link,
        points: route
          ? [route.startPoint, ...(route.bendPoints ?? []), route.endPoint]
          : [],
      };
    }),
  };
}
