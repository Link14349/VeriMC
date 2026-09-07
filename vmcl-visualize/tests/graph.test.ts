import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import ELK from "elkjs/lib/elk.bundled.js";
import { readVmcl } from "../src/vmclReader";
import { graphLinks, neighbors, typeBits } from "../src/model";
import { layoutGraph } from "../src/layout";
import { graphSvg } from "../src/exportSvg";
const fixture = (name: string) =>
  readFileSync(
    new URL(`../public/examples/${name}.vmcl`, import.meta.url),
    "utf8",
  );
const counter = () => JSON.parse(fixture("counter"));

test("all compiler-produced examples decode, preserving both kinds of links", () => {
  for (const [name, nodes] of [
    ["counter", 8],
    ["adder", 9],
    ["rippleAdder", 63],
    ["signalArray", 14],
  ] as const) {
    const graph = readVmcl(fixture(name));
    assert.equal(graph.nodes.length, nodes);
    const links = graphLinks(graph);
    assert.equal(
      links.length,
      graph.connections.length +
        graph.nodes.reduce((sum, n) => sum + n.inputs.length, 0),
    );
    assert.equal(new Set(links.map((l) => l.id)).size, links.length);
    for (const link of links) {
      assert.ok(graph.nodes[link.source]);
      assert.ok(graph.nodes[link.target]);
    }
  }
});
test("register next/clock/reset and state feedback are represented independently", () => {
  const graph = readVmcl(fixture("counter"));
  const links = graphLinks(graph);
  assert.deepEqual(
    links.filter((l) => l.target === 4).map((l) => [l.source, l.label, l.kind]),
    [
      [7, "D", "data"],
      [0, "CLK", "clock"],
      [1, "RST", "reset"],
    ],
  );
  assert.ok(links.some((l) => l.source === 4 && l.target === 7));
  assert.deepEqual([...neighbors(links, 4, "upstream")].sort(), [0, 1, 4, 7]);
  assert.deepEqual([...neighbors(links, 4, "downstream")].sort(), [3, 4, 6, 7]);
});
test("whole array source and partial target drive stay distinct", () => {
  const graph = readVmcl(fixture("signalArray"));
  const links = graphLinks(graph);
  assert.equal(typeBits(graph.nodes[0].type), 4);
  assert.ok(links.some((l) => l.label === "[3]" && l.width === 1));
});
test("bad format, keys, references, types, controls and drivers fail instead of drawing substitutions", () => {
  const invalid = (change: (g: ReturnType<typeof counter>) => void) => {
    const g = counter();
    change(g);
    assert.throws(() => readVmcl(JSON.stringify(g)));
  };
  invalid((g) => (g.formatVersion = 2));
  invalid((g) => (g.extra = true));
  invalid((g) => (g.nodes[4].op = "unknown"));
  invalid((g) => (g.nodes[4].inputs[0] = 900));
  invalid((g) => (g.nodes[4].inputs[0] = -1));
  invalid((g) => (g.nodes[4].inputs = [7, 0]));
  invalid((g) => (g.nodes[4].inputs[1] = 1));
  invalid((g) => (g.nodes[4].attributes.initialState = "zero"));
  invalid((g) => (g.nodes[4].attributes.resetValue = "16"));
  invalid((g) => (g.nodes[5].attributes.value = 1));
  invalid((g) => (g.nodes[5].attributes.value = "01"));
  invalid((g) => (g.nodes[0].type.width = 4));
  invalid(
    (g) =>
      (g.nodes[5].type = {
        kind: "array",
        width: 0,
        length: 2,
        element: { kind: "array" },
      }),
  );
  invalid((g) => (g.connections[0].targetOffset = 1));
  invalid((g) => g.connections.push(g.connections[0]));
  invalid((g) => (g.connections = []));
  invalid((g) => (g.nodes[0].source.end = 100000));
  invalid((g) => (g.nodes[0].source.file = "absent.vmc"));
  invalid((g) => (g.ports[0].direction = "output"));
  assert.throws(() => readVmcl('{"format":1,"format":2}'), /重复/);
  assert.throws(
    () => readVmcl("[".repeat(130) + "0" + "]".repeat(130)),
    /嵌套/,
  );
  assert.throws(() => readVmcl("{broken"), /JSON/);
});
test("large constant values stay exact BigInts", () => {
  const g = counter();
  g.nodes = [g.nodes[5]];
  g.nodes[0].id = 0;
  g.nodes[0].type = { kind: "uint", width: 256 };
  g.connections = [];
  g.ports = [];
  const value = (1n << 255n) + 17n;
  g.nodes[0].attributes.value = value.toString();
  const graph = readVmcl(JSON.stringify(g));
  const attrs = graph.nodes[0].attributes;
  assert.equal(attrs.kind, "constant");
  if (attrs.kind === "constant") assert.equal(attrs.value, value);
});
test("ELK routes every link to its actual ordered ports without overlapping nodes", async () => {
  const elk = new ELK();
  {
    // Bundled ELK uses a synchronous worker shim; no worker thread is created.
    for (const name of ["counter", "rippleAdder", "signalArray"]) {
      const graph = readVmcl(fixture(name));
      const layout = await layoutGraph(graph, elk);
      assert.equal(layout.nodes.length, graph.nodes.length);
      assert.equal(layout.links.length, graphLinks(graph).length);
      for (const n of layout.nodes) {
        assert.ok(Number.isFinite(n.x) && Number.isFinite(n.y));
        for (const other of layout.nodes)
          if (n.id !== other.id)
            assert.ok(
              n.x + n.width <= other.x ||
                other.x + other.width <= n.x ||
                n.y + n.height <= other.y ||
                other.y + other.height <= n.y,
            );
      }
      for (const l of layout.links) {
        const source = layout.nodes[l.source],
          target = layout.nodes[l.target];
        assert.deepEqual(l.points[0], {
          x: source.x + source.width,
          y: source.y + source.outputY,
        });
        assert.deepEqual(l.points.at(-1), {
          x: target.x,
          y: target.y + target.ports.find((p) => p.id === l.targetHandle)!.y,
        });
      }
      if (name === "counter")
        assert.deepEqual(await layoutGraph(graph, elk), layout);
    }
  }
});
test("SVG preserves all links and escapes untrusted names", async () => {
  const graph = readVmcl(fixture("counter"));
  graph.top = '<script>alert("x")</script>';
  const attrs = graph.nodes[0].attributes;
  if (attrs.kind === "signal") attrs.name = '<img onerror="x">';
  const elk = new ELK();
  {
    // Bundled ELK uses a synchronous worker shim; no worker thread is created.
    const layout = await layoutGraph(graph, elk);
    const svg = graphSvg(graph, layout);
    assert.ok(svg.includes("&lt;script&gt;"));
    assert.ok(!svg.includes("<script>"));
    assert.ok(!svg.includes("<img"));
    assert.equal(
      (svg.match(/marker-end="url\(#arrow\)"/g) ?? []).length,
      graphLinks(graph).length,
    );
  }
});
