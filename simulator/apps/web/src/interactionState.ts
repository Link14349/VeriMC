// 编辑器交互状态：与 React、DOM 和内核连接无关的纯逻辑，便于直接回归测试。
import type { BlockDef, CatalogItem, Pos } from './api';
import { blockLabel, favorites, shortName } from './blockLabels';

export type Tool = 'select' | 'place' | 'erase' | 'probe' | 'interact';
export const toolOrder: { id: Tool; label: string; key: string }[] = [
  { id: 'select', label: '选择', key: '1' },
  { id: 'place', label: '放置', key: '2' },
  { id: 'erase', label: '移除', key: '3' },
  { id: 'probe', label: '探针', key: '4' },
  { id: 'interact', label: '操作', key: '5' },
];
export const toolLabel = (tool: Tool) => toolOrder.find(entry => entry.id === tool)?.label ?? tool;

// 编辑层范围与 Minecraft 26.2 主世界一致；所有入口都要经过 clampLayer。
export const layerRange = { min: -64, max: 319 } as const;
export function clampLayer(value: unknown): number | null {
  let numeric: number;
  if (typeof value === 'number') numeric = value;
  else if (typeof value === 'string') { const text = value.trim(); numeric = text === '' ? Number.NaN : Number(text); }
  else return null;
  if (!Number.isFinite(numeric)) return null;
  return Math.min(layerRange.max, Math.max(layerRange.min, Math.trunc(numeric)));
}
export const layerInRange = (value: number) => Number.isInteger(value) && value >= layerRange.min && value <= layerRange.max;

// 器件支持等级：内核只区分 implemented / partial / externalStimulus，界面不得宣称已完全验证。
export type SupportInfo = { level: string; short: string; detail: string };
export function supportLevelInfo(level: string | undefined): SupportInfo {
  switch (level) {
    case 'implemented':
      return { level, short: '已实现', detail: '电路行为已实现；未声称通过完整的 Minecraft 26.2 原版差分验证。' };
    case 'partial':
      return { level, short: '部分实现', detail: '只实现了部分行为，存在已知缺口，不能当作完整器件使用。' };
    case 'externalStimulus':
      return { level, short: '需环境输入', detail: '环境接触或外部事件需由输入面板提供；器件内部响应按已实现范围执行。' };
    default:
      return { level: level ?? 'unknown', short: '未标注', detail: '内核未提供该器件的支持等级，按未验证处理。' };
  }
}

// 快捷键提示随平台变化，避免在 Windows/Linux 上显示 ⌘。
export const isApplePlatform = (platform: string | undefined, userAgent = '') => /mac|iphone|ipad|ipod/i.test(`${platform ?? ''} ${userAgent}`);
export function shortcutHint(apple: boolean, key: string, shift = false) {
  return apple ? `${shift ? '⇧' : ''}⌘${key}` : `Ctrl+${shift ? 'Shift+' : ''}${key}`;
}

export function filterCatalog(catalog: CatalogItem[], search: string, category: string): CatalogItem[] {
  const query = search.trim().toLowerCase();
  const matched = catalog.filter(entry => {
    const short = shortName(entry.name);
    if (query) return `${blockLabel(entry.name)} ${short}`.toLowerCase().includes(query);
    if (category === '常用') return favorites.includes(short);
    if (category === '器件') return entry.device > 1;
    return entry.device === 1;
  });
  if (!query && category === '常用') matched.sort((a, b) => favorites.indexOf(shortName(a.name)) - favorites.indexOf(shortName(b.name)));
  return matched;
}
export function paletteEmptyReason(input: { connected: boolean; catalogSize: number; search: string; category: string; matches: number }): string {
  if (input.matches > 0) return '';
  if (input.catalogSize === 0) return input.connected ? '内核尚未发送器件库。' : '尚未连接本地内核，器件库为空。';
  const query = input.search.trim();
  if (query) return `没有匹配“${query}”的器件或方块 ID。`;
  return `“${input.category}”分类下没有可用器件。`;
}

