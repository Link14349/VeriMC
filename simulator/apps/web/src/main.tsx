import { useEffect, useReducer, useRef, useState } from 'react';
import { createRoot } from 'react-dom/client';
import { Play, Pause, StepForward, SkipForward, RotateCcw, MousePointer2, Box, Eraser, Crosshair, Hand, Undo2, Redo2, FolderOpen, Save, Plus, Search, Layers, RotateCw, Maximize, Eye, Grid2X2, ChevronDown, Cable, CircleHelp, Copy, ClipboardPaste, Cpu, X, ExternalLink } from 'lucide-react';
import { connection, downloadFile, posKey, type Pos, type CatalogItem, type Status, type FileProgress } from './api';
import { blockLabel, propertyLabels, shortName, valueLabels } from './blockLabels';
import { CircuitViewport } from './viewport';
import { Waveform } from './waveform';
import { EnvironmentControls } from './environmentControls';
import { InventoryControls } from './inventoryControls';
import { ActionControls } from './actionControls';
import {
  baseplateLayer, catalogDefaults, effectiveProperty, filterCatalog, initialInteractionState, interactionReducer,
  isApplePlatform, layerRange, offlineReason, paletteEmptyReason, pickCommand, placementPayload, placementRows, resolveShortcut,
  rotatePlacement, shortcutHint, supportLevelInfo, toolLabel, toolOrder, type ShortcutSource, type Tool,
} from './interactionState';
import './styles.css';

