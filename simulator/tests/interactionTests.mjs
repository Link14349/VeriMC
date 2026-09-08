// 前端交互回归：直接驱动 interactionState.ts 的真实实现，不断言源码文本。
import assert from 'node:assert/strict';
import { test, after } from 'node:test';
import { readFile, writeFile, mkdir, mkdtemp, realpath, readdir, rm } from 'node:fs/promises';
import { join, dirname } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import ts from '../apps/web/node_modules/typescript/lib/typescript.js';

// 整个前端源码在此就地编译成 ESM：既驱动纯逻辑，也用 react-dom/server 渲染真实界面。
// 编译产物放在 apps/web/node_modules 下，便于解析 react / react-dom / three / lucide-react。
const scratchRoot = await realpath(fileURLToPath(new URL('../apps/web/node_modules/', import.meta.url)));
const scratch = await mkdtemp(join(scratchRoot, '.simulatorInteraction-'));
const webSrc = new URL('../apps/web/src/', import.meta.url);
await mkdir(scratch, { recursive: true });
for (const file of (await readdir(fileURLToPath(webSrc))).filter(name => name.endsWith('.ts') || name.endsWith('.tsx'))) {
  let source = await readFile(new URL(file, webSrc), 'utf8');
  if (file === 'main.tsx') {
    const entry = /^createRoot\(.*$/m;
    assert.match(source, entry, 'main.tsx 的挂载入口发生变化，渲染测试需要同步更新');
    source = source.replace(entry, 'export { App };');
  }
  const output = ts.transpileModule(source, { compilerOptions: { target: ts.ScriptTarget.ES2022, module: ts.ModuleKind.ESNext, jsx: ts.JsxEmit.ReactJSX } }).outputText;
  await writeFile(join(scratch, file.replace(/\.tsx?$/, '.mjs')), output
    .replace("import './styles.css';", '')
    .replace(/(from\s+['"])(\.\/[A-Za-z0-9_]+)(['"])/g, '$1$2.mjs$3'));
}
await writeFile(join(scratch, 'harness.mjs'), "export { createElement } from 'react';\nexport { renderToStaticMarkup } from 'react-dom/server';\n");
const load = name => import(pathToFileURL(join(scratch, name)));
const { createElement, renderToStaticMarkup } = await load('harness.mjs');
const { App } = await load('main.mjs');
const { connection } = await load('api.mjs');
const interaction = await load('interactionState.mjs');
const {
  baseplateLayer, catalogDefaults, clampLayer, effectiveProperty, filterCatalog, initialInteractionState, interactionReducer,
  isApplePlatform, layerRange, offlineReason, paletteEmptyReason, pickCommand, placementPayload, placementRows,
  resolveShortcut, rotatePlacement, shortcutHint, supportLevelInfo, toolLabel,
} = interaction;

const reduce = (actions, start = initialInteractionState) => actions.reduce(interactionReducer, start);
const key = (over = {}) => ({ key: 'x', code: 'KeyX', ctrlKey: false, metaKey: false, shiftKey: false, altKey: false, source: 'other', ...over });
const context = (over = {}) => ({ help: false, menu: false, fileBusy: false, connected: true, running: false, tool: 'select', hasSelection: false, hasSelectedDef: false, hasClipboard: false, canUndo: true, canRedo: true, ...over });

// 器件库固件：lever 的 face 枚举第一个值是 ceiling，而内核 defaultState 是 wall。
const lever = { name: 'minecraft:lever', defaultState: 5000, device: 4, supportLevel: 'implemented', properties: { face: ['ceiling', 'floor', 'wall'], facing: ['north', 'south', 'west', 'east'], powered: ['true', 'false'] } };
const repeater = { name: 'minecraft:repeater', defaultState: 6000, device: 5, supportLevel: 'implemented', properties: { facing: ['north', 'south', 'west', 'east'], delay: ['1', '2', '3', '4'], locked: ['true', 'false'], powered: ['true', 'false'] } };
const rail = { name: 'minecraft:rail', defaultState: 7000, device: 20, supportLevel: 'implemented', properties: { shape: ['north_south', 'east_west', 'ascending_east', 'ascending_west', 'ascending_north', 'ascending_south', 'south_east', 'south_west', 'north_west', 'north_east'], waterlogged: ['true', 'false'] } };
const wire = { name: 'minecraft:redstone_wire', defaultState: 100, device: 2, supportLevel: 'implemented', properties: { power: ['0', '1'], north: ['up', 'side', 'none'] } };
const stone = { name: 'minecraft:stone', defaultState: 1, device: 1, supportLevel: 'implemented', properties: {} };
const sensor = { name: 'minecraft:sculk_sensor', defaultState: 8000, device: 30, supportLevel: 'partial', properties: { sculk_sensor_phase: ['inactive', 'active', 'cooldown'] } };
const plate = { name: 'minecraft:stone_pressure_plate', defaultState: 9000, device: 31, supportLevel: 'externalStimulus', properties: { powered: ['true', 'false'] } };
const catalog = [wire, repeater, lever, rail, stone, sensor, plate];
const knownStates = new Map([[5000, { stateId: 5000, name: 'minecraft:lever', properties: { face: 'wall', facing: 'north', powered: 'false' } }]]);

// 渲染夹具：给 connection 塞入器件库与已知方块状态，然后按给定动作序列渲染整个 App。
const baseline = structuredClone(initialInteractionState);
connection.catalog = catalog;
connection.states.set(5000, { stateId: 5000, name: 'minecraft:lever', properties: { face: 'wall', facing: 'north', powered: 'false' } });
connection.states.set(6001, { stateId: 6001, name: 'minecraft:repeater', properties: { facing: 'east', delay: '2', locked: 'false', powered: 'false' } });
connection.cells.set('3,1,4', { pos: [3, 1, 4], stateId: 6001, value: 9, renderStateId: 6001, motion: 0 });
const restoreInitialState = () => {
  for (const field of Object.keys(initialInteractionState)) delete initialInteractionState[field];
  Object.assign(initialInteractionState, structuredClone(baseline));
};
const renderApp = actions => {
  Object.assign(initialInteractionState, actions.reduce(interactionReducer, structuredClone(baseline)));
  try { return renderToStaticMarkup(createElement(App)); } finally { restoreInitialState(); }
};
const textOf = html => html.replace(/<[^>]*>/g, ' ').replace(/\s+/g, ' ');
after(async () => { assert.equal(dirname(await realpath(scratch)), scratchRoot); await rm(scratch, { recursive: true, force: true }); });

test('从器件库选择新方块会清除旧的选中器件并切换到该方块的放置属性', () => {
  const picked = reduce([
    { type: 'select', pos: [3, 1, 4], def: { stateId: 6001, name: 'minecraft:repeater', properties: { facing: 'east', delay: '2' } } },
    { type: 'setTool', tool: 'select' },
  ]);
  assert.deepEqual(picked.selection, [3, 1, 4]);
  assert.equal(picked.selectedDef.name, 'minecraft:repeater');

  const switched = interactionReducer(picked, { type: 'chooseBlock', name: 'minecraft:lever' });
  assert.equal(switched.selection, null, '旧选择必须清除，否则检查面板会继续显示 A');
  assert.equal(switched.selectedDef, null);
  assert.equal(switched.tool, 'place');
  assert.equal(switched.placement.name, 'minecraft:lever');
  assert.deepEqual(switched.placement.properties, {}, '换方块后不能沿用上一个方块的放置属性');
  // 面板依据：selectedDef 为空时界面渲染放置属性，此处应给出 B 的属性行。
  assert.deepEqual(placementRows(lever), ['facing', 'face']);
});

test('切换方块后旧方块的放置属性不会带到新方块的放置命令里', () => {
  const state = reduce([
    { type: 'chooseBlock', name: 'minecraft:repeater' },
    { type: 'setPlacementProperty', key: 'delay', value: '3' },
    { type: 'chooseBlock', name: 'minecraft:lever' },
  ]);
  assert.deepEqual(placementPayload(lever, state.placement.properties), {});
  const back = interactionReducer(state, { type: 'setPlacementProperty', key: 'facing', value: 'east' });
  assert.deepEqual(placementPayload(lever, back.placement.properties), { facing: 'east' });
});

test('放置默认值取自 catalog.defaultState，不用属性枚举第一个值冒充', () => {
  const defaults = catalogDefaults(lever, knownStates);
  assert.deepEqual(defaults, { face: 'wall', facing: 'north', powered: 'false' });
  assert.equal(effectiveProperty({}, defaults, 'face'), 'wall');
  assert.notEqual(effectiveProperty({}, defaults, 'face'), lever.properties.face[0]);

  // 界面还没收到该状态时如实留空，放置命令不携带属性，由内核按 defaultState 补齐。
  const unknown = catalogDefaults(repeater, knownStates);
  assert.equal(unknown, undefined);
  assert.equal(effectiveProperty({}, unknown, 'facing'), '');
  assert.deepEqual(placementPayload(repeater, {}), {});
  // stateId 命中但方块名不符时不能当作该方块的默认状态。
  assert.equal(catalogDefaults({ ...repeater, defaultState: 5000 }, knownStates), undefined);
});

test('放置命令只发送合法的显式覆盖项', () => {
  assert.deepEqual(placementPayload(repeater, { facing: 'east', delay: '4', face: 'floor', mode: 'compare', shape: '' }), { facing: 'east', delay: '4' });
  assert.deepEqual(placementPayload(repeater, { facing: 'upside_down' }), {});
  assert.deepEqual(placementPayload(undefined, { facing: 'east' }), {});
  assert.deepEqual(placementRows(stone), [], '没有可调属性的方块不应显示空的属性行');
});

test('R 只在放置工具生效，且不抢占 Ctrl/Alt 组合键', () => {
  assert.equal(resolveShortcut(key({ key: 'r', code: 'KeyR' }), context({ tool: 'select' })), null);
  assert.equal(resolveShortcut(key({ key: 'r', code: 'KeyR' }), context({ tool: 'erase' })), null);
  assert.deepEqual(resolveShortcut(key({ key: 'r', code: 'KeyR' }), context({ tool: 'place' })), { result: { kind: 'rotate' }, preventDefault: true });
  assert.deepEqual(resolveShortcut(key({ key: 'R', code: 'KeyR', shiftKey: true }), context({ tool: 'place' })), { result: { kind: 'rotate' }, preventDefault: true });
  assert.equal(resolveShortcut(key({ key: 'r', code: 'KeyR', ctrlKey: true }), context({ tool: 'place' })), null, 'Ctrl+R 必须留给浏览器刷新');
  assert.equal(resolveShortcut(key({ key: 'r', code: 'KeyR', metaKey: true }), context({ tool: 'place' })), null);
  assert.equal(resolveShortcut(key({ key: 'r', code: 'KeyR', altKey: true }), context({ tool: 'place' })), null);
});

test('旋转按当前有效朝向推进，默认未知时选定第一个水平方向', () => {
  const known = rotatePlacement(lever, {}, { face: 'wall', facing: 'north', powered: 'false' });
  assert.deepEqual(known, { properties: { facing: 'east' }, changed: true, reason: '' });
  const unknown = rotatePlacement(repeater, {}, undefined);
  assert.equal(unknown.properties.facing, 'north');
  assert.equal(interaction.rotatePlacement(repeater, unknown.properties, undefined).properties.facing, 'east');
  // 铁轨旋转形状而不是朝向。
  assert.deepEqual(rotatePlacement(rail, { shape: 'east_west' }, undefined).properties, { shape: 'north_south' });
  assert.deepEqual(rotatePlacement(rail, {}, undefined).properties, { shape: 'north_south' });
  const none = rotatePlacement(stone, {}, undefined);
  assert.equal(none.changed, false);
  assert.match(none.reason, /没有可旋转/);
  assert.equal(rotatePlacement(undefined, {}, undefined).changed, false);
});

test('帮助弹窗、工程菜单和输入控件屏蔽全局快捷键，但 Esc 始终可用', () => {
  const enter = key({ key: 'Enter', code: 'Enter' });
  assert.equal(resolveShortcut(key({ key: ' ', code: 'Space' }), context()), null);
  assert.deepEqual(resolveShortcut(key({ key: 'Enter', code: 'NumpadEnter' }), context({ running: true })).result, { kind: 'command', command: 'pause' });
  assert.deepEqual(resolveShortcut(enter, context()).result, { kind: 'command', command: 'play' });
  assert.equal(resolveShortcut(enter, context({ help: true })), null);
  assert.equal(resolveShortcut(enter, context({ menu: true })), null);
  assert.equal(resolveShortcut(key({ key: 'Enter', code: 'Enter', source: 'text' }), context()), null);
  assert.equal(resolveShortcut(key({ key: '1', source: 'text' }), context()), null, '在输入框里按 1 不应切换工具');
  assert.equal(resolveShortcut(key({ key: '1', source: 'search' }), context()), null);
  assert.equal(resolveShortcut(key({ key: 'Delete', source: 'text' }), context({ hasSelection: true })), null);

  assert.deepEqual(resolveShortcut(key({ key: 'Escape' }), context({ menu: true })).result, { kind: 'escape' });
  assert.deepEqual(resolveShortcut(key({ key: 'Escape' }), context({ help: true })).result, { kind: 'escape' });
  assert.deepEqual(resolveShortcut(key({ key: 'Escape', source: 'text' }), context()).result, { kind: 'escape' });
  assert.deepEqual(resolveShortcut(key({ key: 'Escape', source: 'search' }), context()).result, { kind: 'clearSearch' });
});

test('文件读写期间只保留取消操作', () => {
  const busy = context({ fileBusy: true });
  assert.equal(resolveShortcut(key({ key: ' ', code: 'Space' }), busy), null);
  assert.equal(resolveShortcut(key({ key: '2' }), busy), null);
  assert.deepEqual(resolveShortcut(key({ key: 'Escape' }), busy), { result: { kind: 'abortFile' }, preventDefault: true });
});

test('斜杠聚焦搜索，输入控件内不再触发', () => {
  assert.deepEqual(resolveShortcut(key({ key: '/' }), context()), { result: { kind: 'focusSearch' }, preventDefault: true });
  assert.equal(resolveShortcut(key({ key: '/', source: 'search' }), context()), null);
  assert.equal(resolveShortcut(key({ key: '/', ctrlKey: true }), context()), null);
});

test('Esc 逐层退出：先关帮助，再关菜单，最后清理选择与提示', () => {
  const busy = reduce([
    { type: 'select', pos: [1, 2, 3], def: { stateId: 1, name: 'minecraft:lever', properties: {} } },
    { type: 'setTool', tool: 'erase' },
    { type: 'notify', kind: 'error', text: '内核响应超时' },
    { type: 'setSearch', value: '中继' },
    { type: 'setMenu', value: true },
    { type: 'setHelp', value: true },
  ]);
  assert.equal(busy.menu, false, '打开帮助时应先收起工程菜单');
  const withMenu = interactionReducer({ ...busy, menu: true }, { type: 'escape' });
  assert.equal(withMenu.help, false);
  assert.equal(withMenu.menu, true, '第一次 Esc 只关闭最上层的帮助弹窗');
  assert.deepEqual(withMenu.selection, [1, 2, 3]);

  const closedMenu = interactionReducer(withMenu, { type: 'escape' });
  assert.equal(closedMenu.menu, false);
  assert.deepEqual(closedMenu.selection, [1, 2, 3]);
  assert.ok(closedMenu.message);

  const cleaned = interactionReducer(closedMenu, { type: 'escape' });
  assert.equal(cleaned.selection, null);
  assert.equal(cleaned.selectedDef, null);
  assert.equal(cleaned.tool, 'select');
  assert.equal(cleaned.message, null);
  assert.equal(cleaned.search, '中继', '搜索是持久筛选，只有在搜索框内按 Esc 才清空');
  assert.equal(interactionReducer(cleaned, { type: 'escape' }).selection, null);
});

test('平台修饰键提示一致', () => {
  assert.equal(shortcutHint(true, 'S'), '⌘S');
  assert.equal(shortcutHint(false, 'S'), 'Ctrl+S');
  assert.equal(shortcutHint(true, 'Z', true), '⇧⌘Z');
  assert.equal(shortcutHint(false, 'Z', true), 'Ctrl+Shift+Z');
  assert.equal(isApplePlatform('MacIntel'), true);
  assert.equal(isApplePlatform('iPhone'), true);
  assert.equal(isApplePlatform('Win32'), false);
  assert.equal(isApplePlatform('', 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7)'), true);
  assert.equal(isApplePlatform(undefined), false);
  assert.equal(isApplePlatform('Linux x86_64', 'Mozilla/5.0 (X11; Linux x86_64)'), false);
});

test('编辑层所有入口都限制在 -64..319 的有限整数', () => {
  assert.deepEqual([layerRange.min, layerRange.max], [-64, 319]);
  assert.equal(clampLayer('12'), 12);
  assert.equal(clampLayer(' -70 '), -64);
  assert.equal(clampLayer(400), 319);
  assert.equal(clampLayer(3.9), 3);
  assert.equal(clampLayer(-3.9), -3);
  for (const bad of ['', '   ', 'abc', '-', '1e', NaN, Infinity, -Infinity, null, undefined, true, {}, []]) assert.equal(clampLayer(bad), null, `${String(bad)} 不是合法层号`);

  const typed = reduce([{ type: 'typeLayer', value: '-' }]);
  assert.equal(typed.layer, initialInteractionState.layer, '半成品输入不能改变编辑层');
  assert.equal(typed.layerText, '-');
  const negative = interactionReducer(typed, { type: 'typeLayer', value: '-70' });
  assert.equal(negative.layer, -64);
  assert.equal(interactionReducer(negative, { type: 'commitLayerText' }).layerText, '-64');
  assert.equal(reduce([{ type: 'setLayer', value: 9999 }]).layer, 319);
  assert.equal(reduce([{ type: 'setLayer', value: 'abc' }]).layer, initialInteractionState.layer);
  assert.equal(reduce([{ type: 'setLayer', value: 'abc' }]).layerText, String(initialInteractionState.layer));
  assert.equal(reduce([{ type: 'setLayer', value: -64 }, { type: 'setLayer', value: -65 }]).layer, -64);

  assert.equal(baseplateLayer(0), -1);
  assert.equal(baseplateLayer(-64), null, '世界底部下方不能铺底板');
  assert.equal(baseplateLayer(319), 318);
});

test('断线时不能执行的命令被拒绝并给出原因', () => {
  const offline = context({ connected: false, hasSelection: true, hasSelectedDef: true, hasClipboard: true });
  for (const event of [key({ key: 'Enter', code: 'Enter' }), key({ key: 'f', code: 'KeyF' }), key({ key: 's', ctrlKey: true }), key({ key: 'z', ctrlKey: true }), key({ key: 'Delete' })]) {
    const decision = resolveShortcut(event, offline);
    assert.equal(decision.result.kind, 'unavailable', `${event.key} 断线时不应发出命令`);
    assert.equal(decision.result.reason, offlineReason);
    assert.equal(decision.preventDefault, true);
  }
  // 纯本地操作断线时仍然可用。
  assert.deepEqual(resolveShortcut(key({ key: '3' }), offline).result, { kind: 'setTool', tool: 'erase' });
  assert.deepEqual(resolveShortcut(key({ key: 'r', code: 'KeyR' }), context({ connected: false, tool: 'place' })).result, { kind: 'rotate' });
  assert.deepEqual(resolveShortcut(key({ key: 'c', ctrlKey: true }), offline).result, { kind: 'copy' });
  assert.deepEqual(resolveShortcut(key({ key: 'v', ctrlKey: true }), offline).result, { kind: 'paste' });

  assert.deepEqual(pickCommand('place', false), { blocked: offlineReason });
  assert.deepEqual(pickCommand('erase', false), { blocked: offlineReason });
  assert.equal(pickCommand('select', false), null, '选中是本地操作，断线也应可用');
  assert.deepEqual(pickCommand('place', true), { command: 'place' });
  assert.deepEqual(pickCommand('erase', true), { command: 'remove' });
  assert.deepEqual(pickCommand('interact', true), { command: 'interact' });
  assert.deepEqual(pickCommand('probe', true), { command: 'probe' });
});

test('撤销重做在没有历史时给出具体原因', () => {
  const undo = resolveShortcut(key({ key: 'z', ctrlKey: true }), context({ canUndo: false }));
  assert.equal(undo.result.kind, 'unavailable');
  assert.match(undo.result.reason, /撤销/);
  const redo = resolveShortcut(key({ key: 'z', metaKey: true, shiftKey: true }), context({ canRedo: false }));
  assert.match(redo.result.reason, /重做/);
  assert.deepEqual(resolveShortcut(key({ key: 'z', metaKey: true, shiftKey: true }), context()).result, { kind: 'command', command: 'redo' });
  assert.deepEqual(resolveShortcut(key({ key: 'z', ctrlKey: true }), context()).result, { kind: 'command', command: 'undo' });
});

test('复制粘贴不依赖内核，粘贴会切换到放置并清除选择', () => {
  assert.equal(resolveShortcut(key({ key: 'c', ctrlKey: true }), context({ hasSelectedDef: false })), null);
  assert.equal(resolveShortcut(key({ key: 'v', ctrlKey: true }), context({ hasClipboard: false })), null);
  const def = { stateId: 6002, name: 'minecraft:repeater', properties: { facing: 'south', delay: '3', locked: 'false', powered: 'false' } };
  const pasted = reduce([
    { type: 'select', pos: [0, 0, 0], def },
    { type: 'copy', def },
    { type: 'paste' },
  ]);
  assert.equal(pasted.tool, 'place');
  assert.equal(pasted.selection, null);
  assert.equal(pasted.placement.name, 'minecraft:repeater');
  // 粘贴复刻被复制器件的完整方块状态，而不是退回 defaultState。
  assert.deepEqual(placementPayload(repeater, pasted.placement.properties), { facing: 'south', delay: '3', locked: 'false', powered: 'false' });
  assert.equal(effectiveProperty(pasted.placement.properties, catalogDefaults(repeater, knownStates), 'facing'), 'south');
  assert.equal(reduce([{ type: 'paste' }]).placement.name, initialInteractionState.placement.name, '剪贴板为空时粘贴不改变状态');
});

test('器件支持等级如实标注，不宣称完全验证', () => {
  const levels = ['implemented', 'partial', 'externalStimulus', undefined];
  const shorts = levels.map(level => supportLevelInfo(level).short);
  assert.deepEqual(shorts, ['已实现', '部分实现', '需环境输入', '未标注']);
  assert.equal(new Set(shorts).size, shorts.length, '四种等级必须可区分');
  for (const level of levels) {
    const info = supportLevelInfo(level);
    assert.ok(info.detail.length > 0);
    for (const forbidden of ['完全验证', '完整验证', '已通过差分验证', '完全兼容']) assert.ok(!info.detail.includes(forbidden), `${String(level)} 的说明不能出现“${forbidden}”`);
  }
  assert.match(supportLevelInfo('implemented').detail, /未声称/);
  assert.match(supportLevelInfo('partial').detail, /缺口/);
  assert.match(supportLevelInfo('externalStimulus').detail, /输入面板/);
  assert.doesNotMatch(supportLevelInfo('externalStimulus').detail, /内部触发机制未仿真/);
  assert.equal(supportLevelInfo('brandNew').level, 'brandNew');
  assert.equal(supportLevelInfo('brandNew').short, '未标注');
});

test('器件库筛选与空态提示', () => {
  assert.deepEqual(filterCatalog(catalog, '中继', '常用').map(c => c.name), ['minecraft:repeater']);
  assert.deepEqual(filterCatalog(catalog, 'REPEAT', '结构').map(c => c.name), ['minecraft:repeater'], '搜索应跨分类并忽略大小写');
  assert.deepEqual(filterCatalog(catalog, '', '结构').map(c => c.name), ['minecraft:stone']);
  assert.deepEqual(filterCatalog(catalog, '', '器件').map(c => c.name).includes('minecraft:stone'), false);
  assert.deepEqual(filterCatalog(catalog, '  ', '常用').map(c => c.name), filterCatalog(catalog, '', '常用').map(c => c.name));
  const favouriteOrder = filterCatalog(catalog, '', '常用').map(c => c.name);
  assert.deepEqual(favouriteOrder.slice(0, 2), ['minecraft:redstone_wire', 'minecraft:repeater']);

  assert.equal(paletteEmptyReason({ connected: true, catalogSize: 7, search: '', category: '常用', matches: 3 }), '');
  assert.match(paletteEmptyReason({ connected: true, catalogSize: 7, search: '不存在的方块', category: '常用', matches: 0 }), /没有匹配.*不存在的方块/);
  assert.match(paletteEmptyReason({ connected: true, catalogSize: 7, search: '   ', category: '结构', matches: 0 }), /结构.*没有可用器件/);
  assert.match(paletteEmptyReason({ connected: false, catalogSize: 0, search: '', category: '常用', matches: 0 }), /尚未连接/);
  assert.match(paletteEmptyReason({ connected: true, catalogSize: 0, search: '', category: '常用', matches: 0 }), /尚未发送/);
});

test('错误提示保留到显式关闭，普通提示可被替换', () => {
  const failed = reduce([{ type: 'notify', kind: 'error', text: '内核响应超时，请检查连接' }]);
  assert.equal(failed.message.kind, 'error');
  // 自动消失只针对旧消息 id；错误消息不会被界面的定时器带走。
  assert.deepEqual(interactionReducer(failed, { type: 'dismissMessage', id: failed.message.id - 1 }), failed);
  assert.equal(interactionReducer(failed, { type: 'dismissMessage', id: failed.message.id }).message, null);
  assert.equal(interactionReducer(failed, { type: 'dismissMessage' }).message, null);

  const replaced = interactionReducer(failed, { type: 'notify', kind: 'info', text: '工程已导入' });
  assert.equal(replaced.message.kind, 'info');
  assert.notEqual(replaced.message.id, failed.message.id, '新消息需要独立 id，旧定时器不能清掉它');
});

test('工具名称用中文显示，快捷键与工具一一对应', () => {
  assert.deepEqual(interaction.toolOrder.map(t => t.key), ['1', '2', '3', '4', '5']);
  assert.deepEqual(interaction.toolOrder.map(t => toolLabel(t.id)), ['选择', '放置', '移除', '探针', '操作']);
  for (const entry of interaction.toolOrder) {
    assert.deepEqual(resolveShortcut(key({ key: entry.key }), context()).result, { kind: 'setTool', tool: entry.id });
    assert.equal(reduce([{ type: 'setTool', tool: entry.id }]).tool, entry.id);
  }
  assert.equal(toolLabel('place'), '放置');
});

test('放置流程串联：选方块、改属性、旋转、发出的放置载荷', () => {
  let state = reduce([{ type: 'chooseBlock', name: 'minecraft:lever' }]);
  const defaults = catalogDefaults(lever, knownStates);
  assert.equal(effectiveProperty(state.placement.properties, defaults, 'face'), 'wall');
  state = interactionReducer(state, { type: 'setPlacementProperty', key: 'face', value: 'floor' });
  assert.equal(effectiveProperty(state.placement.properties, defaults, 'face'), 'floor');
  const rotated = rotatePlacement(lever, state.placement.properties, defaults);
  state = interactionReducer(state, { type: 'setPlacementProperties', properties: rotated.properties });
  assert.deepEqual(placementPayload(lever, state.placement.properties), { face: 'floor', facing: 'east' });
  // 选空值等于回到内核默认。
  state = interactionReducer(state, { type: 'setPlacementProperty', key: 'face', value: '' });
  assert.deepEqual(placementPayload(lever, state.placement.properties), { facing: 'east' });
  assert.equal(effectiveProperty(state.placement.properties, defaults, 'face'), 'wall');
});

// ---- 真实界面渲染：用 react-dom/server 渲染编译后的 App，断言实际产生的 DOM。 ----
test('界面渲染：选中器件后从器件库选择另一个方块，检查面板切到新方块的放置属性', () => {
  const selectA = { type: 'select', pos: [3, 1, 4], def: { stateId: 6001, name: 'minecraft:repeater', properties: { facing: 'east', delay: '2', locked: 'false', powered: 'false' } } };
  const selected = textOf(renderApp([selectA]));
  assert.match(selected, /器件检查/);
  assert.match(selected, /中继器/);
  assert.doesNotMatch(selected, /放置属性/);

  const switched = renderApp([selectA, { type: 'chooseBlock', name: 'minecraft:lever' }]);
  const shown = textOf(switched);
  assert.match(shown, /放置设置/, '选新方块后应回到放置面板');
  assert.match(shown, /放置属性/);
  assert.match(shown, /拉杆/);
  assert.doesNotMatch(shown, /器件检查/, '旧的选中器件必须清除');
  assert.match(shown, /当前工具 · 放置 · 拉杆/, '当前工具需要有文字显示');
  // 附着面下拉必须选中 defaultState 的 wall，而不是枚举第一个值 ceiling。
  const face = switched.slice(switched.indexOf('附着面'), switched.indexOf('</select>', switched.indexOf('附着面')));
  assert.match(face, /<option value="wall" selected=""/, `附着面应选中 defaultState 的 wall：${face}`);
  assert.doesNotMatch(face, /<option value="ceiling" selected/, '不能预选属性枚举的第一个值');
  assert.doesNotMatch(face, /默认（由内核决定）/, '已知 defaultState 时不显示占位默认项');
});

test('界面渲染：内核未提供 defaultState 时如实显示“默认”，不预选枚举第一项', () => {
  const html = renderApp([{ type: 'chooseBlock', name: 'minecraft:rail' }]);
  const shape = html.slice(html.indexOf('形状'), html.indexOf('</select>', html.indexOf('形状')));
  assert.match(shape, /<option value="" selected="">默认（由内核决定）<\/option>/, `未知默认时应选中空值而不是第一个形状：${shape}`);
  assert.doesNotMatch(shape, /<option value="north_south" selected/);
  assert.match(textOf(html), /尚未收到器件默认属性/);
  assert.match(textOf(html), /当前工具 · 放置 · 铁轨/);
});

test('界面渲染：器件库展示支持等级，空搜索结果给出明确提示', () => {
  const listed = textOf(renderApp([{ type: 'setCategory', value: '器件' }]));
  for (const label of ['已实现', '部分实现', '需环境输入']) assert.match(listed, new RegExp(label));
  assert.doesNotMatch(listed, /完全验证|完整验证|完全兼容/);

  const empty = textOf(renderApp([{ type: 'setSearch', value: '不存在的方块' }]));
  assert.match(empty, /没有匹配.*不存在的方块.*的器件或方块 ID/);
  assert.match(empty, /清除搜索/);
});

test('界面渲染：断线时命令按钮禁用，帮助弹窗使用原生 dialog', () => {
  const html = renderApp([]);
  // App 内的 connected 初值为 false，此次渲染即断线状态。
  assert.match(html, /正在连接本地内核/);
  const run = html.slice(html.indexOf('runButton'), html.indexOf('runButton') + 400);
  assert.match(run, /disabled/, '断线时运行按钮必须禁用');
  assert.match(html, /<fieldset class="offlineGuard[^"]*" disabled=""/, '子面板的表单控件在断线时应整体禁用');
  assert.match(html, /<dialog class="helpModal"/, '帮助弹窗使用原生 dialog 以获得焦点限制');
  assert.doesNotMatch(html, /open=""/, '帮助弹窗默认关闭');
  assert.match(textOf(html), /当前工具 · 放置/);
  assert.match(textOf(html), new RegExp(shortcutHint(isApplePlatform(typeof navigator === 'undefined' ? '' : navigator.platform, typeof navigator === 'undefined' ? '' : navigator.userAgent), 'S').replace(/\+/g, '\\+')));
});

test('界面渲染：编辑层输入与底板按钮遵守 -64..319', () => {
  const bottom = renderApp([{ type: 'setLayer', value: -200 }]);
  assert.match(bottom, /value="-64"/);
  assert.match(textOf(bottom), /编辑层已在 Y -64/);
  const top = renderApp([{ type: 'setLayer', value: 9999 }]);
  assert.match(top, /value="319"/);
  assert.match(textOf(top), /底板铺在 Y 318/);
});

test('界面渲染：错误提示带关闭按钮，普通提示不带错误标记', () => {
  const failed = renderApp([{ type: 'notify', kind: 'error', text: '内核响应超时，请检查连接' }]);
  assert.match(failed, /class="toast error"/);
  assert.match(failed, /aria-label="关闭提示"/);
  assert.match(textOf(failed), /内核响应超时/);
  const done = renderApp([{ type: 'notify', kind: 'info', text: '工程已导入' }]);
  assert.match(done, /class="toast info"/);
  assert.doesNotMatch(done, /class="toast error"/);
});


test('新内核目录提供默认属性时无需等待场景使用该状态', () => {
  const defaults = { face:'wall', facing:'east', powered:'false' };
  assert.deepEqual(catalogDefaults({...lever,defaultProperties:defaults},new Map()),defaults);
  assert.equal(effectiveProperty({},catalogDefaults({...lever,defaultProperties:defaults},new Map()),'face'),'wall');
});
test('快捷工具切换到放置也清除旧选择', () => {
  const selected=reduce([{type:'select',pos:[1,2,3],def:{stateId:6000,name:repeater.name,properties:{facing:'west'}}}]);
  const placing=interactionReducer(selected,{type:'setTool',tool:'place'});
  assert.equal(placing.selection,null);assert.equal(placing.selectedDef,null);assert.equal(placing.tool,'place');
});
