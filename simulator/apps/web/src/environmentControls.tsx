import { useEffect, useState } from 'react';
import { Zap } from 'lucide-react';
import { connection, posKey, type BlockDef, type Pos, type BlockCell } from './api';
import { shortName } from './blockLabels';
import { TargetControls } from './targetControls';
import { createCoalescedRefresh } from './coalescedRefresh';

type Props = { pos: Pos; block: BlockDef; command: (cmd: string, body?: Record<string, unknown>) => Promise<Record<string, unknown> | undefined> };

export function EnvironmentControls({ pos, block, command }: Props) {
  const [entities, setEntities] = useState(1), [living, setLiving] = useState(1);
  const [sky, setSky] = useState(15), [angle, setAngle] = useState(0);
  const [pages, setPages] = useState(15), [page, setPage] = useState(1);
  const [inspection, setInspection] = useState<Record<string, unknown>>({});
  const name = shortName(block.name), plate = name.endsWith('pressure_plate'), daylight = name === 'daylight_detector';
  const lectern = name === 'lectern', rod = name.endsWith('lightning_rod'), target = name === 'target';
  const detector = name === 'detector_rail', tripwire = name === 'tripwire';
  const button = name.endsWith('_button'), stoneButton = name === 'stone_button' || name === 'polished_blackstone_button';
  const supported = plate || daylight || lectern || rod || target || detector || tripwire || button;
  const key = posKey(pos);
  useEffect(() => {
    if (!supported) return;
    const refresh = createCoalescedRefresh(() => command('inspect', { pos }), info => {
      if (info) setInspection(info);
    }, error => connection.error(String(error)));
    const onCells = (event: Event) => {
      const detail = (event as CustomEvent<{ full: boolean; changes: BlockCell[] }>).detail;
      if (detail.full || detail.changes.some(cell => posKey(cell.pos) === key)) refresh.request();
    };
    connection.addEventListener('cells', onCells); refresh.request();
    return () => { refresh.dispose(); connection.removeEventListener('cells', onCells); };
  }, [key, block.name]);
  if (!supported) return null;
  const stimulus = (values: Record<string, unknown>) => command('stimulate', { pos, stimulus: values });
  const runtime = (inspection.runtime ?? {}) as Record<string, number>;
  const carts = (inspection.runtime as {carts?: Array<{type: string}>})?.carts ?? [];
  const numberField = (label: string, value: number, change: (next: number) => void, maximum: number, minimum = 0) =>
    <label className="propertyRow"><span>{label}</span><input aria-label={label} type="number" min={minimum} max={maximum} value={value} onChange={e => change(Number(e.target.value))}/></label>;
  return <div className="inspectorSection environmentControls"><label className="miniLabel"><Zap size={12}/>环境输入</label>
    {button && <>
      <button className="wideButton accentOutline" onClick={() => stimulus({arrows:1})}>箭留在按钮内</button>
      <button className="wideButton" onClick={() => stimulus({arrows:1, pressedArrows:0})}>箭只触及弹起部分</button>
      <button className="wideButton" onClick={() => stimulus({arrows:0})}>移走所有箭</button>
      <p className="subtleText">当前箭数 {runtime.arrows ?? 0}；其中 {runtime.pressedArrows ?? 0} 支仍触及按下形状。{stoneButton ? '石按钮不受箭矢触发，手动按下持续 20 gt。' : '木按钮每 30 gt 复查，箭仍在内时保持按下。仅触及弹起部分的箭会在复查时产生同刻释放与重按。'}</p>
    </>}
    {tripwire && <>
      <p className="subtleText">提供触及这段线的有效实体数量。</p>
      {numberField('绊线接触数量', entities, setEntities, 1000000)}
      <button className="wideButton accentOutline" onClick={() => stimulus({entities})}>触及绊线</button>
      <button className="wideButton" onClick={() => stimulus({entities:0})}>离开绊线</button>
      <button className="wideButton" onClick={() => stimulus({shear:true})}>用剪刀剪断</button>
      <p className="subtleText">当前接触 {runtime.entities ?? 0}。每 10 gt 复查；普通移除会触发断线脉冲，剪刀解除该段触发。</p>
    </>}
    {detector && <>
      <p className="subtleText">提供检测区内的矿车接触，轨道自动输出信号。</p>
      <button className="wideButton" onClick={() => stimulus({carts:[{type:'minecart'}]})}>普通矿车进入</button>
      <button className="wideButton" onClick={() => stimulus({carts:[{type:'chest_minecart',inventory:[]}]})}>箱子矿车进入</button>
      <button className="wideButton" onClick={() => stimulus({carts:[{type:'hopper_minecart',inventory:[]}]})}>漏斗矿车进入</button>
      <button className="wideButton" onClick={() => stimulus({carts:[]})}>矿车全部离开</button>
      <p className="subtleText">当前 {carts.length} 辆。离开与库存通知在下一次 20 gt 检查时生效；三维矿车是接触标记，不计算运动。</p>
    </>}
    {plate && <>
      <p className="subtleText">输入触及压力板的实体数量，已排除旁观者和不触发方块的实体。</p>
      {numberField('实体总数', entities, setEntities, 1000000)}
      {numberField('其中生物', living, setLiving, entities)}
      <button className="wideButton accentOutline" onClick={() => stimulus({ entities, livingEntities: living })}>应用占用数量</button>
      <button className="wideButton" onClick={() => stimulus({ entities: 0, livingEntities: 0 })}>全部离开</button>
      <p className="subtleText">当前占用 {runtime.entities ?? 0} · 生物 {runtime.livingEntities ?? 0}。释放在器件下一次检测时生效。</p>
    </>}
    {daylight && <>
      {numberField('有效天空亮度', sky, setSky, 15)}
      {numberField('太阳角度 / 度', angle, setAngle, 360)}
      <button className="wideButton accentOutline" onClick={() => stimulus({ skyBrightness: sky, sunAngle: angle })}>应用天空输入</button>
      <p className="subtleText">在下一个 20 gt 边界检测。当前天空亮度 {runtime.skyBrightness ?? 0}，太阳角度 {runtime.sunAngle ?? 0}°。操作器件可切换反向模式。</p>
    </>}
    {lectern && <>
      {numberField('书本总页数', pages, setPages, 100, 1)}
      <button className="wideButton" onClick={() => stimulus({ pages })}>放入书本 / 回到首页</button>
      {numberField('翻到第几页', page, setPage, runtime.pages || 100, 1)}
      <button className="wideButton accentOutline" onClick={() => stimulus({ page: page - 1 })}>翻页</button>
      <button className="wideButton" onClick={() => stimulus({ pages: 0 })}>取出书本</button>
      <p className="subtleText">{runtime.pages ? `第 ${(runtime.page ?? 0) + 1} / ${runtime.pages} 页` : '尚未放入书本'} · 比较器读数 {Number(inspection.analog ?? 0)}。翻页脉冲持续 2 gt。</p>
    </>}
    {rod && <><button className="wideButton accentOutline" onClick={() => stimulus({})}>施加雷击</button><p className="subtleText">产生 8 gt 红石脉冲。天气、火焰和实体伤害不在当前环境模型内。</p></>}
    {target && <TargetControls stimulus={stimulus}/>}
  </div>;
}