export type PlacementProperties = Record<string, string>;
// 放置默认值只来自 catalog.defaultState 对应的方块状态；界面未获得该状态时如实显示“默认”，
// 不把属性枚举的第一个值冒充成默认值。未写入的属性由内核按 defaultState 补齐。
export function catalogDefaults(item: CatalogItem | undefined, states: Map<number, BlockDef>): PlacementProperties | undefined {
  if (!item) return undefined;
  if (item.defaultProperties) return item.defaultProperties;
  const state = states.get(item.defaultState);
  return state && state.name === item.name ? state.properties : undefined;
}
export const placementKeys = ['facing', 'face', 'attachment', 'delay', 'mode', 'type', 'half', 'shape'];
export function placementRows(item: CatalogItem | undefined): string[] {
  if (!item) return [];
  return placementKeys.filter(key => (item.properties[key]?.length ?? 0) > 0);
}
export function effectiveProperty(overrides: PlacementProperties, defaults: PlacementProperties | undefined, key: string): string {
  return overrides[key] ?? defaults?.[key] ?? '';
}
export function placementPayload(item: CatalogItem | undefined, overrides: PlacementProperties): PlacementProperties {
  const payload: PlacementProperties = {};
  if (!item) return payload;
  for (const [key, value] of Object.entries(overrides)) if (value !== '' && item.properties[key]?.includes(value)) payload[key] = value;
  return payload;
}

export const railShapeCycle: Record<string, string> = {
  north_south: 'east_west', east_west: 'north_south',
  ascending_east: 'ascending_south', ascending_south: 'ascending_west', ascending_west: 'ascending_north', ascending_north: 'ascending_east',
  south_east: 'south_west', south_west: 'north_west', north_west: 'north_east', north_east: 'south_east',
};
const horizontalFacings = ['north', 'east', 'south', 'west'];
export type RotateResult = { properties: PlacementProperties; changed: boolean; reason: string };
export function rotatePlacement(item: CatalogItem | undefined, overrides: PlacementProperties, defaults: PlacementProperties | undefined): RotateResult {
  if (!item) return { properties: overrides, changed: false, reason: '器件库尚未提供该方块，无法旋转。' };
  const shapes = item.properties.shape;
  if (shapes?.some(value => value in railShapeCycle)) {
    const current = effectiveProperty(overrides, defaults, 'shape');
    const next = railShapeCycle[current];
    const target = next && shapes.includes(next) ? next : shapes.find(value => value in railShapeCycle)!;
    return { properties: { ...overrides, shape: target }, changed: true, reason: '' };
  }
  const facings = item.properties.facing;
  const options = facings ? horizontalFacings.filter(value => facings.includes(value)) : [];
  if (options.length > 1) {
    const current = effectiveProperty(overrides, defaults, 'facing');
    const next = options[(options.indexOf(current) + 1) % options.length];
    return { properties: { ...overrides, facing: next }, changed: true, reason: '' };
  }
  return { properties: overrides, changed: false, reason: '该方块没有可旋转的水平朝向或铁轨形状。' };
}

export type Message = { id: number; kind: 'error' | 'info'; text: string };
export type InteractionState = {
  tool: Tool;
  placement: { name: string; properties: PlacementProperties };
  selection: Pos | null;
  selectedDef: BlockDef | null;
  layer: number;
  layerText: string;
  cutaway: boolean;
  search: string;
  category: string;
  help: boolean;
  menu: boolean;
  message: Message | null;
  clipboard: BlockDef | null;
  messageSeq: number;
};
export const initialInteractionState: InteractionState = {
  tool: 'place',
  placement: { name: 'minecraft:redstone_wire', properties: {} },
  selection: null,
  selectedDef: null,
  layer: 1,
  layerText: '1',
  cutaway: false,
  search: '',
  category: '常用',
  help: false,
  menu: false,
  message: null,
  clipboard: null,
  messageSeq: 0,
};

