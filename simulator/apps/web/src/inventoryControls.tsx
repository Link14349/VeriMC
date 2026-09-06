import { useEffect, useState } from 'react';
import { connection, posKey, type Pos, type BlockCell } from './api';
import { shortName } from './blockLabels';

type Stack = { slot: number; item: string; count: number };
type Props = { pos: Pos; command: (cmd: string, body?: Record<string, unknown>) => Promise<Record<string, unknown> | undefined> };

export function InventoryControls({ pos, command }: Props) {
  const [slots, setSlots] = useState<Stack[]>([]), [size, setSize] = useState(27), [slot, setSlot] = useState(0);
  const [item, setItem] = useState('minecraft:stone'), [count, setCount] = useState(64), [analog, setAnalog] = useState(0), [viewers, setViewers] = useState(0);
  const key = posKey(pos);
  useEffect(() => {
    let active = true, pending = false;
    const refresh = async () => {
      if (pending) return; pending = true;
      const data = await command('inspect', { pos });
      if (active && data) {
        setSlots((data.inventory ?? []) as Stack[]); setSize(Number(data.inventorySize ?? 27));
        setSlot(previous => Math.min(previous, Math.max(0, Number(data.inventorySize ?? 27) - 1)));
        setAnalog(Number(data.analog ?? 0)); setViewers(Number((data.runtime as Record<string, unknown>)?.viewers ?? 0));
      }
      pending = false;
    };
    const changed = (event: Event) => {
      const detail = (event as CustomEvent<{ full: boolean; changes: BlockCell[] }>).detail;
      if (detail.full || detail.changes.some(cell => posKey(cell.pos) === key)) refresh();
    };
    refresh(); connection.addEventListener('cells', changed);
    return () => { active = false; connection.removeEventListener('cells', changed); };
  }, [key]);
  const update = (inventory: Stack[]) => command('stimulate', { pos, stimulus: { inventory } });
  const selected = slots.find(stack => stack.slot === slot);
  const info = connection.items.find(entry => entry.name === (item.includes(':') ? item : 'minecraft:' + item));
  const choose = (index: number) => {
    setSlot(index); const stack = slots.find(value => value.slot === index);
    if (stack) { setItem(stack.item); setCount(stack.count); }
  };
  return <div className="inspectorSection inventoryControls"><label className="miniLabel">库存 · {size} 槽位</label>
    <div className="inventoryGrid">{Array.from({ length: size }, (_, index) => {
      const stack = slots.find(value => value.slot === index);
      return <button key={index} className={slot === index ? 'selected' : ''} aria-label={`槽位 ${index + 1}`} title={stack ? `${index + 1}: ${shortName(stack.item)} × ${stack.count}` : `${index + 1}: 空`} onClick={() => choose(index)}><span>{stack ? shortName(stack.item).slice(0, 2).toUpperCase() : ''}</span>{stack && <b>{stack.count}</b>}</button>;
    })}</div>
    <p className="subtleText">槽位 {slot + 1} · {selected ? `${shortName(selected.item)} × ${selected.count}` : '空'}<br/>比较器读数 {analog} / 15</p>
    <label className="miniLabel" htmlFor="inventoryItem">物品 ID</label>
    <input id="inventoryItem" className="itemInput" aria-label="物品 ID" list="itemDefinitions" value={item} onChange={e => setItem(e.target.value)}/>
    <datalist id="itemDefinitions">{connection.items.filter(entry => entry.name !== 'minecraft:air').map(entry => <option key={entry.name} value={entry.name}>{shortName(entry.name)} · 堆叠 {entry.maxStack}</option>)}</datalist>
    <label className="propertyRow"><span>数量 / {info?.maxStack ?? '—'}</span><input aria-label="物品数量" type="number" min={0} max={info?.maxStack ?? 99} value={count} onChange={e => setCount(Number(e.target.value))}/></label>
    <button className="wideButton accentOutline" onClick={() => update([{ slot, item, count }])}>写入所选槽位</button>
    <button className="wideButton" onClick={() => update([{ slot, item, count: 0 }])}>清空所选槽位</button>
    <button className="wideButton" onClick={() => command('stimulate', { pos, stimulus: { viewers: viewers ? 0 : 1 } })}>{viewers ? '关闭容器' : '打开容器'}</button>
    <p className="subtleText">当前查看人数 {viewers}。支持默认物品；自定义组件仍在开发中。</p>
  </div>;
}
