// Chrome QA for shared model thumbnails. Controlled states, no kernel simulation.
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { createRequire } from 'node:module';
import { mkdir } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
const { chromium }=createRequire(import.meta.url)('playwright');
const base=process.env.VERIMC_TEST_URL??'http://127.0.0.1:5179';

test('inventory thumbnails show distinct models, cache duplicates and refresh state',async()=>{
  const browser=await chromium.launch({channel:process.env.VERIMC_BROWSER??'chrome',headless:true});
  const page=await browser.newPage({viewport:{width:1000,height:850}});
  const errors=[];page.on('pageerror',error=>{errors.push(error.message);console.error(error.message);});
  try {
    await page.addInitScript(()=>{
      window.contexts=0;const getContext=HTMLCanvasElement.prototype.getContext;
      HTMLCanvasElement.prototype.getContext=function(type,...args){if(type==='webgl2')window.contexts++;return getContext.call(this,type,...args);};
    });
    await page.route('**/thumbnail-harness',route=>route.fulfill({contentType:'text/html; charset=utf-8',body:`<meta charset="utf-8">
      <style>body{background:#202c30;color:#d9dfce;font:13px system-ui;margin:28px}h1{font-size:22px}#gallery{display:grid;grid-template-columns:repeat(6,1fr);gap:12px}.item{background:#303e40;border:1px solid #4e5e59;padding:12px;text-align:center;border-radius:5px}.item .art{display:block;width:80px;height:80px;margin:0 auto 10px}.small{display:block;width:32px;height:32px;margin:10px auto 0}</style>
      <h1>器件缩略图 · 同源模型与材质</h1><p>大图 80px / 下方为 32px 实际列表尺寸</p><div id="root"></div>
      <script type="module">
        import React from '/node_modules/.vite/deps/react.js';
        const h=React.createElement;
        import ReactDOM from '/node_modules/.vite/deps/react-dom_client.js';
        import {BlockThumbnail} from '/src/blockThumbnail.tsx';
        import {blockLabel} from '/src/blockLabels.ts';
        const names=['redstone_wire','repeater','comparator','redstone_torch','lever','stone_button','observer','piston','sticky_piston','redstone_lamp','copper_bulb','target','hopper','chest','dropper','dispenser','note_block','daylight_detector','redstone_block','slime_block','honey_block','stone_pressure_plate','powered_rail','sculk_sensor'];
        const defaults={facing:'south',face:'floor',delay:'1',mode:'compare',locked:'false',powered:'false',lit:'false',power:'0',north:'side',east:'side',south:'side',west:'side',extended:'false',shape:'north_south',type:'normal',sculk_sensor_phase:'inactive'};
        const root=ReactDOM.createRoot(document.querySelector('#root'));
        window.draw=(overrides={})=>root.render(h('div',{id:'gallery'},names.map(name=>h('div',{className:'item',key:name,'data-name':name},h('span',{className:'art'},h(BlockThumbnail,{name:'minecraft:'+name,properties:{...defaults,...(name==='redstone_torch'?{lit:'true'}:{}),...overrides[name]}})),blockLabel('minecraft:'+name),h('span',{className:'small'},h(BlockThumbnail,{name:'minecraft:'+name,properties:{...defaults,...(name==='redstone_torch'?{lit:'true'}:{}),...overrides[name]}}))))));
        draw();
      </script>`}));
    await page.goto(base+'/thumbnail-harness');
    await page.waitForFunction(()=>document.querySelectorAll('img.blockThumbnail').length===48&&[...document.images].every(img=>img.complete&&img.naturalWidth===96));
    const src=name=>page.locator('[data-name="'+name+'"] .art img').getAttribute('src');
    for(const [a,b] of [['repeater','comparator'],['piston','sticky_piston'],['redstone_torch','lever'],['dropper','dispenser']])assert.notEqual(await src(a),await src(b),a+' / '+b+' must remain identifiable');
    assert.equal(await page.evaluate(()=>contexts),1,'one shared context for the batch, including duplicate sizes');
    await mkdir(new URL('../testResults/',import.meta.url),{recursive:true});
    await page.screenshot({path:fileURLToPath(new URL('../testResults/blockThumbnails.png',import.meta.url))});
    const before=await src('repeater');
    await page.evaluate(()=>draw({repeater:{delay:'4',powered:'true',locked:'true'}}));
    await page.waitForFunction(before=>{const img=document.querySelector('[data-name="repeater"] .art img');return img&&img.src!==before&&img.complete;},before);
    assert.notEqual(await src('repeater'),before,'inspector state updates must invalidate the thumbnail');
    assert.deepEqual(errors,[]);
  } finally {await browser.close();}
});
