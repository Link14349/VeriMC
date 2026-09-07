import assert from 'node:assert/strict';
import { test, after } from 'node:test';
import { readFile, writeFile, mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { pathToFileURL } from 'node:url';
import ts from '../apps/web/node_modules/typescript/lib/typescript.js';

const scratch = await mkdtemp(join(tmpdir(), 'simulatorWebTests-'));
for (const name of ['traceHistory', 'api', 'coalescedRefresh']) {
  const source = await readFile(new URL(`../apps/web/src/${name}.ts`, import.meta.url), 'utf8');
  const output = ts.transpileModule(source, { compilerOptions: { target: ts.ScriptTarget.ES2022, module: ts.ModuleKind.ESNext } }).outputText;
  await writeFile(join(scratch, `${name}.mjs`), output.replace("'./traceHistory'", "'./traceHistory.mjs'"));
}
after(() => rm(scratch, { recursive: true, force: true }));
const { TraceHistory } = await import(pathToFileURL(join(scratch, 'traceHistory.mjs')));
const { SimulatorConnection } = await import(pathToFileURL(join(scratch, 'api.mjs')));
const { createCoalescedRefresh } = await import(pathToFileURL(join(scratch, 'coalescedRefresh.mjs')));
const frame = (from, edges, reset = false, epoch = 1) => ({ frameId: 1, from, next: from + edges.length, reset, epoch, edges });

const flush = () => new Promise(resolve => setImmediate(resolve));
const deferred = () => { let resolve, reject; const promise = new Promise((a,b) => { resolve=a; reject=b; }); return {promise,resolve,reject}; };
test('old backend rejects project transfers before sending an unknown command', async () => {
  const connection = new SimulatorConnection();
  connection.request = () => assert.fail('old backend received a file command');
  await assert.rejects(connection.projectFile({ signal: new AbortController().signal, progress: assert.fail }), /后台仍是旧版本.*重启模拟器服务/);
});
test('inspector reads final state after updates during both initial and trailing requests', async () => {
  const reads=[], shown=[];
  const refresh=createCoalescedRefresh(() => { const read=deferred();reads.push(read);return read.promise; }, value => shown.push(value), assert.fail);
  refresh.request();for(let i=0;i<100;++i)refresh.request();
  assert.equal(reads.length,1);
  reads[0].resolve('old');await flush();assert.equal(reads.length,2);
  for(let i=0;i<100;++i)refresh.request();
  reads[1].resolve('middle');await flush();assert.equal(reads.length,3);
  reads[2].resolve('latest');await flush();
  assert.deepEqual(shown,['old','middle','latest']);assert.equal(reads.length,3);
  refresh.dispose();
});
test('selection disposal suppresses stale replies and queued reads', async () => {
  const read=deferred();let calls=0;
  const refresh=createCoalescedRefresh(() => {++calls;return read.promise;},assert.fail,assert.fail);
  refresh.request();refresh.request();refresh.dispose();read.resolve('old selection');await flush();
  refresh.request();assert.equal(calls,1);
});
test('failed inspector request reports error and retains a pending refresh', async () => {
  const reads=[], shown=[], errors=[];
  const refresh=createCoalescedRefresh(() => {const read=deferred();reads.push(read);return read.promise;},value=>shown.push(value),error=>errors.push(error.message));
  refresh.request();refresh.request();reads[0].reject(new Error('disconnected'));await flush();
  assert.equal(reads.length,2);reads[1].resolve('recovered');await flush();
  assert.deepEqual(errors,['disconnected']);assert.deepEqual(shown,['recovered']);refresh.dispose();
});

test('trace retains ordered same-tick edges and rejects gaps atomically', () => {
  const trace = new TraceHistory(5);
  trace.append(frame(0, [[1,0,1,0],[1,1,2,15],[1,1,3,0]], true));
  assert.throws(() => trace.append(frame(4, [[1,2,4,15]])), /不连续/);
  assert.throws(() => trace.append(frame(3, [[1,0,4,15]])), /异常/);
  assert.equal(trace.size, 3);
  trace.append(frame(3, [[1,2,4,15]]));
  const window = trace.window(1, 1, 1);
  assert.equal(window.initial, 0);
  assert.deepEqual(window.rows.slice(window.begin, window.end), [[1,1,2,15],[1,1,3,0]]);
});

test('history truncation preserves known levels and marks older intervals unavailable', () => {
  const trace = new TraceHistory(3);
  trace.append(frame(10, [[1,0,1,15],[2,1,2,0],[2,2,3,15],[2,3,4,0],[2,4,5,15]], true));
  assert.equal(trace.dropped, 12);
  assert.equal(trace.size, 3);
  assert.equal(trace.window(1, 0, 4).start, 2);
  assert.equal(trace.window(1, 0, 4).initial, 15);
  assert.equal(trace.window(1, 0, 1).initial, undefined);
  trace.append(frame(0, [[3,0,1,0]], true, 2));
  assert.equal(trace.dropped, 0);
  assert.equal(trace.window(1, 0, 5).initial, undefined);
});

test('compacted channel indices match a reference scan over sustained traffic', () => {
  const trace = new TraceHistory(4096), all = [];
  for (let batch = 0; batch < 200; ++batch) {
    const edges = Array.from({ length: 256 }, (_, i) => { const seq = batch * 256 + i; return [seq % 8, seq >> 3, seq, seq % 16]; });
    trace.append(frame(all.length, edges, batch === 0)); all.push(...edges);
    for (let id = 0; id < 8; ++id) {
      const left = Math.max(trace.startTick, (all.length >> 3) - 15), right = left + 8;
      const visible = trace.window(id, left, right);
      assert.deepEqual(visible.rows.slice(visible.begin, visible.end), all.filter(e => e[0] === id && e[1] >= left && e[1] <= right));
      assert.equal(visible.initial, all.findLast(e => e[0] === id && e[1] < left)?.[3]);
    }
  }
  assert.equal(trace.size, 4096); assert.equal(trace.dropped, all.length - 4096);
});

class TestSocket {
  static OPEN = 1;
  static latest;
  readyState = 1;
  sent = [];
  constructor() { TestSocket.latest = this; }
  send(value) { this.sent.push(JSON.parse(value)); }
  close() { this.readyState = 3; this.onclose?.(); }
  message(value) { this.onmessage({ data: typeof value === 'object' && !(value instanceof ArrayBuffer) ? JSON.stringify(value) : value }); }
}
globalThis.WebSocket = TestSocket;
globalThis.location = { host: '127.0.0.1:28765' };
globalThis.fetch = async () => ({ ok: true, json: async () => ({ token: 'test' }) });
if (!globalThis.CustomEvent) globalThis.CustomEvent = class extends Event { constructor(type, init) { super(type); this.detail = init.detail; } };
const binaryFrame = (id, full) => {
  const bytes = new ArrayBuffer(32), view = new DataView(bytes);
  view.setUint32(0, 0x32434d56, true); view.setUint32(4, full ? 1 : 2, true); view.setUint32(8, id, true);
  return bytes;
};

test('browser ACK follows trace processing; invalid terminal frame is not acknowledged', async () => {
  const connection = new SimulatorConnection(); await connection.connect();
  const socket = TestSocket.latest; socket.onopen();
  socket.message({ type: 'ready', protocolVersion: 3, catalog: [] });
  socket.message(binaryFrame(11, true)); assert.equal(socket.sent.length, 0);
  let observed = false;
  connection.addEventListener('trace', () => { observed = true; assert.equal(socket.sent.length, 0); assert.equal(connection.trace.size, 1); });
  socket.message({ type: 'trace', ...frame(0, [[1,0,0,0]], true), frameId: 11 });
  assert.equal(observed, true);
  assert.deepEqual(socket.sent, [{ cmd: 'ack', frameId: 11, epoch: 1, traceEnd: 1 }]);
  socket.message(binaryFrame(12, false));
  socket.message({ type: 'trace', ...frame(2, [[1,1,1,15]]), frameId: 12 });
  assert.equal(socket.sent.length, 1); assert.equal(connection.connected, false);
});