export type InteractionAction =
  | { type: 'setTool'; tool: Tool }
  | { type: 'chooseBlock'; name: string }
  | { type: 'setPlacementProperty'; key: string; value: string }
  | { type: 'setPlacementProperties'; properties: PlacementProperties }
  | { type: 'select'; pos: Pos; def: BlockDef | null }
  | { type: 'refreshSelection'; def: BlockDef | null }
  | { type: 'clearSelection' }
  | { type: 'setLayer'; value: unknown }
  | { type: 'typeLayer'; value: string }
  | { type: 'commitLayerText' }
  | { type: 'setCutaway'; value: boolean }
  | { type: 'setSearch'; value: string }
  | { type: 'setCategory'; value: string }
  | { type: 'setHelp'; value: boolean }
  | { type: 'setMenu'; value: boolean }
  | { type: 'notify'; kind: 'error' | 'info'; text: string }
  | { type: 'dismissMessage'; id?: number }
  | { type: 'copy'; def: BlockDef }
  | { type: 'paste' }
  | { type: 'escape' };

const withoutSelection = (state: InteractionState) => ({ ...state, selection: null, selectedDef: null });

export function interactionReducer(state: InteractionState, action: InteractionAction): InteractionState {
  switch (action.type) {
    case 'setTool':
      return { ...(action.tool === 'place' ? withoutSelection(state) : state), tool: action.tool };
    case 'chooseBlock':
      // 从器件库选择新方块时，检查面板必须切换到该方块的放置属性，旧的选中器件同时清除。
      return { ...withoutSelection(state), tool: 'place', placement: { name: action.name, properties: {} } };
    case 'setPlacementProperty': {
      const properties = { ...state.placement.properties };
      if (action.value === '') delete properties[action.key]; else properties[action.key] = action.value;
      return { ...state, placement: { ...state.placement, properties } };
    }
    case 'setPlacementProperties':
      return { ...state, placement: { ...state.placement, properties: { ...action.properties } } };
    case 'select':
      return { ...state, selection: action.pos, selectedDef: action.def };
    case 'refreshSelection':
      return state.selection ? { ...state, selectedDef: action.def } : state;
    case 'clearSelection':
      return withoutSelection(state);
    case 'setLayer': {
      const next = clampLayer(action.value);
      if (next === null) return { ...state, layerText: String(state.layer) };
      return { ...state, layer: next, layerText: String(next) };
    }
    case 'typeLayer': {
      const next = clampLayer(action.value);
      return next === null ? { ...state, layerText: action.value } : { ...state, layer: next, layerText: action.value };
    }
    case 'commitLayerText':
      return { ...state, layerText: String(state.layer) };
    case 'setCutaway':
      return { ...state, cutaway: action.value };
    case 'setSearch':
      return { ...state, search: action.value };
    case 'setCategory':
      return { ...state, category: action.value };
    case 'setHelp':
      return { ...state, help: action.value, menu: action.value ? false : state.menu };
    case 'setMenu':
      return { ...state, menu: action.value };
    case 'notify': {
      const id = state.messageSeq + 1;
      return { ...state, messageSeq: id, message: { id, kind: action.kind, text: action.text } };
    }
    case 'dismissMessage':
      if (action.id !== undefined && state.message?.id !== action.id) return state;
      return { ...state, message: null };
    case 'copy':
      return { ...state, clipboard: action.def };
    case 'paste':
      if (!state.clipboard) return state;
      return { ...withoutSelection(state), tool: 'place', placement: { name: state.clipboard.name, properties: { ...state.clipboard.properties } } };
    case 'escape':
      // 分层退出：先关最上层的弹窗，最后才做整体清理，避免一次 Esc 丢掉多层上下文。
      if (state.help) return { ...state, help: false };
      if (state.menu) return { ...state, menu: false };
      return { ...withoutSelection(state), tool: 'select', message: null };
    default:
      return state;
  }
}

