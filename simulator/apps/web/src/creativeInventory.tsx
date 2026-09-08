import { useEffect, useId, useRef, useState, type KeyboardEvent, type PointerEvent } from 'react';
import type { CatalogItem } from './api';
import { blockLabel, shortName } from './blockLabels';
import { supportLevelInfo } from './interactionState';
import './creativeInventory.css';

export type HotbarSlot = string | null;

const iconFor = (name: string) => {
  const n = shortName(name);
  return n === 'redstone_wire' ? '╋' : n === 'repeater' ? '⇥' : n === 'comparator' ? '▷' : n.includes('torch') ? '♟' : n === 'lever' ? '╱' : n.includes('button') ? '▰' : n === 'observer' ? '◉' : n.includes('lamp') ? '▦' : n.includes('bulb') ? '▥' : n.includes('piston') ? '▣' : n === 'redstone_block' ? '◆' : n.includes('glass') ? '◇' : '▧';
};
function ItemGlyph({ name }: { name: string }) {
  const n = shortName(name);
  return <span aria-hidden="true" className={`creativeItemGlyph ${/redstone|repeater|comparator|torch/.test(n) ? 'isRedstone' : ''}`}>{iconFor(name)}</span>;
}
const slotLabel = (name: HotbarSlot, index: number) => `快捷栏 ${index + 1}：${name ? blockLabel(name) : '空'}`;

export function CreativeHotbar({ slots, selected, onSelect, onInventory }: {
  slots: HotbarSlot[]; selected: number; onSelect: (index: number) => void; onInventory: () => void;
}) {
  return <>
    <div className="creativeHotbar" role="group" aria-label="快捷栏">
      <span className="creativeSelectedName" aria-live="polite">{slots[selected] ? blockLabel(slots[selected]) : '空手'}</span>
      {Array.from({ length: 9 }, (_, index) => {
        const name = slots[index] ?? null;
        return <button key={index} className="creativeSlot" aria-label={slotLabel(name, index)} aria-pressed={selected === index}
          title={`${slotLabel(name, index)}${name ? ` · ${name}` : ''}`} onClick={() => onSelect(index)}>
          <kbd>{index + 1}</kbd>{name && <ItemGlyph name={name}/>}<span className="creativeSlotMark" aria-hidden="true"/>
        </button>;
      })}
    </div>
    <button className="creativeInventoryShortcut" onClick={onInventory} title="打开物品栏 E"><kbd>E</kbd> 物品栏</button>
  </>;
}

