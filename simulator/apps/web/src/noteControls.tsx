import {useState} from 'react';
import {type Pos} from './api';
import {valueLabels} from './blockLabels';

type Props={pos:Pos;head:boolean;inspection:Record<string,unknown>;command:(cmd:string,body?:Record<string,unknown>)=>Promise<Record<string,unknown>|undefined>};
export function NoteControls({pos,head,inspection,command}:Props) {
  const [sound,setSound]=useState('minecraft:block.note_block.harp');
  const runtime=inspection.runtime as {customSound?:string;playCount?:number;lastPlayed?:{tick:number;instrument:string;note:number;pitch:number;sound:string}}|undefined;
  const stimulus=(values:Record<string,unknown>)=>command('stimulate',{pos,stimulus:values});
  if(head)return <>
    <label className="miniLabel" htmlFor="headSound">音符盒声音标识符</label>
    <input id="headSound" className="itemInput" value={sound} onChange={e=>setSound(e.target.value)}/>
    <button className="wideButton" onClick={()=>stimulus({customSound:sound})}>设置头颅声音</button>
    <button className="wideButton" onClick={()=>stimulus({customSound:null})}>清除头颅声音</button>
    <p className="subtleText">当前：{runtime?.customSound??'未设置'}。未设置时，下面的音符盒仍产生振动，但不执行声音输出。浏览器暂不播放声音。</p>
  </>;
  const last=runtime?.lastPlayed;
  return <>
    <button className="wideButton accentOutline" onClick={()=>stimulus({playNote:true})}>演奏当前音符</button>
    <button className="wideButton" onClick={()=>command('interact',{pos})}>调高一音并演奏</button>
    <p className="subtleText">已执行 {runtime?.playCount??0} 次演奏。{last&&<>最近：{last.tick} gt · {valueLabels[last.instrument]??last.instrument} · 音符 {last.note} · 音高倍率 {last.pitch.toFixed(4)}</>}</p>
    <p className="subtleText">上方需要空气，头颅乐器除外。下方材质或上方头颅决定乐器；调音循环 0–24。运行或单步执行演奏事件，振动在触发时发出。浏览器暂不播放声音。</p>
  </>;
}
