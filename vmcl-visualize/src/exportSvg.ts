import {
  category,
  categoryColors,
  nodeName,
  typeLabel,
  operationLabels,
  type LogicGraph,
} from "./model";
import type { GraphLayout } from "./layout";
import { roundedPath } from "./geometry";
const escape = (value: string) =>
  value.replace(
    /[<>&"']/g,
    (c) =>
      ({
        "<": "&lt;",
        ">": "&gt;",
        "&": "&amp;",
        '"': "&quot;",
        "'": "&apos;",
      })[c]!,
  );
export function graphSvg(graph: LogicGraph, layout: GraphLayout): string {
  let width = 700,
    height = 240;
  for (const node of layout.nodes) {
    width = Math.max(width, node.x + node.width + 60);
    height = Math.max(height, node.y + node.height + 40);
  }
  for (const link of layout.links)
    for (const point of link.points) {
      width = Math.max(width, point.x + 60);
      height = Math.max(height, point.y + 40);
    }
  const edges = layout.links
    .map(
      (link) =>
        `<path d="${roundedPath(link.points)}" fill="none" stroke="${link.kind === "clock" ? "#b48b43" : link.kind === "reset" ? "#b86b78" : "#8997a5"}" stroke-width="1.6" ${link.kind !== "data" ? 'stroke-dasharray="6 4"' : ""} marker-end="url(#arrow)"/>`,
    )
    .join("");
  const nodes = layout.nodes
    .map((n) => {
      const node = graph.nodes[n.id],
        color = categoryColors[category(node)];
      return `<g transform="translate(${n.x} ${n.y})"><rect width="${n.width}" height="${n.height}" rx="8" fill="white" stroke="${color}"/><path d="M1 29h${n.width - 2}" stroke="#e8e9e7"/><text x="12" y="19" font-size="10" fill="${color}">${escape(operationLabels[node.op])} · #${node.id}</text><text x="16" y="59" font-size="15" fill="#273240">${escape(nodeName(node).slice(0, 22))}</text><text x="16" y="80" font-size="11" fill="#657382">${escape(typeLabel(node.type).slice(0, 26))}</text>${n.ports.map((p) => `<circle cx="0" cy="${p.y}" r="3" fill="white" stroke="${color}"/><text x="-10" y="${p.y + 3}" text-anchor="end" font-size="9" fill="#657382">${escape(p.label)}</text>`).join("")}<circle cx="${n.width}" cy="${n.outputY}" r="3" fill="white" stroke="${color}"/></g>`;
    })
    .join("");
  return `<svg xmlns="http://www.w3.org/2000/svg" width="${width}" height="${height + 74}" viewBox="0 0 ${width} ${height + 74}"><title>${escape(graph.top)} — VMCL logic graph</title><defs><marker id="arrow" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="5" markerHeight="5" orient="auto-start-reverse"><path d="M0 0L10 5L0 10" fill="#8997a5"/></marker></defs><rect width="100%" height="100%" fill="#f9faf8"/><g font-family="ui-monospace, monospace"><text x="36" y="35" fill="#273240" font-size="18">${escape(graph.top)}</text><text x="36" y="56" fill="#657382" font-size="11">VMCL v1 · ${graph.nodes.length} nodes · ${layout.links.length} links · logical structure</text><g transform="translate(0 74)">${edges}${nodes}</g></g></svg>`;
}