export function CreativeInventory({ catalog, slots, selected, connected, onSelect, onAssign, onClear, onClose }: {
  catalog: CatalogItem[]; slots: HotbarSlot[]; selected: number; connected: boolean;
  onSelect: (index: number) => void; onAssign: (name: string, index: number) => void;
  onClear: (index: number) => void; onClose: (resume?: boolean) => void;
}) {
  const dialog = useRef<HTMLDialogElement>(null), search = useRef<HTMLInputElement>(null), ghost = useRef<HTMLDivElement>(null);
  const pointer = useRef({ x: 0, y: 0 });
  const [query, setQuery] = useState(''), [category, setCategory] = useState('全部');
  const [held, setHeld] = useState<string | null>(null), [hovered, setHovered] = useState<string | null>(null);
  const titleId = useId();
  useEffect(() => {
    dialog.current?.showModal();
    search.current?.focus({ preventScroll: true });
    // The caller unmounts this dialog and restores canvas focus. Closing in
    // cleanup would restore focus to an earlier, potentially removed control.
  }, []);
  const close = (resume = false) => { dialog.current?.close(); onClose(resume); };
  const queryText = query.trim().toLowerCase();
  const filtered = catalog.filter(item => {
    if (category === '红石器件' && item.device <= 1) return false;
    if (category === '结构' && item.device > 1) return false;
    return `${blockLabel(item.name)} ${item.name}`.toLowerCase().includes(queryText);
  });
  const assign = (name: string, index: number) => { onAssign(name, index); setHeld(null); };
  const handleKey = (event: KeyboardEvent<HTMLDialogElement>) => {
    // Keep all inventory keystrokes inside the native modal, including the
    // workbench's Enter, Delete and movement shortcuts.
    event.stopPropagation();
    const target = event.target;
    const editing = target instanceof HTMLElement && !!target.closest('input,textarea,select,[contenteditable="true"]');
    if (event.key === 'Escape' || (!editing && event.key.toLowerCase() === 'e' && !event.ctrlKey && !event.metaKey && !event.altKey)) {
      event.preventDefault(); if(!event.repeat)close(event.key !== 'Escape'); return;
    }
    if (editing || event.ctrlKey || event.metaKey || event.altKey || !/^[1-9]$/.test(event.key)) return;
    const index = Number(event.key) - 1;
    // Text entry keeps digits; outside the search field, numbers assign the hovered item.
    if (hovered && filtered.some(item => item.name === hovered)) { event.preventDefault(); assign(hovered, index); }
    else if (!editing) { event.preventDefault(); onSelect(index); }
  };
  const movePointer = (event: PointerEvent<HTMLDialogElement>) => {
    pointer.current = { x: event.clientX, y: event.clientY };
    if (ghost.current) ghost.current.style.transform = `translate(${event.clientX + 16}px,${event.clientY + 16}px)`;
  };
  return <dialog ref={dialog} className="creativeInventory" aria-labelledby={titleId}
    onCancel={event => { event.preventDefault(); close(); }} onKeyDownCapture={handleKey} onKeyUp={event => event.stopPropagation()}
    onPointerMove={movePointer} onContextMenu={event => event.preventDefault()}>
    <header className="creativeInventoryHeader">
      <div><span className="creativeInventoryEyebrow">CREATIVE INVENTORY</span><h1 id={titleId}>物品栏</h1><p>从器件列表中选择，放入下方快捷栏。</p></div>
      <button className="creativeInventoryClose" onClick={() => close(true)} aria-label="关闭物品栏" title="关闭物品栏 Esc"><span aria-hidden="true">×</span><kbd>Esc</kbd></button>
    </header>
    <div className="creativeInventoryFilters">
      <div className="creativeInventoryTabs" role="group" aria-label="物品分类">{['全部','红石器件','结构'].map(name => <button key={name} aria-pressed={category === name} onClick={() => {setCategory(name);setHovered(null);}}>{name}</button>)}</div>
      <label className="creativeInventorySearch"><span aria-hidden="true">⌕</span><input ref={search} type="search" value={query} onChange={event => {setQuery(event.target.value);setHovered(null);}} placeholder="搜索名称或 Minecraft ID" aria-label="搜索物品"/></label>
    </div>
    <div className="creativeInventoryMeta"><span>{filtered.length} 种物品</span><span>{connected ? '来自当前内核器件目录' : '内核已断开 · 世界编辑暂不可用'}</span></div>
    <div className="creativeItemGrid" role="group" aria-label="可用物品" onPointerLeave={() => setHovered(null)}>
      {filtered.map(item => {
        const support = supportLevelInfo(item.supportLevel);
        return <button key={item.name} className={`creativeItem ${held === item.name ? 'isHeld' : ''}`} aria-label={`${blockLabel(item.name)} · ${item.name}`}
          title={`${blockLabel(item.name)} · ${item.name}\n${support.detail}\n点击取物；Shift 点击放入当前格；悬停按 1–9 指定格子`}
          onPointerEnter={() => setHovered(item.name)} onFocus={() => setHovered(item.name)}
          onClick={event => {if (event.shiftKey) assign(item.name,selected); else setHeld(value => value === item.name ? null : item.name);}}>
          <ItemGlyph name={item.name}/><strong>{blockLabel(item.name)}</strong><small>{shortName(item.name)}</small>
          <span className={`creativeSupport level-${support.level}`}>{support.short}</span>
        </button>;
      })}
      {!filtered.length && <div className="creativeInventoryEmpty">{catalog.length ? '没有匹配的物品，试试其他名称。' : connected ? '当前内核尚未提供器件目录。' : '连接内核后加载物品列表。'}</div>}
    </div>
    <footer className="creativeInventoryFooter">
      <div className="creativeInventorySelection" aria-live="polite"><span>{held ? <>手持 <strong>{blockLabel(held)}</strong> · 点击下方格子放入</> : <>当前选择 <strong>{slots[selected] ? blockLabel(slots[selected]) : '空手'}</strong></>}</span><small>右键格子清空</small></div>
      <div className="creativeInventoryHotbar" role="group" aria-label="配置快捷栏">{Array.from({length:9},(_,index) => {
        const name=slots[index] ?? null;
        return <button key={index} className="creativeSlot" aria-label={slotLabel(name,index)} aria-pressed={selected === index}
          title={`${slotLabel(name,index)}${name ? ` · ${name}` : ''} · 右键清空`}
          onClick={() => {if (held) assign(held,index);onSelect(index);}}
          onContextMenu={event => {event.preventDefault();onClear(index);}}>
          <kbd>{index + 1}</kbd>{name && <ItemGlyph name={name}/>}<span className="creativeSlotMark" aria-hidden="true"/>
        </button>;
      })}</div>
      <p className="creativeInventoryInstructions"><span><kbd>E</kbd> / <kbd>Esc</kbd> 关闭</span><span>点击取物 / 放入</span><span>悬停 <kbd>1</kbd>–<kbd>9</kbd> 分配</span><span><kbd>Shift</kbd> 点击放入当前格</span></p>
    </footer>
    {held && <div ref={ghost} className="creativeHeldItem" aria-hidden="true" style={{transform:`translate(${pointer.current.x + 16}px,${pointer.current.y + 16}px)`}}><ItemGlyph name={held}/><span>{blockLabel(held)}</span></div>}
  </dialog>;
}
