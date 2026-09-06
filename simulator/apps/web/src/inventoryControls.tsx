import { useEffect, useState } from 'react';
import { connection, posKey, type Pos, type BlockCell } from './api';
import { shortName } from './blockLabels';

type Stack = { slot: number; item: string; count: number };
type Props = { pos: Pos; hopper?: boolean; cart?: boolean; dropper?: boolean; bookshelf?: boolean; pot?: boolean; command: (cmd: string, body?: Record<string, unknown>) => Promise<Record<string, unknown> | undefined> };

export function InventoryControls({ pos, hopper = false, cart = false, dropper = false, bookshelf = false, pot = false, command }: Props) {
  const [slots, setSlots] = useState<Stack[]>([]), [size, setSize] = useState(cart ? 0 : 27), [slot, setSlot] = useState(0);
  const [item, setItem] = useState(bookshelf?'minecraft:book':'minecraft:stone'), [count, setCount] = useState(bookshelf?1:64), [analog, setAnalog] = useState(0), [viewers, setViewers] = useState(0);
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
  }, [key, cart]);
  const update = (inventory: Stack[]) => command('stimulate', { pos, stimulus: cart ? { cartInventory: inventory } : { inventory } });
  const selected = slots.find(stack => stack.slot === slot);
  const info = connection.items.find(entry => entry.name === (item.includes(':') ? item : 'minecraft:' + item));
  const maximum=bookshelf?1:info?.maxStack??99;
  const choose = (index: number) => {
    setSlot(index); const stack = slots.find(value => value.slot === index);
    if (stack) { setItem(stack.item); setCount(stack.count); }
  };
  if (cart && size === 0) return null;
  return <div className="inspectorSection inventoryControls"><label className="miniLabel">{cart ? '矿车库存' : '库存'} · {size} 槽位</label>
    <div className={`inventoryGrid ${bookshelf?'bookshelfGrid':''}`}>{Array.from({ length: size }, (_, index) => {
      const stack = slots.find(value => value.slot === index);
      return <button key={index} className={slot === index ? 'selected' : ''} aria-label={`槽位 ${index + 1}`} title={stack ? `${index + 1}: ${shortName(stack.item)} × ${stack.count}` : `${index + 1}: 空`} onClick={() => choose(index)}><span>{stack ? shortName(stack.item).slice(0, 2).toUpperCase() : ''}</span>{stack && <b>{stack.count}</b>}</button>;
    })}</div>
    <p className="subtleText">槽位 {slot + 1} · {selected ? `${shortName(selected.item)} × ${selected.count}` : '空'}<br/>比较器读数 {analog} / 15</p>
    <label className="miniLabel" htmlFor="inventoryItem">物品 ID</label>
    <input id="inventoryItem" className="itemInput" aria-label="物品 ID" list="itemDefinitions" value={item} onChange={e => setItem(e.target.value)}/>
    <datalist id="itemDefinitions">{connection.items.filter(entry => bookshelf?entry.bookshelfBook:entry.name !== 'minecraft:air').map(entry => <option key={entry.name} value={entry.name}>{shortName(entry.name)} · 堆叠 {bookshelf?1:entry.maxStack}</option>)}</datalist>
    <label className="propertyRow"><span>数量 / {maximum}</span><input aria-label="物品数量" type="number" min={0} max={maximum} value={count} onChange={e => setCount(Number(e.target.value))}/></label>
    <button className="wideButton accentOutline" onClick={() => update([{ slot, item, count }])}>写入所选槽位</button>
    <button className="wideButton" onClick={() => update([{ slot, item, count: 0 }])}>清空所选槽位</button>
    {!hopper && !cart && !dropper && !bookshelf && !pot && <button className="wideButton" onClick={() => command('stimulate', { pos, stimulus: { viewers: viewers ? 0 : 1 } })}>{viewers ? '关闭容器' : '打开容器'}</button>}
    <p className="subtleText">{pot?'单槽容量由物品堆叠上限决定；可通过漏斗或投掷器存取。':bookshelf?'每槽一本书；比较器读最后操作的槽位编号（1–6），取空后仍保留读数。':cart ? '探测铁轨定期通知比较器读取矿车库存。' : hopper ? '先推出，再从上方吸入；红石供电时锁定。' : dropper ? '通电后 4 gt 随机选槽，向前方容器转移一件。向外抛出时记录初始位置与速度，暂停等待环境反馈。' : `当前查看人数 ${viewers}。`}支持默认物品；自定义组件仍在开发中。</p>
  </div>;
}
