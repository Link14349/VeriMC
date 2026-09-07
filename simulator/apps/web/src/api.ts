import { TraceHistory } from './traceHistory';
export type Pos = [number, number, number];
export type BlockDef = { stateId: number; name: string; properties: Record<string, string> };
export type BlockCell = { pos: Pos; stateId: number; value: number; renderStateId: number; motion: number };
export type CatalogItem = { name: string; defaultState: number; device: number; properties: Record<string, string[]>; supportLevel: string };
export type Probe = { id: number; pos: Pos; name: string; value: number; mode: string; trigger: string };
export type EnvironmentAction = { id: number; tick: number; sequence: number; kind: 'itemEjected'; source: Pos; item: string; count: number; position: Pos; velocity: Pos; resolved: boolean };
export type Status = { tick: number; running: boolean; speed: number; eventsPerSecond: number; blocks: number; pending: number; updates: number; events: number; storageBytes: number; traceDropped: number; pauseReason: string; pendingActions: EnvironmentAction[]; actionsDropped: number; revision: number; probes: Probe[]; canUndo: boolean; canRedo: boolean; name: string };
type Reply = Record<string, unknown>;
export const posKey = (p: Pos) => p.join(',');
export class SimulatorConnection extends EventTarget {
  cells = new Map<string, BlockCell>();
  states = new Map<number, BlockDef>();
  catalog: CatalogItem[] = [];
  items: { name: string; maxStack: number; bookshelfBook?: boolean; jukeboxSong?: {name:string;lengthTicks:number;comparatorOutput:number} }[] = [];
  gameEvents: { name: string; frequency: number; radius: number; listenable: boolean; ignoreSneaking: boolean }[] = [];
  trace = new TraceHistory();
  connected = false;
  status: Status = { tick: 0, running: false, speed: 20, eventsPerSecond: 0, blocks: 0, pending: 0, updates: 0, events: 0, storageBytes: 0, traceDropped: 0, pauseReason: '', pendingActions: [], actionsDropped: 0, revision: 0, probes: [], canUndo: false, canRedo: false, name: '连接本地内核…' };
  private socket?: WebSocket;
  private nextRequest = 1;
  private pending = new Map<number, { resolve: (value: Reply) => void; reject: (reason: Error) => void; timer: number }>();
  private stopped = false;
  private frame?: { id: number; full: boolean };
  async connect() {
    try {
      const boot = await fetch('/api/bootstrap').then(r => { if (!r.ok) throw new Error('本地服务无法连接'); return r.json(); });
      if (this.stopped) return;
      const socket = new WebSocket(`ws://${location.host}/socket?token=${encodeURIComponent(boot.token)}`); this.socket = socket; socket.binaryType = 'arraybuffer';
      socket.onopen = () => { this.connected = true; this.dispatchEvent(new Event('status')); };
      socket.onmessage = (event) => { try { this.receive(event.data); } catch (error) { this.error(String(error)); this.stop(); } };
      socket.onclose = () => { this.connected = false; this.dispatchEvent(new Event('status')); for (const p of this.pending.values()) { clearTimeout(p.timer); p.reject(new Error('本地内核连接断开')); } this.pending.clear(); if (!this.stopped) setTimeout(() => this.connect(), 2000); };
    } catch (error) { this.error(String(error)); if (!this.stopped) setTimeout(() => this.connect(), 3000); }
  }
  stop() { this.stopped = true; this.socket?.close(); }
  request(cmd: string, body: Record<string, unknown> = {}): Promise<Reply> {
    if (this.socket?.readyState !== WebSocket.OPEN) return Promise.reject(new Error('本地 C++ 内核尚未连接'));
    const requestId = this.nextRequest++;
    return new Promise((resolve, reject) => {
      const timer = window.setTimeout(() => { this.pending.delete(requestId); reject(new Error('内核响应超时，请检查连接')); }, 30000);
      this.pending.set(requestId, { resolve, reject, timer }); this.socket!.send(JSON.stringify({ ...body, cmd, requestId }));
    });
  }
  private receive(data: string | ArrayBuffer) {
    if (data instanceof ArrayBuffer) {
      const view = new DataView(data); if (view.byteLength < 32 || view.getUint32(0, true) !== 0x32434d56) { this.error('不支持的内核数据格式，请同步重启内核并刷新界面'); return; }
      const kind = view.getUint32(4, true), full = kind === 1, frameId = view.getUint32(8, true), count = view.getUint32(12, true);
      if (this.frame || ![1,2].includes(kind) || view.byteLength !== 32 + count * 28) throw new Error('内核数据帧异常');
      if (full) this.cells.clear();
      const changes: BlockCell[] = [];
      for (let i = 0, offset = 32; i < count; ++i, offset += 28) {
        const cell: BlockCell = { pos: [view.getInt32(offset, true), view.getInt32(offset + 4, true), view.getInt32(offset + 8, true)], stateId: view.getUint32(offset + 12, true), value: view.getUint32(offset + 16, true), renderStateId: view.getUint32(offset + 20, true), motion: view.getUint32(offset + 24, true) };
        if (cell.stateId === 0) this.cells.delete(posKey(cell.pos)); else this.cells.set(posKey(cell.pos), cell); changes.push(cell);
      }
      this.frame = { id: frameId, full };
      this.dispatchEvent(new CustomEvent('cells', { detail: { full, changes } })); return;
    }
    const message = JSON.parse(data);
    if (message.type === 'states') { for (const state of message.states) this.states.set(state.stateId, state); }
    else if (message.type === 'ready') { if (message.protocolVersion !== 3) throw new Error('内核协议版本不同，请重启内核并刷新界面'); this.frame = undefined; this.catalog = message.catalog; this.items = message.items ?? []; this.gameEvents=message.gameEvents??[];this.dispatchEvent(new Event('catalog')); }
    else if (message.type === 'status') { this.status = message; this.trace.retainProbes(message.probes.map((p: Probe) => p.id)); this.dispatchEvent(new Event('status')); }
    else if (message.type === 'trace') {
      if (!this.frame || message.frameId !== this.frame.id || message.reset !== this.frame.full) throw new Error('波形与场景帧不匹配，已停止接收');
      this.trace.append(message); this.dispatchEvent(new Event('trace'));
      this.frame = undefined;
      this.socket?.send(JSON.stringify({ cmd: 'ack', frameId: message.frameId, epoch: message.epoch, traceEnd: message.next }));
    }
    else if (message.type === 'error') { this.error(message.message); const p = this.pending.get(message.requestId); if (p) { clearTimeout(p.timer); p.reject(new Error(message.message)); this.pending.delete(message.requestId); } }
    else if (message.type === 'reply') { const p = this.pending.get(message.requestId); if (p) { clearTimeout(p.timer); p.resolve(message); this.pending.delete(message.requestId); } }
  }
  error(message: string) { this.dispatchEvent(new CustomEvent('error', { detail: message })); }
}
export const connection = new SimulatorConnection();
export function downloadFile(name: string, content: string, type = 'application/json') { const url = URL.createObjectURL(new Blob([content], { type })); const anchor = document.createElement('a'); anchor.href = url; anchor.download = name; anchor.click(); setTimeout(() => URL.revokeObjectURL(url), 1000); }
