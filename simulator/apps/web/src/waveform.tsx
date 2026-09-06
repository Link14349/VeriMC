import { useEffect, useRef, useState } from 'react';
import { ZoomIn, ZoomOut, ArrowRightToLine, Download, Trash2, Activity } from 'lucide-react';
import { connection, downloadFile, type Status } from './api';
const probeColors = ['#f0c88a','#80bec4','#bda2de','#bdce88','#e49e8a','#89aeeb'];
export function Waveform({ status, command }: { status: Status; command: (cmd: string, body?: Record<string, unknown>) => Promise<Record<string, unknown> | undefined> }) {
  const canvasRef = useRef<HTMLCanvasElement>(null), wrapRef = useRef<HTMLDivElement>(null);
  const [span,setSpan] = useState(80), [follow,setFollow] = useState(true), [start,setStart] = useState(0), [cursors,setCursors] = useState<[number | null,number | null]>([null,null]);
  const values = useRef({ status,span,follow,start,cursors }); values.current = { status,span,follow,start,cursors };
  const draw = useRef<() => void>(() => {});
  useEffect(() => {
    const canvas = canvasRef.current!, wrap = wrapRef.current!;
    draw.current = () => {
      const { status: s, span: windowSpan, follow: auto, start: offset, cursors: cursor } = values.current;
      const width = wrap.clientWidth, height = Math.max(128,s.probes.length*42+30), dpr = Math.min(devicePixelRatio,2);
      if (canvas.width !== width*dpr || canvas.height !== height*dpr) { canvas.width = width*dpr; canvas.height = height*dpr; canvas.style.width=`${width}px`; canvas.style.height=`${height}px`; }
      const ctx = canvas.getContext('2d')!; ctx.setTransform(dpr,0,0,dpr,0,0); ctx.clearRect(0,0,width,height);
      const left = auto ? Math.max(0,s.tick-windowSpan+windowSpan*.18) : offset;
      const x = (tick: number) => (tick-left)/windowSpan*width;
      ctx.font = '10px ui-monospace, monospace'; ctx.textBaseline = 'top';
      const step = Math.max(1,Math.pow(10,Math.floor(Math.log10(windowSpan/8))));
      for (let t=Math.ceil(left/step)*step;t<=left+windowSpan;t+=step) { const px=x(t); ctx.strokeStyle='#2e363a'; ctx.lineWidth=1; ctx.beginPath(); ctx.moveTo(px+.5,22); ctx.lineTo(px+.5,height); ctx.stroke(); if (Math.round(t/step)%2===0) { ctx.fillStyle='#78878c'; ctx.fillText(`${Math.round(t)} gt`,px+5,5); } }
      s.probes.forEach((probe,index) => {
        const bottom = 22+(index+1)*42-7, top=bottom-25; ctx.strokeStyle='#2b3337'; ctx.beginPath(); ctx.moveTo(0,bottom+6);ctx.lineTo(width,bottom+6);ctx.stroke();
        const edges = connection.edges.filter(edge => edge[0]===probe.id); let last=0; let initialized=false;
        ctx.strokeStyle=probeColors[index%probeColors.length];ctx.lineWidth=1.7;ctx.beginPath();
        for (const edge of edges) {
          const [,tick,,value]=edge;
          if (tick<left) { last=value; continue; }
          if (!initialized) { ctx.moveTo(0,bottom-last/15*25); initialized=true; }
          if (tick>left+windowSpan) break;
          ctx.lineTo(x(tick),bottom-last/15*25);ctx.lineTo(x(tick),bottom-value/15*25);last=value;
        }
        if (!initialized && edges.length) ctx.moveTo(0,bottom-last/15*25);
        if (edges.length) ctx.lineTo(Math.min(width,x(s.tick)),bottom-last/15*25);ctx.stroke();
        if (!edges.length) {ctx.fillStyle='#647177';ctx.fillText('等待采样',12,top+7);}
      });
      if (s.tick>=left && s.tick<=left+windowSpan) { ctx.strokeStyle='#ffffff30';ctx.setLineDash([3,4]);ctx.beginPath();ctx.moveTo(x(s.tick),22);ctx.lineTo(x(s.tick),height);ctx.stroke();ctx.setLineDash([]); }
      cursor.forEach((tick,index) => { if(tick===null)return;const px=x(tick);ctx.strokeStyle=index===0?'#f0c88a':'#bda2de';ctx.beginPath();ctx.moveTo(px,0);ctx.lineTo(px,height);ctx.stroke();ctx.fillStyle=ctx.strokeStyle;ctx.fillText(`${index?'B':'A'} ${tick}`,px+4,16); });
    };
    const observer = new ResizeObserver(() => draw.current());observer.observe(wrap);connection.addEventListener('trace',draw.current);
    return () => { observer.disconnect();connection.removeEventListener('trace',draw.current); };
  },[]);
  useEffect(() => draw.current(),[status,span,start,follow,cursors]);
  return <section className="waveform">
    <div className="waveHeader"><div className="sectionTitle"><Activity size={15}/><strong>逻辑分析仪</strong><span className="muted">{status.probes.length} CHANNELS</span></div><div className="waveActions">
      <span className="cursorHint">单击 A · Shift 单击 B</span><button title="放大时间轴" onClick={()=>setSpan(Math.max(4,span/2))}><ZoomIn size={15}/></button><button title="缩小时间轴" onClick={()=>setSpan(Math.min(100000,span*2))}><ZoomOut size={15}/></button><button className={follow?'active':''} title="跟随当前时刻" onClick={()=>setFollow(!follow)}><ArrowRightToLine size={15}/></button>
      <button title="导出 VCD 波形" onClick={async()=>{const data=await command('vcd');if(data)downloadFile('circuit.vcd',String(data.text),'text/plain');}}><Download size={15}/></button><button title="清除历史采样" onClick={()=>command('clearTrace')}><Trash2 size={14}/></button></div></div>
    <div className="waveBody"><div className="channels"><div className="channelHead">SIGNAL <span>0–15</span></div>{status.probes.map((p,i)=><div className="channel" key={p.id}><i style={{background:probeColors[i%probeColors.length]}}/><span title={`${p.name} (${p.pos.join(', ')})`}>{p.name}</span><b>{p.value}</b><select title="探针触发断点" value={p.trigger} onChange={e=>command('configureProbe',{id:p.id,trigger:e.target.value})}><option value="none">—</option><option value="rising">↑</option><option value="falling">↓</option><option value="value">15</option></select><button title="移除探针" onClick={()=>command('removeProbe',{id:p.id})}>×</button></div>)}</div>
      <div className="wavePlot" ref={wrapRef} onWheel={e=>{if(e.shiftKey){setFollow(false);setStart(Math.max(0,start+e.deltaY*span/1000));}}}><canvas ref={canvasRef} onClick={e=>{const rect=e.currentTarget.getBoundingClientRect();const left=follow?Math.max(0,status.tick-span+span*.18):start;const tick=Math.max(0,Math.round(left+(e.clientX-rect.left)/rect.width*span));setCursors(old=>e.shiftKey?[old[0],tick]:[tick,old[1]]);}}/></div>
      {!status.probes.length&&<div className="waveEmpty">选择探针工具，点击器件开始记录信号。</div>}
    </div>
    <div className="waveFooter"><span>游戏刻 gt · 1 gt = 50 ms · 保留同刻边沿顺序</span><span>{cursors[0]!==null&&cursors[1]!==null?`Δ ${Math.abs(cursors[1]-cursors[0])} gt / ${Math.abs(cursors[1]-cursors[0])*50} ms`:'A —     B —     Δ —'}</span>{status.traceDropped>0&&<span className="warning">历史已截断 {status.traceDropped} 条</span>}</div>
  </section>;
}
