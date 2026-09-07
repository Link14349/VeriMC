import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import {
  Background,
  BackgroundVariant,
  MarkerType,
  MiniMap,
  ReactFlow,
  useNodesState,
  useReactFlow,
  useViewport,
} from "@xyflow/react";
import {
  ArrowDownLeft,
  ArrowUpRight,
  Braces,
  Check,
  ChevronRight,
  CircuitBoard,
  Clock3,
  Download,
  FileCode2,
  FileUp,
  Focus,
  GitBranch,
  HelpCircle,
  Layers3,
  LoaderCircle,
  Maximize2,
  MousePointer2,
  PanelLeftClose,
  PanelLeftOpen,
  Plus,
  Minus,
  Search,
  ShieldCheck,
  SlidersHorizontal,
  Unplug,
  X,
} from "lucide-react";
import {
  category,
  categoryColors,
  categoryLabels,
  neighbors,
  nodeName,
  typeBits,
  typeLabel,
  type LogicGraph,
  type LogicNode,
  type NodeCategory,
} from "./model";
import type { GraphLayout, LayoutLink } from "./layout";
import {
  CircuitNodeView,
  CircuitEdgeView,
  type CircuitNode,
  type CircuitEdge,
} from "./graphView";
import { graphSvg } from "./exportSvg";
import { viewerLimits } from "./vmclReader";

const nodeTypes = { circuit: CircuitNodeView },
  edgeTypes = { circuit: CircuitEdgeView };
