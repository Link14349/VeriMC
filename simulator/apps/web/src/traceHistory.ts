export type Edge = [number, number, number, number];
export type TraceFrame = { frameId: number; epoch: number; from: number; next: number; reset: boolean; edges: Edge[] };
type Series = { rows: Edge[]; head: number; preceding?: Edge };

// Append in arrival order; compact only after at least half the storage expires.
// Per-probe indices keep drawing independent of unrelated channels' history.
export class TraceHistory {
  private rows: Edge[] = [];
  private head = 0;
  private series = new Map<number, Series>();
  private epoch?: number;
  private next = 0;
  dropped = 0;
  constructor(readonly capacity = 500000) {
    if (!Number.isSafeInteger(capacity) || capacity < 1) throw new Error('无效的波形容量');
  }
  get size() { return this.rows.length - this.head; }
  get startTick() { return this.rows[this.head]?.[1] ?? 0; }
  append(frame: TraceFrame) {
    const { epoch, from, next, reset, edges } = frame;
    if (![epoch, from, next].every(v => Number.isSafeInteger(v) && v >= 0) || !Array.isArray(edges) || next !== from + edges.length ||
      (!reset && (epoch !== this.epoch || from !== this.next))) throw new Error('波形帧不连续，已停止接收；请刷新重新同步');
    let previous = reset ? undefined : this.rows.at(-1);
    for (const edge of edges) {
      if (!Array.isArray(edge) || edge.length !== 4 || !edge.every(v => Number.isSafeInteger(v) && v >= 0) || edge[3] > 15 ||
        (previous && (edge[1] < previous[1] || (edge[1] === previous[1] && edge[2] < previous[2])))) throw new Error('波形边沿数据异常，已停止接收');
      previous = edge;
    }
    if (reset) { this.rows = []; this.head = 0; this.series.clear(); this.dropped = from; }
    this.epoch = epoch; this.next = next;
    for (const edge of edges) {
      this.rows.push(edge);
      let channel = this.series.get(edge[0]);
      if (!channel) { channel = { rows: [], head: 0 }; this.series.set(edge[0], channel); }
      channel.rows.push(edge);
    }
    while (this.size > this.capacity) {
      const edge = this.rows[this.head++], channel = this.series.get(edge[0])!;
      channel.preceding = edge; ++channel.head; ++this.dropped;
      if (channel.head >= 1024 && channel.head * 2 >= channel.rows.length) { channel.rows = channel.rows.slice(channel.head); channel.head = 0; }
    }
    if (this.head >= 1024 && this.head * 2 >= this.rows.length) { this.rows = this.rows.slice(this.head); this.head = 0; }
  }
  retainProbes(ids: number[]) {
    const active = new Set(ids);
    for (const [id, channel] of this.series) if (!active.has(id) && channel.head === channel.rows.length) this.series.delete(id);
  }
  window(probeId: number, left: number, right: number) {
    const channel = this.series.get(probeId);
    const start = Math.max(left, this.startTick);
    if (!channel || start > right) return { start, initial: undefined, rows: [] as Edge[], begin: 0, end: 0 };
    const rows = channel.rows;
    const bound = (tick: number, inclusive: boolean) => {
      let low = channel.head, high = rows.length;
      while (low < high) { const mid = (low + high) >>> 1; if (rows[mid][1] < tick || (inclusive && rows[mid][1] === tick)) low = mid + 1; else high = mid; }
      return low;
    };
    const begin = bound(start, false), end = bound(right, true);
    const prior = begin > channel.head ? rows[begin - 1] : channel.preceding;
    return { start, initial: prior?.[3], rows, begin, end };
  }
}
