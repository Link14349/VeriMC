import { useState } from 'react';
import { type Pos } from './api';
import { valueLabels } from './blockLabels';

export function TargetControls({ stimulus }: { stimulus: (values: Record<string, unknown>) => unknown }) {
  const [face,setFace]=useState('up'),[hitU,setHitU]=useState(.5),[hitV,setHitV]=useState(.5);
  const vertical=face==='up'||face==='down', alongZ=face==='north'||face==='south';
  const normal=['up','south','east'].includes(face)?1:0;
  const hit: Pos=vertical?[hitU,normal,hitV]:alongZ?[hitU,hitV,normal]:[normal,hitV,hitU];
  const coordinates=[vertical||alongZ?'X':'Z',vertical?'Z':'Y'];
  return <>
    <label className="propertyRow"><span>命中面</span><select aria-label="命中面" value={face} onChange={event=>setFace(event.target.value)}>{['up','down','north','south','east','west'].map(side=><option value={side} key={side}>{valueLabels[side]}</option>)}</select></label>
    <button className="targetFace" aria-label="选择标靶命中点" onClick={event=>{
      const bounds=event.currentTarget.getBoundingClientRect();
      setHitU(Math.max(0,Math.min(1,(event.clientX-bounds.left)/bounds.width)));
      setHitV(Math.max(0,Math.min(1,1-(event.clientY-bounds.top)/bounds.height)));
    }}><svg viewBox="0 0 100 100" aria-hidden="true">
      <rect width="100" height="100" fill="#d6cbb3"/>
      <rect x="12" y="12" width="76" height="76" fill="#a9574a"/>
      <rect x="28" y="28" width="44" height="44" fill="#d6cbb3"/>
      <rect x="43" y="43" width="14" height="14" fill="#a9574a"/>
      <circle cx={hitU*100} cy={(1-hitV)*100} r="3.5" fill="#1d292e" stroke="white" strokeWidth="1"/>
    </svg></button>
    {[hitU,hitV].map((value,index)=><label className="propertyRow" key={index}><span>格内 {coordinates[index]}</span><input aria-label={`命中格内 ${coordinates[index]}`} type="number" min="0" max="1" step="0.01" value={value} onChange={event=>(index===0?setHitU:setHitV)(Number(event.target.value))}/></label>)}
    <div className="targetPresets"><button onClick={()=>{setHitU(.5);setHitV(.5);}}>中心</button><button onClick={()=>{setHitU(.05);setHitV(.5);}}>近边缘</button></div>
    <button className="wideButton accentOutline" onClick={()=>stimulus({face,hit,arrow:true})}>箭命中 · 20 gt</button>
    <button className="wideButton" onClick={()=>stimulus({face,hit,arrow:false})}>其他投射物 · 8 gt</button>
    <p className="subtleText">点击图选择命中点，内核按位置计算强度。已有脉冲期间再次命中不会改变输出或延长时间。</p>
  </>;
}