const examples = [
  { id: "counter", name: "同步计数器", detail: "4 位 · 状态与反馈" },
  { id: "adder", name: "四位加法器", detail: "字级加法与进位" },
  { id: "rippleAdder", name: "逐位进位加法器", detail: "4 个实例 · 展开连接" },
  { id: "signalArray", name: "信号数组", detail: "数组 · generate" },
];
interface Document {
  name: string;
  graph: LogicGraph;
  layout: GraphLayout;
  example: string | null;
}
function flowNodes(graph: LogicGraph, layout: GraphLayout): CircuitNode[] {
  return layout.nodes.map((n) => ({
    id: String(n.id),
    type: "circuit",
    position: { x: n.x, y: n.y },
    width: n.width,
    height: n.height,
    ariaRole: "button",
    ariaLabel: `${nodeName(graph.nodes[n.id])}，${categoryLabels[category(graph.nodes[n.id])]}，节点 #${n.id}`,
    data: { logical: graph.nodes[n.id], layout: n, muted: false },
  }));
}
function IconButton({
  label,
  children,
  ...props
}: React.ButtonHTMLAttributes<HTMLButtonElement> & { label: string }) {
  return (
    <button className="iconButton" title={label} aria-label={label} {...props}>
      {children}
    </button>
  );
}
function Properties({ rows }: { rows: [string, React.ReactNode][] }) {
  return (
    <dl className="propertyList">
      {rows.map(([key, value]) => (
        <div key={key}>
          <dt>{key}</dt>
          <dd>{value}</dd>
        </div>
      ))}
    </dl>
  );
}
function NodeInspector({
  node,
  graph,
  links,
  navigate,
}: {
  node: LogicNode;
  graph: LogicGraph;
  links: LayoutLink[];
  navigate: (id: number) => void;
}) {
  const kind = category(node),
    a = node.attributes;
  const incoming = links.filter((l) => l.target === node.id),
    outgoing = links.filter((l) => l.source === node.id);
  return (
    <>
      <div className="inspectorHero">
        <span className="categoryTag" style={{ color: categoryColors[kind] }}>
          {categoryLabels[kind]} <span>#{node.id}</span>
        </span>
        <h2>{nodeName(node)}</h2>
        <code>{typeLabel(node.type)}</code>
      </div>
      <section className="inspectorSection">
        <h3>节点属性</h3>
        <Properties
          rows={[
            ["操作", node.op],
            ["位数", `${typeBits(node.type)} bit`],
            ["展开路径", node.instance],
            ...(a.kind === "signal" || a.kind === "register"
              ? [["完整名称", a.name] as [string, string]]
              : []),
            ...(a.kind === "constant"
              ? [
                  [
                    "十进制值",
                    <code className="longValue">{a.value.toString()}</code>,
                  ] as [string, React.ReactNode],
                ]
              : []),
            ...(a.kind === "slice"
              ? [["起始位", String(a.offset)] as [string, string]]
              : []),
            ...(a.kind === "shift"
              ? [["移位量", String(a.amount)] as [string, string]]
              : []),
          ]}
        />
        {a.kind === "register" && (
          <div className="stateNote">
            <Layers3 size={15} />
            <div>
              <strong>同步状态边界</strong>
              <p>上升沿采样 · 同步高有效复位</p>
              <p>
                复位值 <code>{a.resetValue.toString()}</code>，未复位时无效。
              </p>
            </div>
          </div>
        )}
        {node.type.kind === "enum" && (
          <div className="enumMembers">
            {node.type.members.map((member, i) => (
              <span key={member}>
                {i} · {member}
              </span>
            ))}
          </div>
        )}
      </section>
      {[
        { title: "输入来源", links: incoming, upstream: true },
        { title: "输出去向", links: outgoing, upstream: false },
      ].map((group) => (
        <section className="inspectorSection" key={group.title}>
          <h3>
            {group.title}
            <span>{group.links.length}</span>
          </h3>
          {group.links.length ? (
            group.links.map((link) => {
              const id = group.upstream ? link.source : link.target;
              return (
                <button
                  className="connectionRow"
                  key={link.id}
                  onClick={() => navigate(id)}
                >
                  <span className={`wireDot ${link.kind}`} />
                  <span>
                    <strong>{nodeName(graph.nodes[id])}</strong>
                    <small>
                      #{id} · {link.label} · {link.width} bit
                    </small>
                  </span>
                  <ArrowUpRight size={13} />
                </button>
              );
            })
          ) : (
            <p className="mutedText">
              {node.op === "input" && group.upstream
                ? "由顶层外部输入提供"
                : "无连接"}
            </p>
          )}
        </section>
      ))}
      <section className="inspectorSection">
        <h3>
          源码位置 <FileCode2 size={13} />
        </h3>
        <div className="sourceCard">
          <strong>{node.source.file}</strong>
          <span>
            第 {node.source.line} 行，第 {node.source.column} 列
          </span>
          <small>
            字节区间 [{node.source.start}, {node.source.end})
          </small>
        </div>
      </section>
    </>
  );
}
function LinkInspector({
  link,
  graph,
  navigate,
}: {
  link: LayoutLink;
  graph: LogicGraph;
  navigate: (id: number) => void;
}) {
  const connection =
    link.connection === undefined ? null : graph.connections[link.connection];
  return (
    <>
      <div className="inspectorHero">
        <span className="categoryTag">
          {link.kind === "clock"
            ? "时钟连线"
            : link.kind === "reset"
              ? "复位连线"
              : "数据连线"}
        </span>
        <h2>{link.width} bit</h2>
        <code>{connection ? "显式信号驱动" : "运算输入依赖"}</code>
      </div>
      <section className="inspectorSection">
        <h3>连接方向</h3>
        {[link.source, link.target].map((id, i) => (
          <button
            className="connectionRow"
            key={i}
            onClick={() => navigate(id)}
          >
            <span>{i ? "到" : "从"}</span>
            <span>
              <strong>{nodeName(graph.nodes[id])}</strong>
              <small>
                #{id} · {typeLabel(graph.nodes[id].type)}
              </small>
            </span>
            <ArrowUpRight size={14} />
          </button>
        ))}
        <Properties
          rows={
            connection
              ? [
                  [
                    "目标位段",
                    `[${connection.targetOffset + connection.width - 1}:${connection.targetOffset}]`,
                  ],
                  ["源位偏移", String(connection.sourceOffset)],
                  ["视图类型", typeLabel(connection.type)],
                  [
                    "来源",
                    `${connection.sourceSpan.file}:${connection.sourceSpan.line}`,
                  ],
                ]
              : [
                  ["输入端", link.label],
                  ["输入序号", String(link.inputIndex)],
                ]
          }
        />
      </section>
    </>
  );
}
function Overview({
  doc,
  navigate,
}: {
  doc: Document;
  navigate: (id: number) => void;
}) {
  const g = doc.graph;
  const counts = g.nodes.reduce(
    (result, node) => {
      const key = category(node);
      result[key] = (result[key] ?? 0) + 1;
      return result;
    },
    {} as Partial<Record<NodeCategory, number>>,
  );
  return (
    <>
      <div className="inspectorHero overviewHero">
        <div className="overviewIcon">
          <CircuitBoard size={24} />
        </div>
        <span className="eyebrow">DESIGN OVERVIEW</span>
        <h2>{g.top.split("::").at(-1)}</h2>
        <code>{g.top}</code>
        <p>点击节点或连线，查看逻辑与来源。</p>
      </div>
      <section className="inspectorSection">
        <h3>电路组成</h3>
        <div className="compositionBar">
          {Object.entries(counts).map(([key, count]) => (
            <span
              key={key}
              style={{
                flex: count,
                background: categoryColors[key as NodeCategory],
              }}
            />
          ))}
        </div>
        <div className="categoryList">
          {Object.entries(counts).map(([key, count]) => (
            <div key={key}>
              <span
                style={{ background: categoryColors[key as NodeCategory] }}
              />
              <label>{categoryLabels[key as NodeCategory]}</label>
              <strong>{count}</strong>
            </div>
          ))}
        </div>
      </section>
      <section className="inspectorSection">
        <h3>
          顶层端口<span>{g.ports.length}</span>
        </h3>
        {g.ports.map((port) => (
          <button
            className="connectionRow"
            key={port.name}
            onClick={() => navigate(port.node)}
          >
            {port.direction === "input" ? (
              <ArrowDownLeft size={15} />
            ) : (
              <ArrowUpRight size={15} />
            )}
            <span>
              <strong>{port.name}</strong>
              <small>{typeLabel(g.nodes[port.node].type)}</small>
            </span>
            <span className="directionLabel">
              {port.direction === "input" ? "IN" : "OUT"}
            </span>
          </button>
        ))}
      </section>
      <section className="inspectorSection">
        <h3>
          源文件<span>{g.sources.length}</span>
        </h3>
        {g.sources.map((source) => (
          <div className="sourceCard" key={source.path}>
            <strong>{source.path}</strong>
            <small>{source.byteLength.toLocaleString()} bytes · SHA-256</small>
            <code title={source.sha256}>{source.sha256.slice(0, 20)}…</code>
          </div>
        ))}
      </section>
      <div className="inspectionNote">
        <ShieldCheck size={14} />
        <p>展示逻辑结构。未运行仿真或验证物理电路。</p>
      </div>
    </>
  );
}
export function App() {
  const [doc, setDoc] = useState<Document | null>(null),
    [nodes, setNodes, onNodesChange] = useNodesState<CircuitNode>([]);
  const [exportUrl, setExportUrl] = useState<string | null>(null);
  const [selected, setSelected] = useState<number | null>(null),
    [selectedLink, setSelectedLink] = useState<string | null>(null);
  const [query, setQuery] = useState(""),
    [scope, setScope] = useState(""),
    [trace, setTrace] = useState<"both" | "upstream" | "downstream">("both");
  const [showControls, setShowControls] = useState(true),
    [showLabels, setShowLabels] = useState(true),
    [sidebar, setSidebar] = useState(true);
  const [busy, setBusy] = useState("正在打开示例…"),
    [error, setError] = useState(""),
    [dragging, setDragging] = useState(false);
  const fileInput = useRef<HTMLInputElement>(null),
    searchInput = useRef<HTMLInputElement>(null),
    helpDialog = useRef<HTMLDialogElement>(null);
  const worker = useRef<Worker | null>(null),
    sequence = useRef(0),
    timer = useRef<ReturnType<typeof setTimeout> | null>(null),
    dragDepth = useRef(0);
  const flow = useReactFlow<CircuitNode, CircuitEdge>();
  const viewport = useViewport();
  const duration = window.matchMedia("(prefers-reduced-motion: reduce)").matches
    ? 0
    : 250;
  const clearFocus = useCallback(() => {
    setSelected(null);
    setSelectedLink(null);
    setQuery("");
    setScope("");
    setTrace("both");
  }, []);
  const fitAll = useCallback(() => {
    void flow.fitView({ padding: 0.08, maxZoom: 1.15, duration });
  }, [flow, duration]);
  const stopWorker = useCallback(() => {
    worker.current?.terminate();
    worker.current = null;
    if (timer.current) clearTimeout(timer.current);
  }, []);
  const beginRead = useCallback(
    (text: string, name: string, example: string | null, ticket: number) => {
      if (ticket !== sequence.current) return;
      const active = new Worker(new URL("./graphWorker.ts", import.meta.url), {
        type: "module",
      });
      worker.current = active;
      const fail = (message: string) => {
        if (ticket !== sequence.current) return;
        stopWorker();
        setBusy("");
        setError(message);
      };
      timer.current = setTimeout(
        () => fail("处理超过 30 秒，已停止。请导出较小的模块再打开。"),
        30000,
      );
      active.onerror = () =>
        fail("浏览器未能完成图处理。请刷新重试，或打开较小的模块。");
      active.onmessage = (
        event: MessageEvent<{
          phase?: string;
          error?: string;
          graph?: LogicGraph;
          layout?: GraphLayout;
        }>,
      ) => {
        if (ticket !== sequence.current) return;
        if (event.data.phase) {
          setBusy("正在排列节点与连线…");
          return;
        }
        if (event.data.error) {
          fail(event.data.error);
          return;
        }
        const { graph, layout } = event.data;
        if (!graph || !layout) {
          fail("图处理返回了无效结果。");
          return;
        }
        stopWorker();
        clearFocus();
        setDoc({ name, graph, layout, example });
        setNodes(flowNodes(graph, layout));
        setBusy("");
        setError("");
      };
      active.postMessage({ text });
    },
    [clearFocus, setNodes, stopWorker],
  );
  const loadExample = useCallback(
    async (id: string) => {
      const ticket = ++sequence.current;
      stopWorker();
      setBusy("正在打开示例…");
      setError("");
      try {
        const response = await fetch(
          `${import.meta.env.BASE_URL}examples/${id}.vmcl`,
        );
        if (!response.ok) throw new Error("示例文件读取失败。");
        beginRead(await response.text(), `${id}.vmcl`, id, ticket);
      } catch (e) {
        if (ticket === sequence.current) {
          setBusy("");
          setError(e instanceof Error ? e.message : "示例读取失败。");
        }
      }
    },
    [beginRead, stopWorker],
  );
  const openFile = useCallback(
    async (file: File) => {
      const ticket = ++sequence.current;
      stopWorker();
      setError("");
      setBusy("正在读取本地文件…");
      try {
        if (!file.name.toLowerCase().endsWith(".vmcl"))
          throw new Error("请选择 .vmcl 逻辑图文件。");
        if (file.size > viewerLimits.fileBytes)
          throw new Error("文件超过 16 MiB；请先导出较小的模块。");
        const bytes = await file.arrayBuffer();
        const text = new TextDecoder("utf-8", { fatal: true }).decode(bytes);
        beginRead(text, file.name, null, ticket);
      } catch (e) {
        if (ticket === sequence.current) {
          setBusy("");
          setError(
            e instanceof TypeError
              ? "文件不是有效的 UTF-8 文本。"
              : e instanceof Error
                ? e.message
                : "文件读取失败。",
          );
        }
      }
    },
    [beginRead, stopWorker],
  );
  useEffect(() => {
    void loadExample("counter");
    return () => {
      sequence.current++;
      stopWorker();
    };
  }, [loadExample, stopWorker]);
  useEffect(() => {
    if (doc) {
      const t = setTimeout(fitAll, 60);
      return () => clearTimeout(t);
    }
  }, [doc, fitAll]);
  const focusNode = useCallback(
    (id: number) => {
      setSelected(id);
      setSelectedLink(null);
      setQuery("");
      const n = flow.getNode(String(id));
      if (n)
        void flow.setCenter(
          n.position.x + (n.width ?? 194) / 2,
          n.position.y + (n.height ?? 108) / 2,
          { zoom: 1.1, duration },
        );
    },
    [flow, duration],
  );
  const focusScope = (path: string) => {
    clearFocus();
    setScope(path);
    const ids = doc?.graph.nodes
      .filter((n) => n.instance === path || n.instance.startsWith(path + "."))
      .map((n) => ({ id: String(n.id) }));
    if (ids?.length)
      void flow.fitView({ nodes: ids, padding: 0.3, duration, maxZoom: 1.15 });
  };
  useEffect(() => {
    const keyDown = (e: KeyboardEvent) => {
      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "o") {
        e.preventDefault();
        fileInput.current?.click();
        return;
      }
      if (e.key === "Escape" && !helpDialog.current?.open) {
        clearFocus();
        (e.target as HTMLElement).blur();
        return;
      }
      if ((e.target as HTMLElement).closest("input,textarea,select")) return;
      if (!e.ctrlKey && !e.metaKey && e.key.toLowerCase() === "f") fitAll();
      if (e.key === "/") {
        e.preventDefault();
        setSidebar(true);
        setTimeout(() => searchInput.current?.focus(), 0);
      }
    };
    window.addEventListener("keydown", keyDown);
    return () => window.removeEventListener("keydown", keyDown);
  }, [clearFocus, fitAll]);
  const matches = useMemo(() => {
    const q = query.trim().toLowerCase();
    if (!q || !doc) return [];
    return doc.graph.nodes.filter((n) =>
      q.startsWith("#")
        ? String(n.id) === q.slice(1)
        : `${nodeName(n)} ${n.op} ${n.instance} ${n.source.file} ${typeLabel(n.type)} ${n.attributes.kind === "signal" || n.attributes.kind === "register" ? n.attributes.name : ""}`
            .toLowerCase()
            .includes(q),
    );
  }, [doc, query]);
  const scopes = useMemo(() => {
    if (!doc) return [];
    const paths = new Set([
      ...doc.graph.instances.map((i) => i.path),
      ...doc.graph.nodes.map((n) => n.instance),
    ]);
    return [...paths]
      .filter(Boolean)
      .sort()
      .map((path) => ({
        path,
        depth: path.split(".").length - 1,
        count: doc.graph.nodes.filter(
          (n) => n.instance === path || n.instance.startsWith(path + "."),
        ).length,
      }));
  }, [doc]);
  const focusIds = useMemo(() => {
    if (!doc) return null;
    if (selected !== null) return neighbors(doc.layout.links, selected, trace);
    if (selectedLink) {
      const link = doc.layout.links.find((l) => l.id === selectedLink);
      return link ? new Set([link.source, link.target]) : null;
    }
    if (query.trim()) return new Set(matches.map((n) => n.id));
    if (scope)
      return new Set(
        doc.graph.nodes
          .filter(
            (n) => n.instance === scope || n.instance.startsWith(scope + "."),
          )
          .map((n) => n.id),
      );
    return null;
  }, [doc, selected, selectedLink, trace, query, matches, scope]);
  const visibleNodes = useMemo(
    () =>
      nodes.map((n) => ({
        ...n,
        selected: Number(n.id) === selected,
        data: {
          ...n.data,
          muted: focusIds !== null && !focusIds.has(Number(n.id)),
        },
      })),
    [nodes, selected, focusIds],
  );
  const edges: CircuitEdge[] = useMemo(
    () =>
      doc?.layout.links.map((link) => ({
        id: link.id,
        source: String(link.source),
        target: String(link.target),
        sourceHandle: "out",
        targetHandle: link.targetHandle,
        type: "circuit",
        ariaRole: "button",
        ariaLabel: `连线 ${link.source} → ${link.target}，${link.label}，${link.width} bit`,
        selected: selectedLink === link.id,
        hidden: !showControls && link.kind !== "data",
        markerEnd: {
          type: MarkerType.ArrowClosed,
          width: 11,
          height: 11,
          color:
            link.kind === "clock"
              ? "#b48b43"
              : link.kind === "reset"
                ? "#b86b78"
                : "#8997a5",
        },
        data: {
          link,
          muted:
            focusIds !== null &&
            !(focusIds.has(link.source) && focusIds.has(link.target)),
          showLabels,
        },
      })) ?? [],
    [doc, focusIds, selectedLink, showControls, showLabels],
  );
  const chosenNode = selected !== null ? doc?.graph.nodes[selected] : null;
  const chosenLink = doc?.layout.links.find((l) => l.id === selectedLink);
  useEffect(() => {
    if (!doc) return;
    const url = URL.createObjectURL(
      new Blob([graphSvg(doc.graph, doc.layout)], {
        type: "image/svg+xml;charset=utf-8",
      }),
    );
    setExportUrl(url);
    return () => URL.revokeObjectURL(url);
  }, [doc]);
  return (
    <div
      className={`appShell ${sidebar ? "" : "sidebarHidden"}`}
      onDragEnter={(e) => {
        if (e.dataTransfer.types.includes("Files")) {
          e.preventDefault();
          dragDepth.current++;
          setDragging(true);
        }
      }}
      onDragOver={(e) => {
        if (e.dataTransfer.types.includes("Files")) e.preventDefault();
      }}
      onDragLeave={(e) => {
        if (e.dataTransfer.types.includes("Files")) {
          dragDepth.current--;
          if (dragDepth.current <= 0) setDragging(false);
        }
      }}
      onDrop={(e) => {
        if (e.dataTransfer.files.length) {
          e.preventDefault();
          setDragging(false);
          dragDepth.current = 0;
          if (e.dataTransfer.files.length !== 1)
            setError("每次请打开一个 .vmcl 文件。");
          else void openFile(e.dataTransfer.files[0]);
        }
      }}
    >
      <input
        className="fileInput"
        ref={fileInput}
        type="file"
        accept=".vmcl"
        aria-label="选择 VMCL 文件"
        onChange={(e) => {
          const f = e.target.files?.[0];
          if (f) void openFile(f);
          e.target.value = "";
        }}
      />
      <header className="appHeader">
        <div className="brand">
          <img src={`${import.meta.env.BASE_URL}favicon.svg`} alt="" />
          <strong>
            VMCL<span>Visualize</span>
          </strong>
          <span className="versionBadge">BETA</span>
        </div>
        <div className="headerDivider" />
        <span className="headerSubtitle">逻辑电路工作台</span>
        <div className="headerActions">
          <span className="localBadge">
            <span />
            本地处理
          </span>
          <IconButton
            label="使用帮助"
            onClick={() => helpDialog.current?.showModal()}
          >
            <HelpCircle size={18} />
          </IconButton>
          <button
            className="primaryButton"
            onClick={() => fileInput.current?.click()}
          >
            <FileUp size={16} />
            打开 .vmcl<span className="keycap">⌘ O</span>
          </button>
        </div>
      </header>
      <aside className="leftSidebar">
        <div className="sidebarTitle">
          <span>设计资源</span>
          <IconButton label="收起侧栏" onClick={() => setSidebar(false)}>
            <PanelLeftClose size={16} />
          </IconButton>
        </div>
        <div className="searchBox">
          <Search size={15} />
          <input
            ref={searchInput}
            placeholder="搜索节点、信号或 #ID"
            aria-label="搜索节点"
            value={query}
            onChange={(e) => {
              setQuery(e.target.value);
              setSelected(null);
              setSelectedLink(null);
            }}
          />
          <kbd>/</kbd>
        </div>
        {query.trim() ? (
          <div className="searchResults">
            <div className="sectionCaption">
              搜索结果 <span>{matches.length}</span>
            </div>
            {matches.slice(0, 100).map((n) => (
              <button
                className="searchResult"
                key={n.id}
                onClick={() => focusNode(n.id)}
              >
                <span
                  className="categoryDot"
                  style={{ background: categoryColors[category(n)] }}
                />
                <span>
                  <strong>{nodeName(n)}</strong>
                  <small>
                    #{n.id} · {typeLabel(n.type)}
                  </small>
                </span>
                <ArrowUpRight size={13} />
              </button>
            ))}
            {!matches.length && (
              <p className="emptySearch">
                没有匹配节点
                <br />
                <small>试试信号名、操作名或 #4</small>
              </p>
            )}
            {matches.length > 100 && (
              <p className="mutedText">显示前 100 项，请缩小搜索范围。</p>
            )}
          </div>
        ) : (
          <>
            <div className="sectionCaption">
              当前设计 <span>{doc ? "01" : "—"}</span>
            </div>
            <div className="fileCard">
              <span className="fileIcon">
                <FileCode2 size={20} />
              </span>
              <span>
                <strong title={doc?.name}>{doc?.name ?? "尚未打开"}</strong>
                <small>
                  {doc?.example ? "内置示例" : "本地文件"} · VMCL v1
                </small>
              </span>
              <span className="loadedDot" />
            </div>
            <div className="treeHeading">
              <GitBranch size={13} />
              <span>展开层级</span>
              <span>{doc?.graph.instances.length ?? 0} 实例</span>
            </div>
            <div className="instanceTree">
              {scopes.map((item) => (
                <button
                  key={item.path}
                  title={item.path}
                  className={`treeRow ${scope === item.path ? "active" : ""}`}
                  style={{ paddingLeft: 12 + Math.min(item.depth, 5) * 12 }}
                  onClick={() => focusScope(item.path)}
                >
                  {item.depth ? <GitBranch size={13} /> : <Layers3 size={14} />}
                  <span>{item.path.split(".").at(-1)}</span>
                  <small>{item.count}</small>
                </button>
              ))}
            </div>
            <div className="sectionCaption examplesCaption">
              示例电路 <span>04</span>
            </div>
            <div className="exampleList">
              {examples.map((example) => (
                <button
                  className={`exampleButton ${doc?.example === example.id ? "active" : ""}`}
                  key={example.id}
                  onClick={() => void loadExample(example.id)}
                >
                  <span className="exampleIcon">
                    {example.id === "counter" ? (
                      <Clock3 size={16} />
                    ) : example.id === "signalArray" ? (
                      <Layers3 size={16} />
                    ) : (
                      <Braces size={16} />
                    )}
                  </span>
                  <span>
                    <strong>{example.name}</strong>
                    <small>{example.detail}</small>
                  </span>
                  {doc?.example === example.id ? (
                    <Check size={14} />
                  ) : (
                    <ChevronRight size={13} />
                  )}
                </button>
              ))}
            </div>
          </>
        )}
        <div className="sidebarFoot">
          <div className="privacySymbol">
            <Unplug size={16} />
          </div>
          <div>
            <strong>文件留在你的设备上</strong>
            <p>拖入 .vmcl 即可查看，无需上传。</p>
          </div>
        </div>
      </aside>
      <main className="mainWorkspace">
        <div className="workspaceHeading">
          <div className="breadcrumb">
            {!sidebar && (
              <IconButton label="展开侧栏" onClick={() => setSidebar(true)}>
                <PanelLeftOpen size={16} />
              </IconButton>
            )}
            <CircuitBoard size={14} />
            <span>{doc?.graph.top.split("::")[0] ?? "VeriMC"}</span>
            <ChevronRight size={12} />
            <strong>{doc?.graph.top.split("::").at(-1) ?? "逻辑图"}</strong>
            <span className="graphBadge">逻辑图</span>
          </div>
          <a
            className="textButton exportButton"
            href={!busy && exportUrl ? exportUrl : undefined}
            download={doc?.name.replace(/\.vmcl$/i, "") + ".svg"}
            aria-disabled={!exportUrl || !!busy}
            tabIndex={!exportUrl || !!busy ? -1 : 0}
            title="导出完整图的自动布局，不包含聚焦筛选或手动拖动"
          >
            <Download size={14} />
            导出 SVG
          </a>
        </div>
        <div className="canvasToolbar">
          <div className="canvasTitle">
            <span className="tabMarker" />
            <span>电路结构</span>
            {doc && (
              <small>
                {doc.graph.nodes.length} 节点<span>·</span>
                {doc.layout.links.length} 连线
              </small>
            )}
          </div>
          <div className="viewOptions">
            <button
              aria-pressed={showLabels}
              className={showLabels ? "optionActive" : ""}
              onClick={() => setShowLabels((v) => !v)}
              title="显示连线位宽"
            >
              <span className="bitIcon">N</span>位宽
            </button>
            <button
              aria-pressed={showControls}
              className={showControls ? "optionActive" : ""}
              onClick={() => setShowControls((v) => !v)}
            >
              <SlidersHorizontal size={13} />
              控制线
            </button>
            <span className="smallDivider" />
            <IconButton
              label="重新自动布局"
              disabled={!doc || !!busy}
              onClick={() => {
                if (doc) {
                  setNodes(flowNodes(doc.graph, doc.layout));
                  setTimeout(fitAll, 0);
                }
              }}
            >
              <GitBranch size={15} />
            </IconButton>
            <IconButton label="适应画布" onClick={fitAll}>
              <Maximize2 size={16} />
            </IconButton>
          </div>
        </div>
        {error && (
          <div className="errorBanner" role="alert">
            <span>
              <strong>无法打开文件</strong>
              {error}
            </span>
            <IconButton label="关闭错误提示" onClick={() => setError("")}>
              <X size={16} />
            </IconButton>
          </div>
        )}
        <div className="graphCanvas" aria-label="逻辑电路画布">
          <ReactFlow<CircuitNode, CircuitEdge>
            nodes={visibleNodes}
            edges={edges}
            nodeTypes={nodeTypes}
            edgeTypes={edgeTypes}
            onNodesChange={(changes) => {
              onNodesChange(changes);
              const selection = changes.find(
                (change) => change.type === "select" && change.selected,
              );
              if (selection?.type === "select") {
                setSelected(Number(selection.id));
                setSelectedLink(null);
                setQuery("");
              }
            }}
            onEdgesChange={(changes) => {
              const selection = changes.find(
                (change) => change.type === "select" && change.selected,
              );
              if (selection?.type === "select") {
                setSelectedLink(selection.id);
                setSelected(null);
                setQuery("");
              }
            }}
            onNodeClick={(_, node) => {
              setSelected(Number(node.id));
              setSelectedLink(null);
              setQuery("");
            }}
            onEdgeClick={(_, edge) => {
              setSelectedLink(edge.id);
              setSelected(null);
              setQuery("");
            }}
            onPaneClick={clearFocus}
            minZoom={0.08}
            maxZoom={2.5}
            nodesConnectable={false}
            edgesReconnectable={false}
            deleteKeyCode={null}
            selectionKeyCode={null}
            panOnScroll
            zoomOnScroll={false}
            zoomOnPinch
            zoomOnDoubleClick={false}
            panOnDrag
            fitView
            fitViewOptions={{ padding: 0.08, maxZoom: 1.15 }}
          >
            <Background
              variant={BackgroundVariant.Dots}
              gap={22}
              size={1.1}
              color="#d1d7d8"
            />
            <MiniMap
              nodeColor={(n) =>
                categoryColors[category((n as CircuitNode).data.logical)]
              }
              maskColor="rgba(235,239,236,0.67)"
              pannable
              zoomable
              ariaLabel="电路缩略图"
            />
          </ReactFlow>
          <div className="canvasLegend">
            <span>
              <i className="legendData" />
              数据
            </span>
            <span>
              <i className="legendClock" />
              时钟
            </span>
            <span>
              <i className="legendReset" />
              复位
            </span>
          </div>
          {focusIds && (
            <div className="focusBar">
              <Focus size={14} />
              <span>
                {chosenNode
                  ? `聚焦 ${nodeName(chosenNode)}`
                  : selectedLink
                    ? "聚焦连线"
                    : query
                      ? `${matches.length} 个搜索结果`
                      : scope.split(".").at(-1)}
              </span>
              {chosenNode && (
                <select
                  aria-label="追踪方向"
                  value={trace}
                  onChange={(e) => setTrace(e.target.value as typeof trace)}
                >
                  <option value="both">相邻节点</option>
                  <option value="upstream">上游一跳</option>
                  <option value="downstream">下游一跳</option>
                </select>
              )}
              <button onClick={clearFocus}>
                恢复全图
                <X size={12} />
              </button>
            </div>
          )}
          {!showControls && (
            <div className="hiddenWiresNote">已隐藏时钟 / 复位连线</div>
          )}
          <div className="zoomControls">
            <IconButton
              label="缩小"
              onClick={() => void flow.zoomOut({ duration })}
            >
              <Minus size={15} />
            </IconButton>
            <button onClick={fitAll} title="适应画布">
              {Math.round(viewport.zoom * 100)}%
            </button>
            <IconButton
              label="放大"
              onClick={() => void flow.zoomIn({ duration })}
            >
              <Plus size={15} />
            </IconButton>
            <span />
            <IconButton label="适应全图" onClick={fitAll}>
              <Maximize2 size={15} />
            </IconButton>
          </div>
          <div className="canvasHint">
            <MousePointer2 size={12} />
            拖动画布平移 · 触控板捏合缩放 · <kbd>F</kbd> 适应全图
          </div>
          {doc && !doc.graph.nodes.length && (
            <div className="emptyCanvas">
              <CircuitBoard size={32} />
              <h2>这个模块没有逻辑节点</h2>
              <p>可以打开其它 .vmcl 或选择左侧示例。</p>
            </div>
          )}
          {busy && (
            <div className="busyOverlay" role="status">
              <LoaderCircle size={24} className="spinning" />
              <strong>{busy}</strong>
              <button
                className="textButton"
                onClick={() => {
                  sequence.current++;
                  stopWorker();
                  setBusy("");
                }}
              >
                取消
              </button>
            </div>
          )}
        </div>
        <footer className="workspaceFooter">
          <span className="fileStatus">
            <span />
            {doc ? `${doc.name} · 已载入` : "等待文件"}
          </span>
          <span>
            VMCL v1<span className="footerSep">/</span>仅逻辑结构
          </span>
        </footer>
      </main>
      <aside
        className={`rightInspector ${chosenNode || chosenLink ? "mobileOpen" : ""}`}
      >
        <div className="sidebarTitle">
          <span>
            {chosenNode ? "节点详情" : chosenLink ? "连线详情" : "设计概览"}
          </span>
          {chosenNode || chosenLink ? (
            <IconButton label="关闭详情" onClick={clearFocus}>
              <X size={16} />
            </IconButton>
          ) : (
            <SlidersHorizontal size={15} />
          )}
        </div>
        <div className="inspectorScroll">
          {doc &&
            (chosenNode ? (
              <NodeInspector
                node={chosenNode}
                graph={doc.graph}
                links={doc.layout.links}
                navigate={focusNode}
              />
            ) : chosenLink ? (
              <LinkInspector
                link={chosenLink}
                graph={doc.graph}
                navigate={focusNode}
              />
            ) : (
              <Overview doc={doc} navigate={focusNode} />
            ))}
        </div>
        <div className="inspectorFooter">
          VERIMC<span>LOGIC EXPLORER</span>
        </div>
      </aside>
      {dragging && (
        <div className="dropOverlay">
          <div>
            <FileUp size={36} />
            <h2>在这里放下逻辑图</h2>
            <p>打开 .vmcl · 文件仅在本地处理</p>
          </div>
        </div>
      )}
      <dialog ref={helpDialog} className="helpDialog">
        <div className="dialogHeading">
          <h2>从源码，看到电路</h2>
          <IconButton
            label="关闭帮助"
            onClick={() => helpDialog.current?.close()}
          >
            <X size={18} />
          </IconButton>
        </div>
        <p>打开编译器输出的 .vmcl，或选择示例开始探索。</p>
        <div className="helpSteps">
          <div>
            <FileUp />
            <span>
              <strong>01 · 打开</strong>选择文件或直接拖入网页，不会上传。
            </span>
          </div>
          <div>
            <MousePointer2 />
            <span>
              <strong>02 · 探索</strong>
              拖动画布、移动节点；点击节点追踪相邻连线。
            </span>
          </div>
          <div>
            <Search />
            <span>
              <strong>03 · 定位</strong>搜索信号、操作或
              #ID，左侧层级可聚焦实例。
            </span>
          </div>
          <div>
            <Download />
            <span>
              <strong>04 · 导出</strong>下载完整图的自动布局
              SVG，方便查看和分享。
            </span>
          </div>
        </div>
        <div className="helpKeys">
          <span>
            <kbd>F</kbd>适应画布
          </span>
          <span>
            <kbd>/</kbd>搜索
          </span>
          <span>
            <kbd>Esc</kbd>取消聚焦
          </span>
        </div>
        <p className="helpFootnote">
          v1 · 最多 3,000 节点 / 15,000 连线 / 16
          MiB。网页检查可视化所需的结构、类型和驱动；完整按位组合环与时钟域校验请使用
          verimcCli validate。这里不执行仿真。
        </p>
      </dialog>
    </div>
  );
}