const toolIcons: Record<Tool, typeof Box> = { select: MousePointer2, place: Box, erase: Eraser, probe: Crosshair, interact: Hand };
const blockIcon = (name: string) => { const n=shortName(name);return n==='redstone_wire'?'╋':n==='repeater'?'⇥':n==='comparator'?'▷':n.includes('torch')?'♟':n==='lever'?'╱':n.includes('button')?'▰':n==='observer'?'◉':n.includes('lamp')?'▦':n.includes('bulb')?'▥':n==='redstone_block'?'◆':n==='glass'?'◇':'▣'; };
const apple = isApplePlatform(typeof navigator==='undefined'?'':navigator.platform, typeof navigator==='undefined'?'':navigator.userAgent);
const demoKinds: { kind: string; label: string }[] = [
  { kind: 'basic', label: '打开入门样例' }, { kind: 'pistons', label: '打开活塞实验' }, { kind: 'environment', label: '打开环境实验' },
  { kind: 'inventory', label: '打开漏斗实验' }, { kind: 'notes', label: '打开音符实验' }, { kind: 'vibrations', label: '打开振动实验' },
  { kind: 'droppers', label: '打开投掷器实验' }, { kind: 'rails', label: '打开铁轨实验' }, { kind: 'tripwire', label: '打开绊线实验' },
];
function SupportTag({ level }: { level?: string }) {
  const info = supportLevelInfo(level);
  return <em className={`supportTag level-${info.level}`} title={info.detail}>{info.short}</em>;
}
function App() {
  const [state,dispatch]=useReducer(interactionReducer,initialInteractionState);
  const [status,setStatus]=useState<Status>(connection.status),[connected,setConnected]=useState(false),[catalog,setCatalog]=useState(connection.catalog),[hover,setHover]=useState<Pos|null>(null),[fps,setFps]=useState(0);
  const [fileProgress,setFileProgress]=useState<FileProgress|null>(null);
  const [sectionAxis,setSectionAxis]=useState<'none'|'x'|'y'|'z'>('none');
  const [sectionMaximum,setSectionMaximum]=useState(0),[sectionText,setSectionText]=useState('0');
  const fileController=useRef<AbortController|null>(null);
  const viewportRef=useRef<HTMLDivElement>(null), view=useRef<CircuitViewport|null>(null), importRef=useRef<HTMLInputElement>(null);
  const searchInput=useRef<HTMLInputElement>(null), menuAnchor=useRef<HTMLDivElement>(null), menuButton=useRef<HTMLButtonElement>(null);
  const helpDialog=useRef<HTMLDialogElement>(null), helpReturn=useRef<HTMLElement|null>(null), helpButton=useRef<HTMLButtonElement>(null);
  const stateRef=useRef(state);stateRef.current=state;
  const statusRef=useRef(status);statusRef.current=status;
  const connectedRef=useRef(connected);connectedRef.current=connected;
  const notify=(text:string)=>dispatch({type:'notify',kind:'info',text});
  const fail=(text:string)=>dispatch({type:'notify',kind:'error',text});
  const command=async(cmd:string,body:Record<string,unknown>={})=>{try{return await connection.request(cmd,body);}catch(error){fail(error instanceof Error?error.message:String(error));return undefined;}};
  const callbacks=useRef({command});callbacks.current={command};
  useEffect(()=>{
    const onStatus=()=>{setStatus({...connection.status});setConnected(connection.connected);};
    const onCatalog=()=>setCatalog([...connection.catalog]);
    const onError=(event:Event)=>dispatch({type:'notify',kind:'error',text:(event as CustomEvent<string>).detail});
    const refreshSelection=()=>{const p=stateRef.current.selection;if(!p)return;const cell=connection.cells.get(posKey(p));dispatch({type:'refreshSelection',def:cell?connection.states.get(cell.stateId)??null:null});};
    connection.addEventListener('status',onStatus);connection.addEventListener('catalog',onCatalog);connection.addEventListener('error',onError);connection.addEventListener('cells',refreshSelection);connection.connect();
    const viewport=new CircuitViewport(viewportRef.current!);view.current=viewport;viewport.onHover=setHover;viewport.onFps=setFps;
    viewport.onPick=(pos,active)=>{
      const {placement}=stateRef.current;const run=callbacks.current.command;
      if(active==='select'){const cell=connection.cells.get(posKey(pos));dispatch({type:'select',pos,def:cell?connection.states.get(cell.stateId)??null:null});return;}
      // 断线时不发出无法执行的编辑命令，直接给出可见提示。
      const routing=pickCommand(active,connection.connected);
      if(!routing)return;
      if('blocked' in routing){dispatch({type:'notify',kind:'error',text:routing.blocked});return;}
      if(routing.command==='place'){const target=connection.catalog.find(c=>c.name===placement.name);void run('place',{pos,name:placement.name,properties:placementPayload(target,placement.properties)});return;}
      if(routing.command==='probe'){void run('probe',{pos,name:blockLabel(connection.states.get(connection.cells.get(posKey(pos))?.stateId??0)?.name??'信号')});return;}
      void run(routing.command,{pos});
    };
    return()=>{viewport.dispose();connection.stop();connection.removeEventListener('status',onStatus);connection.removeEventListener('catalog',onCatalog);connection.removeEventListener('error',onError);connection.removeEventListener('cells',refreshSelection);};
  },[]);
  useEffect(()=>{if(view.current)view.current.tool=state.tool;},[state.tool]);
  useEffect(()=>{view.current?.select(state.selection);},[state.selection]);
  useEffect(()=>{view.current?.setLayer(state.layer,state.cutaway);},[state.layer,state.cutaway]);
  useEffect(()=>{view.current?.setSection(sectionAxis,sectionMaximum);},[sectionAxis,sectionMaximum]);
  // 错误必须由用户显式关闭或被新消息替换；普通提示到时自动消失。
  useEffect(()=>{const message=state.message;if(!message||message.kind==='error')return;const timer=setTimeout(()=>dispatch({type:'dismissMessage',id:message.id}),5000);return()=>clearTimeout(timer);},[state.message]);
  // 原生 dialog 负责焦点限制与 Esc；关闭后把焦点还给触发按钮。
  useEffect(()=>{
    const dialog=helpDialog.current;if(!dialog)return;
    if(state.help&&!dialog.open){helpReturn.current=document.activeElement instanceof HTMLElement?document.activeElement:null;dialog.showModal();}
    else if(!state.help&&dialog.open)dialog.close();
  },[state.help]);
  useEffect(()=>{
    if(!state.menu)return;
    const away=(event:Event)=>{if(!menuAnchor.current?.contains(event.target as Node))dispatch({type:'setMenu',value:false});};
    window.addEventListener('pointerdown',away);return()=>window.removeEventListener('pointerdown',away);
  },[state.menu]);

  const item=catalog.find(c=>c.name===state.placement.name);
  const chosenItem=catalog.find(c=>c.name===state.selectedDef?.name);
  // 放置默认值只认 catalog.defaultState 对应的方块状态，界面没有该状态时如实显示“默认”。
  const defaults=catalogDefaults(item,connection.states);
  useEffect(()=>{
    view.current?.setPlacement(state.placement.name, defaults ? {...defaults,...placementPayload(item,state.placement.properties)} : null);
  },[state.placement.name,state.placement.properties,item,defaults]);
  const rotate=()=>{
    const target=connection.catalog.find(c=>c.name===stateRef.current.placement.name);
    const outcome=rotatePlacement(target,stateRef.current.placement.properties,catalogDefaults(target,connection.states));
    if(!outcome.changed){fail(outcome.reason);return;}
    dispatch({type:'setPlacementProperties',properties:outcome.properties});
  };
  const rotateRef=useRef(rotate);rotateRef.current=rotate;
  const transfer=async(file?:File,checkpoint=false)=>{
    if(fileController.current)return;
    if(!connection.connected){fail(offlineReason);return;}
    const controller=new AbortController();fileController.current=controller;
    setFileProgress({id:'',operation:file?'import':'export',state:'working',stage:'准备文件',done:'0',total:'0',error:''});
    try{
      await connection.projectFile({file,checkpoint,signal:controller.signal,progress:setFileProgress});
      if(file){dispatch({type:'clearSelection'});setTimeout(()=>view.current?.fit(),200);}
      notify(file?'工程已导入':checkpoint?'运行快照已生成，已交给浏览器下载':'电路文件已生成，已交给浏览器下载');
    }catch(error){fail(error instanceof Error?error.message:String(error));}
    finally{fileController.current=null;setFileProgress(null);}
  };
  const save=(checkpoint:boolean)=>transfer(undefined,checkpoint);
  const saveRef=useRef(save);saveRef.current=save;
  useEffect(()=>{
    const sourceOf=(target:EventTarget|null):ShortcutSource=>{
      if(target===searchInput.current)return 'search';
      if(target instanceof HTMLInputElement||target instanceof HTMLSelectElement||target instanceof HTMLTextAreaElement)return 'text';
      if(target instanceof HTMLElement&&target.isContentEditable)return 'text';
      return 'other';
    };
    const handle=(event:KeyboardEvent)=>{
      const live=stateRef.current, source=sourceOf(event.target);
      const decision=resolveShortcut(
        {key:event.key,code:event.code,ctrlKey:event.ctrlKey,metaKey:event.metaKey,shiftKey:event.shiftKey,altKey:event.altKey,source},
        {help:live.help,menu:live.menu,fileBusy:!!fileController.current,connected:connectedRef.current,running:statusRef.current.running,tool:live.tool,
         hasSelection:!!live.selection,hasSelectedDef:!!live.selectedDef,hasClipboard:!!live.clipboard,canUndo:statusRef.current.canUndo,canRedo:statusRef.current.canRedo});
      if(!decision)return;
      if(decision.preventDefault)event.preventDefault();
      const result=decision.result;
      switch(result.kind){
        case 'abortFile': fileController.current?.abort();return;
        case 'escape': {
          const restore=live.menu&&!live.help;
          if(source==='text'&&event.target instanceof HTMLElement)event.target.blur();
          dispatch({type:'escape'});
          if(restore)menuButton.current?.focus();
          return;
        }
        case 'clearSearch': dispatch({type:'setSearch',value:''});searchInput.current?.blur();return;
        case 'focusSearch': searchInput.current?.focus();searchInput.current?.select();return;
        case 'setTool': dispatch({type:'setTool',tool:result.tool});return;
        case 'rotate': rotateRef.current();return;
        case 'copy': if(live.selectedDef){dispatch({type:'copy',def:live.selectedDef});dispatch({type:'notify',kind:'info',text:'已复制器件；粘贴后进入放置模式'});}return;
        case 'paste': dispatch({type:'paste'});return;
        case 'save': void saveRef.current(false);return;
        case 'removeSelected': if(live.selection)void callbacks.current.command('remove',{pos:live.selection});return;
        case 'unavailable': dispatch({type:'notify',kind:'error',text:result.reason});return;
        default: void callbacks.current.command(result.command);
      }
    };
    window.addEventListener('keydown',handle);return()=>window.removeEventListener('keydown',handle);
  },[]);

  const closeMenu=()=>dispatch({type:'setMenu',value:false});
  const runDemo=(kind:string)=>{closeMenu();void command('demo',kind==='basic'?{}:{kind});setTimeout(()=>view.current?.fit(),200);};
  const filtered=filterCatalog(catalog,state.search,state.category);
  const emptyReason=paletteEmptyReason({connected,catalogSize:catalog.length,search:state.search,category:state.category,matches:filtered.length});
  const heroName=state.selectedDef?.name??state.placement.name;
  const heroItem:CatalogItem|undefined=state.selectedDef?chosenItem:item;
  const baseLayer=baseplateLayer(state.layer);
  const rows=placementRows(item);
  return <main className="app">
    <header className="appHeader"><div className="brand"><div className="brandMark"><Cable size={21}/></div><strong>simulator<span> / </span><small>VeriMC</small></strong><span className="alphaBadge">PREVIEW</span></div><div className="projectTitle"><span className="projectDot"/>{status.name}<ChevronDown size={13}/></div><div className="headerRight"><span className="version">JAVA 26.2</span><button ref={helpButton} className="iconButton" title="使用帮助" onClick={()=>dispatch({type:'setHelp',value:true})}><CircleHelp size={17}/></button></div></header>
    <nav className="toolbar"><div className="fileTools"><div className="menuAnchor" ref={menuAnchor}><button ref={menuButton} className="textButton" aria-haspopup="menu" aria-expanded={state.menu} onClick={()=>dispatch({type:'setMenu',value:!state.menu})}><FolderOpen size={16}/>工程<ChevronDown size={12}/></button>{state.menu&&<div className="dropdown" role="menu"><button role="menuitem" disabled={!connected} onClick={()=>{closeMenu();dispatch({type:'clearSelection'});void command('new');}}>新建空白电路</button>{demoKinds.map(entry=><button role="menuitem" key={entry.kind} disabled={!connected} onClick={()=>runDemo(entry.kind)}>{entry.label}</button>)}<button role="menuitem" disabled={!connected} onClick={()=>{closeMenu();importRef.current?.click();}}>导入 .vmcb / JSON…</button><button role="menuitem" disabled={!connected} onClick={()=>{closeMenu();void save(false);}}>导出电路 .vmcb</button><button role="menuitem" disabled={!connected} onClick={()=>{closeMenu();void save(true);}}>导出快照 .vmcb</button><button role="menuitem" disabled={!connected} onClick={async()=>{closeMenu();const data=await command('actionLog');if(data)downloadFile('environmentActions.json',JSON.stringify({actions:data.actions,dropped:data.dropped},null,2));}}>导出外部动作记录</button></div>}</div><button title={`保存工程 ${shortcutHint(apple,'S')}`} disabled={!connected} onClick={()=>void save(false)}><Save size={16}/></button><i className="divider"/><button title={`撤销 ${shortcutHint(apple,'Z')}`} disabled={!connected||!status.canUndo} onClick={()=>void command('undo')}><Undo2 size={16}/></button><button title={`重做 ${shortcutHint(apple,'Z',true)}`} disabled={!connected||!status.canRedo} onClick={()=>void command('redo')}><Redo2 size={16}/></button></div>
      <div className="transport"><button className={`runButton ${status.running?'running':''}`} onClick={()=>void command(status.running?'pause':'play')} disabled={!connected}>{status.running?<Pause size={15} fill="currentColor"/>:<Play size={15} fill="currentColor"/>}{status.running?'暂停':'运行'}<kbd>Space</kbd></button><button title="前进 1 游戏刻 F" disabled={!connected} onClick={()=>void command('step')}><StepForward size={17}/></button><button title="执行下一个计划事件" disabled={!connected} onClick={()=>void command('stepEvent')}><SkipForward size={17}/></button><button title="恢复本轮运行起点" disabled={!connected} onClick={()=>void command('reset')}><RotateCcw size={16}/></button><i className="divider"/><select aria-label="仿真速度" disabled={!connected} value={status.speed} onChange={e=>void command('speed',{value:Number(e.target.value)})}><option value={20}>1× · 20 gt/s</option><option value={200}>10× · 200 gt/s</option><option value={2000}>100× · 2k gt/s</option><option value={0}>不限速</option></select></div>
      <div className="tickReadout"><span>SIM TIME</span><b>{status.tick.toLocaleString()}</b><span>gt</span></div></nav>
    <div className="workspace">
      <aside className="palette"><div className="panelHeading"><span>器件库</span><span className="muted">{catalog.length}</span></div><label className="search"><Search size={14}/><input ref={searchInput} placeholder="搜索器件或方块 ID" aria-label="搜索器件或方块 ID" value={state.search} onChange={e=>dispatch({type:'setSearch',value:e.target.value})}/><kbd>/</kbd></label><div className="tabs">{['常用','器件','结构'].map(c=><button className={state.category===c?'active':''} onClick={()=>dispatch({type:'setCategory',value:c})} key={c}>{c}</button>)}</div>
        <div className="paletteList">{filtered.map(c=><button key={c.name} className={`paletteItem ${state.placement.name===c.name?'selected':''}`} onClick={()=>dispatch({type:'chooseBlock',name:c.name})} title={`${c.name} · ${supportLevelInfo(c.supportLevel).detail}`}><span className={`blockGlyph ${shortName(c.name).includes('redstone')?'red':''}`}>{blockIcon(c.name)}</span><span>{blockLabel(c.name)}<small>{shortName(c.name)}</small><SupportTag level={c.supportLevel}/></span>{state.placement.name===c.name&&<i/>}</button>)}
          {emptyReason&&<div className="paletteEmpty" role="status"><p>{emptyReason}</p>{state.search.trim()&&<button className="wideButton" onClick={()=>{dispatch({type:'setSearch',value:''});searchInput.current?.focus();}}>清除搜索</button>}</div>}</div>
        <div className="paletteNote"><Cpu size={16}/><span>原生 C++ 仿真<br/><small>标注区分已实现 / 部分实现 / 需环境输入，均未声称完整差分验证</small></span></div></aside>
      <section className="centerWorkspace"><div className="viewportArea"><div className="viewportCanvas" ref={viewportRef}/><div className="viewportTop"><span className="sceneBadge"><span className={status.running?'liveDot':'pausedDot'}/>{status.running?'SIMULATING':'EDIT MODE'}<i/><span className="toolReadout">当前工具 · {toolLabel(state.tool)}{state.tool==='place'?` · ${blockLabel(state.placement.name)}`:''}</span></span><div className="viewButtons"><button title="俯视" onClick={()=>view.current?.top()}><Grid2X2 size={15}/></button><button title="适合画面" onClick={()=>view.current?.fit()}><Maximize size={15}/></button></div></div>
        <div className="toolRail">{toolOrder.map(t=>{const Icon=toolIcons[t.id];return <button key={t.id} className={state.tool===t.id?'active':''} aria-pressed={state.tool===t.id} title={`${t.label} [${t.key}]`} onClick={()=>dispatch({type:'setTool',tool:t.id})}><Icon size={18}/><kbd>{t.key}</kbd></button>;})}</div>
        <div className="layerControl"><Layers size={15}/><span>编辑层 Y</span><button title="下降一层" disabled={state.layer<=layerRange.min} onClick={()=>dispatch({type:'setLayer',value:state.layer-1})}>−</button><input aria-label={`编辑层 Y，范围 ${layerRange.min} 到 ${layerRange.max}`} inputMode="numeric" value={state.layerText} onChange={e=>dispatch({type:'typeLayer',value:e.target.value})} onBlur={()=>dispatch({type:'commitLayerText'})}/><button title="上升一层" disabled={state.layer>=layerRange.max} onClick={()=>dispatch({type:'setLayer',value:state.layer+1})}>+</button><i/><button title="隐藏编辑层以上方块" aria-pressed={state.cutaway} className={state.cutaway?'active':''} onClick={()=>dispatch({type:'setCutaway',value:!state.cutaway})}><Eye size={15}/></button></div>
        <div className="sectionControl"><label>剖切 <select aria-label="剖切轴" value={sectionAxis} onChange={e=>setSectionAxis(e.target.value as typeof sectionAxis)}><option value="none">关闭</option><option value="x">X</option><option value="y">Y</option><option value="z">Z</option></select></label>{sectionAxis!=='none'&&<label>显示 ≤ <input aria-label="剖切坐标" type="number" step={1} value={sectionText} onChange={e=>{setSectionText(e.target.value);const n=Number(e.target.value);if(e.target.value.trim()&&Number.isFinite(n))setSectionMaximum(Math.trunc(n));}} onBlur={()=>setSectionText(String(sectionMaximum))}/></label>}<span>仅影响显示</span></div>
        <div className="viewportBottom"><span>右键拖动旋转 · 中键 / WASD 平移 · 滚轮缩放</span><span className="coord">{hover?`X ${hover[0]}   Y ${hover[1]}   Z ${hover[2]}`:'X —   Y —   Z —'}</span></div>
        {status.pauseReason&&<div className="pauseBanner" role="alert">{status.pauseReason}</div>}
      </div><fieldset className="offlineGuard column" disabled={!connected}><Waveform status={status} command={command}/></fieldset></section>
      <aside className="inspector"><div className="panelHeading"><span>{state.selectedDef?'器件检查':'放置设置'}</span>{state.selection&&<button title="取消选择" onClick={()=>dispatch({type:'clearSelection'})}><X size={14}/></button>}</div>
        <fieldset className="offlineGuard" disabled={!connected}><ActionControls status={status} command={command}/></fieldset><div className="inspectorHero"><div className="largeGlyph">{blockIcon(heroName)}</div><strong>{blockLabel(heroName)}</strong><code>{shortName(heroName)}</code><SupportTag level={heroItem?.supportLevel}/><small className="supportDetail">{supportLevelInfo(heroItem?.supportLevel).detail}</small></div>
        {state.selection&&state.selectedDef?<><div className="inspectorSection"><label className="miniLabel">坐标</label><div className="positionFields">{state.selection.map((v,i)=><span key={i}><small>{['X','Y','Z'][i]}</small>{v}</span>)}</div><div className="signalMeter"><span>当前信号</span><strong>{connection.cells.get(posKey(state.selection))?.value??0}<small>/ 15</small></strong></div><div className="meter"><i style={{width:`${(connection.cells.get(posKey(state.selection))?.value??0)/15*100}%`}}/></div></div>
          <div className="inspectorSection"><label className="miniLabel">方块状态</label>{Object.entries(state.selectedDef.properties).map(([key,value])=><label className="propertyRow" key={key}><span>{propertyLabels[key]??key}</span><select value={value} disabled={!connected} onChange={e=>void command('place',{pos:stateRef.current.selection,name:stateRef.current.selectedDef!.name,properties:{...stateRef.current.selectedDef!.properties,[key]:e.target.value}})}>{(chosenItem?.properties[key]??[value]).map(v=><option key={v} value={v}>{valueLabels[v]??v}</option>)}</select></label>)}</div>
          {chosenItem?.supportLevel==='externalStimulus'&&<p className="subtleText" style={{padding:'0 15px'}}>此器件的环境状态由输入面板或方块属性提供。</p>}
          <fieldset className="offlineGuard" disabled={!connected}>
            <EnvironmentControls key={"environment:"+posKey(state.selection)+state.selectedDef.name} pos={state.selection} block={state.selectedDef} command={command}/>
            {(chosenItem?.device===17||chosenItem?.device===16||chosenItem?.device===28||chosenItem?.device===18||state.selectedDef.name==='minecraft:chiseled_bookshelf'||state.selectedDef.name==='minecraft:decorated_pot'||state.selectedDef.name==='minecraft:jukebox')&&<InventoryControls key={"inventory:"+posKey(state.selection)+state.selectedDef.name} pos={state.selection} hopper={chosenItem?.device===16} cart={chosenItem?.device===28} dropper={chosenItem?.device===18} bookshelf={state.selectedDef.name==='minecraft:chiseled_bookshelf'} pot={state.selectedDef.name==='minecraft:decorated_pot'} jukebox={state.selectedDef.name==='minecraft:jukebox'} command={command}/>}
          </fieldset>
          <div className="inspectorSection"><button className="wideButton accentOutline" disabled={!connected} onClick={()=>void command('probe',{pos:stateRef.current.selection,name:blockLabel(stateRef.current.selectedDef!.name)})}><Crosshair size={15}/>添加信号探针</button>{!['minecraft:jukebox','minecraft:composter'].includes(state.selectedDef.name)&&<button className="wideButton" disabled={!connected} onClick={()=>void command('interact',{pos:stateRef.current.selection})}><Hand size={15}/>操作器件</button>}<button className="wideButton" onClick={()=>{const def=stateRef.current.selectedDef;if(def){dispatch({type:'copy',def});notify('器件已复制，点击粘贴进入放置模式');}}}><Copy size={14}/>复制器件</button></div>
        </>:<><div className="inspectorSection"><label className="miniLabel">放置属性</label><p className="subtleText">{defaults?'预览箭头：中继器 / 比较器为输出，侦测器为检测面，其他器件为朝向。':'内核尚未提供默认状态，暂不显示器件模型预览；仍可按默认属性放置。'}</p>
          {!item&&<p className="subtleText">{catalog.length===0?(connected?'内核尚未发送器件库，暂时无法显示放置属性。':'尚未连接本地内核，暂时无法显示放置属性。'):`器件库中没有 ${shortName(state.placement.name)}，无法显示放置属性。`}</p>}
          {item&&rows.length===0&&<p className="subtleText">该方块没有可调整的放置属性。</p>}
          {item&&rows.map(key=>{const values=item.properties[key];const value=effectiveProperty(state.placement.properties,defaults,key);
            return <label key={key} className="propertyRow"><span>{propertyLabels[key]??key}</span><select value={value} onChange={e=>dispatch({type:'setPlacementProperty',key,value:e.target.value})}>{!defaults&&<option value="">默认（由内核决定）</option>}{values.map(v=><option value={v} key={v}>{valueLabels[v]??v}</option>)}</select></label>;})}
          <button className="wideButton" disabled={!item} onClick={rotate}><RotateCw size={15}/>旋转方向<kbd>R</kbd></button>
          <p className="subtleText">{defaults?'未修改的项目采用器件默认值。':'尚未收到器件默认属性，未修改的项目会在放置时采用默认值。'}</p>
          <p className="subtleText">R 只在放置工具下生效。中继器、比较器的“朝向”指向输入端；侦测器的朝向指向检测面。</p></div>
          <div className="inspectorSection"><label className="miniLabel">搭建起点</label><button className="wideButton" disabled={!connected||baseLayer===null} onClick={()=>{if(baseLayer===null)return;const blocks=[];for(let x=-2;x<=13;x++)for(let z=-2;z<=9;z++)blocks.push({pos:[x,baseLayer,z],name:'minecraft:white_concrete'});void command('edit',{blocks});}}><Plus size={15}/>铺设 16 × 12 底板</button><p className="subtleText">{baseLayer===null?`编辑层已在 Y ${layerRange.min}，下方超出世界范围，无法铺设底板。`:`底板铺在 Y ${baseLayer}。粉线、火把和中继器需要下方支撑。`}</p>{state.clipboard&&<button className="wideButton" onClick={()=>dispatch({type:'paste'})}><ClipboardPaste size={15}/>粘贴 {blockLabel(state.clipboard.name)}</button>}</div></>}
        <div className="inspectorFooter"><span className="miniLabel">运行状态</span><div><span>计划事件</span><code>{status.pending.toLocaleString()}</code></div><div><span>分块存储</span><code>{(status.storageBytes/1048576).toFixed(2)} MB</code></div><div><span>已处理事件</span><code>{status.events.toLocaleString()}</code></div></div>
      </aside>
    </div>
    <footer className="statusBar"><span><i className={connected?'connectionDot':'disconnectedDot'}/>{connected?'本地内核已连接':'正在连接本地内核…'}</span><span>{status.blocks.toLocaleString()} 方块<i/> {fps} FPS<i/> {Math.round(status.eventsPerSecond).toLocaleString()} 事件/s</span><span>Java 26.2 · 非实验性红石<i/>工程文件与运行快照</span></footer>
    <input ref={importRef} type="file" accept=".vmcb,.json" hidden onChange={e=>{const file=e.target.files?.[0];if(file)void transfer(file);e.target.value='';}}/>
    {fileProgress&&<div className="fileOverlay"><section className="fileDialog" role="dialog" aria-modal="true" aria-label="工程文件读写"><strong>{fileProgress.operation==='import'?'导入工程':'导出工程'}</strong><p aria-live="polite">{fileProgress.stage}</p>{BigInt(fileProgress.total)>0n&&<progress max={1000} value={Number(BigInt(fileProgress.done)*1000n/BigInt(fileProgress.total))}/>}<button onClick={()=>fileController.current?.abort()} disabled={fileController.current?.signal.aborted}>取消</button><small>导入校验失败或取消时，保留当前工程。</small></section></div>}
    {state.message&&<div className={`toast ${state.message.kind}`} role={state.message.kind==='error'?'alert':'status'} aria-live={state.message.kind==='error'?'assertive':'polite'}><span>{state.message.text}</span><button title="关闭提示" aria-label="关闭提示" onClick={()=>dispatch({type:'dismissMessage'})}><X size={15}/></button></div>}
    <dialog className="helpModal" ref={helpDialog} aria-label="使用帮助"
      onClose={()=>{dispatch({type:'setHelp',value:false});const back=helpReturn.current??helpButton.current;helpReturn.current=null;back?.focus();}}
      onClick={event=>{if(event.target===helpDialog.current)dispatch({type:'setHelp',value:false});}}>
      <button className="modalClose" title="关闭帮助" aria-label="关闭帮助" onClick={()=>dispatch({type:'setHelp',value:false})}><X size={18}/></button><span className="eyebrow">YOUR REDSTONE WORKBENCH</span><h1>从一条信号开始。</h1><p>浏览器负责搭建与显示，本地 C++ 内核执行电路。示波器保留游戏刻内的信号变化。</p>
      <div className="helpSteps"><div><b>01</b><strong>搭建电路</strong><p>器件库选择方块，在当前 Y 层左键放置。用右侧按钮快速铺底板，放置工具下按 R 调整朝向。</p></div><div><b>02</b><strong>驱动与运行</strong><p>选择操作工具点击拉杆或按钮，Space 运行，F 前进 1 gt。器件编辑会暂停运行。</p></div><div><b>03</b><strong>检查信号</strong><p>探针工具点击方块，下方查看 0–15 信号。探针菜单可设置上升沿或下降沿断点。</p></div></div>
      <div className="helpKeys"><span className="miniLabel">快捷键</span><div><kbd>1</kbd>–<kbd>5</kbd> 切换工具</div><div><kbd>Space</kbd> 运行 / 暂停</div><div><kbd>F</kbd> 前进 1 gt</div><div><kbd>R</kbd> 旋转待放置方块</div><div><kbd>/</kbd> 聚焦器件搜索</div><div><kbd>{shortcutHint(apple,'S')}</kbd> 导出电路</div><div><kbd>{shortcutHint(apple,'Z')}</kbd> 撤销</div><div><kbd>{shortcutHint(apple,'Z',true)}</kbd> 重做</div><div><kbd>Esc</kbd> 逐层关闭弹窗并清除选择</div></div>
      <p className="subtleText">预览版本只开放内核已登记的器件，并按“已实现 / 部分实现 / 需环境输入”标注；这些标注不代表已通过完整的 Minecraft 26.2 差分验证。电路文件保存方块设计，运行快照还包含队列和器件内部状态。</p>
      <button className="runButton" onClick={()=>dispatch({type:'setHelp',value:false})}>开始搭建<ExternalLink size={14}/></button>
    </dialog>
  </main>;
}
createRoot(document.getElementById('root')!).render(<App/>);
