import {useState} from 'react';
import {connection, type Pos} from './api';

type Props={pos:Pos;level:number;command:(cmd:string,body?:Record<string,unknown>)=>Promise<Record<string,unknown>|undefined>};
export function ComposterControls({pos,level,command}:Props) {
  const [item,setItem]=useState('minecraft:pumpkin_pie');
  const info=connection.items.find(value=>value.name===(item.includes(':')?item:'minecraft:'+item));
  return <>
    <p className="subtleText">堆肥层数 {level} / 8 · {level===8?'已成熟':level===7?'等待成熟':'可以投料'}</p>
    <label className="miniLabel" htmlFor="compostItem">投入物品</label>
    <input id="compostItem" aria-label="堆肥物品" className="itemInput" list="compostItems" value={item} onChange={event=>setItem(event.target.value)}/>
    <datalist id="compostItems">{connection.items.filter(value=>value.compostChance!==undefined).map(value=><option key={value.name} value={value.name}>{Math.round(value.compostChance!*100)}%</option>)}</datalist>
    <p className="subtleText">{info?.compostChance!==undefined?`升层概率 ${Math.round(info.compostChance*100)}%${level===0?'；首件必定升层':''}`:'此物品不能堆肥'}</p>
    <button className="wideButton accentOutline" disabled={level>=7||info?.compostChance===undefined} onClick={()=>command('stimulate',{pos,stimulus:{compostItem:item}})}>投入一件</button>
    <button className="wideButton" onClick={()=>command('probe',{pos,name:'堆肥层数',mode:'analog'})}>添加层数探针</button>
    <p className="subtleText">每次输入代表投入一件，即使没有升层也会消耗。第 7 层等待 20 gt 成熟。上方漏斗可投料，下方漏斗可取骨粉；手动取出掉落物尚未开放。</p>
  </>;
}