export const offlineReason = '本地内核未连接，该命令暂时不可用。';
export type ShortcutSource = 'search' | 'text' | 'other';
export type ShortcutEvent = { key: string; code: string; ctrlKey: boolean; metaKey: boolean; shiftKey: boolean; altKey: boolean; source: ShortcutSource };
export type ShortcutContext = { help: boolean; menu: boolean; fileBusy: boolean; connected: boolean; running: boolean; tool: Tool; hasSelection: boolean; hasSelectedDef: boolean; hasClipboard: boolean; canUndo: boolean; canRedo: boolean };
export type ShortcutResult =
  | { kind: 'abortFile' }
  | { kind: 'escape' }
  | { kind: 'clearSearch' }
  | { kind: 'focusSearch' }
  | { kind: 'setTool'; tool: Tool }
  | { kind: 'rotate' }
  | { kind: 'copy' }
  | { kind: 'paste' }
  | { kind: 'save' }
  | { kind: 'command'; command: string }
  | { kind: 'removeSelected' }
  | { kind: 'unavailable'; reason: string };
export type ShortcutDecision = { result: ShortcutResult; preventDefault: boolean };

export function resolveShortcut(event: ShortcutEvent, context: ShortcutContext): ShortcutDecision | null {
  const unavailable = (reason = offlineReason): ShortcutDecision => ({ result: { kind: 'unavailable', reason }, preventDefault: true });
  // 文件读写期间只保留取消，避免中途触发编辑命令。
  if (context.fileBusy) return event.key === 'Escape' ? { result: { kind: 'abortFile' }, preventDefault: true } : null;
  if (event.key === 'Escape') return event.source === 'search' ? { result: { kind: 'clearSearch' }, preventDefault: true } : { result: { kind: 'escape' }, preventDefault: false };
  // 帮助弹窗、工程菜单和文本输入中，除 Esc 外不响应全局快捷键。
  if (context.help || context.menu || event.source !== 'other') return null;
  if (event.metaKey || event.ctrlKey) {
    const key = event.key.toLowerCase();
    if (key === 'z') {
      const redo = event.shiftKey;
      if (!context.connected) return unavailable();
      if (redo ? !context.canRedo : !context.canUndo) return unavailable(redo ? '没有可重做的操作。' : '没有可撤销的操作。');
      return { result: { kind: 'command', command: redo ? 'redo' : 'undo' }, preventDefault: true };
    }
    if (key === 's') return context.connected ? { result: { kind: 'save' }, preventDefault: true } : unavailable();
    if (key === 'c') return context.hasSelectedDef ? { result: { kind: 'copy' }, preventDefault: true } : null;
    if (key === 'v') return context.hasClipboard ? { result: { kind: 'paste' }, preventDefault: true } : null;
    return null; // 其余组合键交还浏览器，例如 Ctrl+R 刷新、Ctrl+F 查找。
  }
  if (event.altKey) return null;
  if (event.code === 'Enter' || event.code === 'NumpadEnter') return context.connected ? { result: { kind: 'command', command: context.running ? 'pause' : 'play' }, preventDefault: true } : unavailable();
  if (event.code === 'KeyF') return context.connected ? { result: { kind: 'command', command: 'step' }, preventDefault: true } : unavailable();
  if (event.key === '/') return { result: { kind: 'focusSearch' }, preventDefault: true };
  // R 只在放置工具下调整待放置方块，不影响已选中的器件。
  if (event.key === 'r' || event.key === 'R') return context.tool === 'place' ? { result: { kind: 'rotate' }, preventDefault: true } : null;
  const picked = toolOrder.find(entry => entry.key === event.key);
  if (picked) return { result: { kind: 'setTool', tool: picked.id }, preventDefault: true };
  if (event.key === 'Delete' || event.key === 'Backspace') {
    if (!context.hasSelection) return null;
    return context.connected ? { result: { kind: 'removeSelected' }, preventDefault: true } : unavailable();
  }
  return null;
}

// 视口拾取同样受连接状态约束：只有本地选中不需要内核。
export function pickCommand(tool: Tool, connected: boolean): { command: string } | { blocked: string } | null {
  if (tool === 'select') return null;
  if (!connected) return { blocked: offlineReason };
  if (tool === 'place') return { command: 'place' };
  if (tool === 'erase') return { command: 'remove' };
  if (tool === 'interact') return { command: 'interact' };
  return { command: 'probe' };
}

// 底板铺在编辑层下方一层；越界时返回 null，由界面禁用按钮而不是悄悄夹到合法值。
export const baseplateLayer = (layer: number): number | null => (layerInRange(layer - 1) ? layer - 1 : null);
