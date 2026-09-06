import { useState } from 'react';
import { connection, type Pos } from './api';
import { shortName } from './blockLabels';

type Props={pos:Pos;inspection:Record<string,unknown>;command:(cmd:string,body?:Record<string,unknown>)=>Promise<Record<string,unknown>|undefined>};
const eventLabels:Record<string,string>={step:'脚步',swim:'游泳',flap:'振翅',eat:'进食',drink:'饮用',bounce:'弹跳',equip:'装备',unequip:'卸下装备',explode:'爆炸',block_activate:'方块激活',block_deactivate:'方块关闭',block_open:'方块打开',block_close:'方块合上',block_change:'方块变化',block_place:'放置方块',block_destroy:'破坏方块',block_attach:'连接',block_detach:'断开',container_open:'容器打开',container_close:'容器关闭',projectile_land:'投射物落地',projectile_shoot:'发射投射物',note_block_play:'音符盒演奏',prime_fuse:'引燃',entity_die:'实体死亡',entity_damage:'实体受伤',lightning_strike:'雷击',teleport:'传送',shear:'剪切'};
const eventLabel=(name:string)=>eventLabels[shortName(name)]??(shortName(name).startsWith('resonate_')?'共振 '+shortName(name).slice(9):shortName(name).replaceAll('_',' '));
export function VibrationControls({pos,inspection,command}:Props) {
  const [source,setSource]=useState<Pos>([pos[0]+3,pos[1],pos[2]]),[offset,setOffset]=useState<Pos>([.5,.5,.5]);
  const [event,setEvent]=useState('minecraft:step'),[sneaking,setSneaking]=useState(false),[affected,setAffected]=useState('');
  const vibration=inspection.vibration as {state?:string;remaining?:number;event?:string;distance?:number}|undefined;
  const send=()=>command('stimulate',{pos:source,stimulus:{gameEvent:event,offset,source:{sneaking},...(affected?{affectedBlock:{name:'minecraft:'+affected}}:{})}});
  const coordinates=(label:string,values:Pos,setValues:(value:Pos)=>void,fraction=false)=><div className="vibrationCoordinates"><span>{label}</span>{['X','Y','Z'].map((axis,i)=><label key={axis}>{axis}<input aria-label={`${label} ${axis}`} type="number" min={fraction?0:-29999984} max={fraction?1:29999984} step={fraction?.01:1} value={values[i]} onChange={e=>{const next=[...values] as Pos;next[i]=Number(e.target.value);setValues(next);}}/></label>)}</div>;
  return <div className="vibrationControls">
    <p className="subtleText">信号强度 {Number(inspection.value??0)} / 15 · 比较器频率 {Number(inspection.analog??0)} / 15<br/>{vibration?.state==='selecting'?'等待下一刻选择事件':vibration?.state==='travelling'?`正在传播 · 剩余 ${vibration.remaining} gt`:'没有传播中的振动'}{vibration?.event&&<> · {eventLabel(vibration.event)}</>}</p>
    <label className="miniLabel" htmlFor="vibrationEvent">外部游戏事件</label>
    <select id="vibrationEvent" aria-label="外部游戏事件" className="itemInput" value={event} onChange={e=>setEvent(e.target.value)}>{connection.gameEvents.filter(e=>e.listenable&&e.frequency>0).map(e=><option key={e.name} value={e.name}>{eventLabel(e.name)} · 频率 {e.frequency}</option>)}</select>
    {coordinates('来源方块',source,setSource)}
    <details><summary>精确位置与来源条件</summary>
      {coordinates('格内偏移',offset,setOffset,true)}
      <label className="propertyRow"><span>来源正在潜行</span><input type="checkbox" checked={sneaking} onChange={e=>setSneaking(e.target.checked)}/></label>
      <label className="propertyRow"><span>事件影响的材质</span><select value={affected} onChange={e=>setAffected(e.target.value)}><option value="">未指定</option><option value="white_wool">羊毛</option><option value="white_carpet">羊毛地毯</option><option value="stone">石头</option></select></label>
    </details>
    <button className="wideButton accentOutline" onClick={send}>发送振动事件</button>
    <button className="wideButton" onClick={()=>command('probe',{pos,name:'振动频率',mode:'analog'})}>添加频率探针</button>
    <p className="subtleText">内核计算距离、筛选、遮挡和时序。校频从背面读取红石强度，0 接受全部频率。按钮、门、容器等已接入的器件自动产生事件。</p>
  </div>;
}
