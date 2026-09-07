import type { Point } from "./layout";
export function roundedPath(points: Point[]): string {
  if (!points.length) return "";
  let path = `M ${points[0].x} ${points[0].y}`;
  for (let i = 1; i < points.length - 1; i++) {
    const a = points[i - 1],
      b = points[i],
      c = points[i + 1];
    const before = Math.hypot(b.x - a.x, b.y - a.y),
      after = Math.hypot(c.x - b.x, c.y - b.y);
    if (!before || !after) continue;
    const radius = Math.min(9, before / 2, after / 2);
    const start = {
      x: b.x + ((a.x - b.x) * radius) / before,
      y: b.y + ((a.y - b.y) * radius) / before,
    };
    const end = {
      x: b.x + ((c.x - b.x) * radius) / after,
      y: b.y + ((c.y - b.y) * radius) / after,
    };
    path += ` L ${start.x} ${start.y} Q ${b.x} ${b.y} ${end.x} ${end.y}`;
  }
  const last = points.at(-1)!;
  return `${path} L ${last.x} ${last.y}`;
}
