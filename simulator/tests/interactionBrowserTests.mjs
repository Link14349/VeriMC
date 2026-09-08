// Uses a controlled in-browser kernel fixture; this does not validate C++ behavior.
// Requires a Vite dev server on VERIMC_TEST_URL (default localhost:5179) and Playwright.
import assert from 'node:assert/strict';
import { test, before, after } from 'node:test';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { mkdir } from 'node:fs/promises';
const { chromium }=createRequire(import.meta.url)('playwright');
const base=process.env.VERIMC_TEST_URL ?? 'http://127.0.0.1:5179';
let browser,page;
before(async()=>{
  browser=await chromium.launch({channel:process.env.VERIMC_BROWSER??'chrome',headless:true});
  page=await browser.newPage({viewport:{width:1440,height:1000}});
  await page.route('**/api/bootstrap',route=>route.fulfill({json:{token:'test-fixture',projectFileVersion:1}}));
  await page.routeWebSocket('**/socket?*',()=>{});
  await page.goto(base);
  await page.locator('canvas').first().waitFor();
  await page.evaluate(async()=>{
    const apiUrl=performance.getEntriesByType('resource').find(entry=>new URL(entry.name).pathname==='/src/api.ts').name;
    const {connection}=await import(apiUrl); window.kernel=connection; window.commands=[];
    const defs=[
      {stateId:1,name:'minecraft:repeater',properties:{facing:'west',delay:'1',locked:'false',powered:'false'}},
      {stateId:2,name:'minecraft:redstone_torch',properties:{lit:'true'}},
      {stateId:3,name:'minecraft:lever',properties:{facing:'north',face:'floor',powered:'false'}},
      {stateId:4,name:'minecraft:stone',properties:{}},
      {stateId:5,name:'minecraft:redstone_wall_torch',properties:{facing:'north',lit:'true'}},
    ];
    defs.forEach(def=>connection.states.set(def.stateId,def));
    connection.catalog=defs.map(def=>({name:def.name,defaultState:def.stateId,defaultProperties:def.properties,device:2,supportLevel:def.stateId===3?'externalStimulus':'implemented',properties:Object.fromEntries(Object.entries(def.properties).map(([key,value])=>[key,key==='facing'?['north','east','south','west']:[value]]))}));
    connection.connected=true; connection.status={...connection.status,name:'交互回归电路',blocks:1};
    connection.request=async(cmd,body={})=>{
      commands.push({cmd,body});
      if(cmd==='play')connection.status.running=true;
      if(cmd==='pause')connection.status.running=false;
      connection.dispatchEvent(new Event('status'));
      return {};
    };
    const cell={pos:[4,1,2],stateId:1,renderStateId:1,value:0,motion:0};
    connection.cells.set('4,1,2',cell);
    connection.dispatchEvent(new Event('catalog'));connection.dispatchEvent(new Event('status'));
    connection.dispatchEvent(new CustomEvent('cells',{detail:{full:true,changes:[cell]}}));
  });
});
after(async()=>{await browser?.close();});

test('selection to palette change replaces inspector context and Escape clears it',async()=>{
  const point=await page.evaluate(async()=>{
    const THREE=await import('/node_modules/.vite/deps/three.js');
    const rect=document.querySelector('.viewportCanvas canvas').getBoundingClientRect();
    const camera=new THREE.PerspectiveCamera(42,rect.width/rect.height,.1,3000);
    camera.position.set(18,18,22);camera.lookAt(4,0,2);camera.updateMatrixWorld();
    const p=new THREE.Vector3(4.5,1.1,2.5).project(camera);
    return [rect.left+(p.x+1)*rect.width/2,rect.top+(1-p.y)*rect.height/2];
  });
  await page.keyboard.press('1');
  await page.mouse.click(...point);
  await page.waitForFunction(()=>document.querySelector('.inspector .panelHeading')?.textContent.includes('器件检查'));
  await page.locator('.paletteItem').filter({hasText:'redstone_torch'}).click();
  assert.match(await page.locator('.inspector .panelHeading').innerText(),/放置/);
  assert.match(await page.locator('.inspectorHero').innerText(),/redstone_torch/);
  await page.keyboard.press('Escape');
  assert.doesNotMatch(await page.locator('.inspector .panelHeading').innerText(),/器件检查/);
});

test('search shortcut and help modal isolate editing shortcuts',async()=>{
  await page.keyboard.press('/');
  assert.equal(await page.locator('.search input').evaluate(el=>el===document.activeElement),true);
  await page.keyboard.type('no-such-block');
  assert.equal(await page.locator('.paletteItem').count(),0);
  await page.locator('.search input').fill('');
  await page.getByTitle('使用帮助').click();
  await page.locator('.helpModal h1').evaluate(el=>{el.tabIndex=-1;el.focus();});
  const count=await page.evaluate(()=>commands.length);
  await page.keyboard.press('Enter');await page.keyboard.press('f');await page.keyboard.press('Delete');
  assert.equal(await page.evaluate(()=>commands.length),count);
  await page.keyboard.press('Escape');
  assert.equal(await page.getByTitle('使用帮助').evaluate(el=>el===document.activeElement),true);
});

test('layer inputs remain bounded and disconnected shortcuts cannot send edits',async()=>{
  const layer=page.locator('.layerControl input');
  await layer.fill('319');await layer.press('Tab');
  assert.equal(await page.getByTitle('上升一层').isDisabled(),true);
  assert.equal(await layer.inputValue(),'319');
  await page.evaluate(()=>{kernel.connected=false;kernel.dispatchEvent(new Event('status'));});
  await page.locator('.viewportCanvas canvas').focus();
  const count=await page.evaluate(()=>commands.length);
  await page.keyboard.press('Enter');await page.keyboard.press('f');
  assert.equal(await page.evaluate(()=>commands.length),count);
  await page.evaluate(()=>{kernel.connected=true;kernel.dispatchEvent(new Event('status'));});
  await layer.fill('1');await layer.press('Tab');
});

test('Enter toggles simulation once per press and Space only moves the camera',async()=>{
  await page.locator('.viewportCanvas canvas').focus();
  await page.evaluate(()=>{commands.length=0;kernel.status.running=false;kernel.dispatchEvent(new Event('status'));});
  await page.keyboard.press('Space');
  assert.equal(await page.evaluate(()=>commands.length),0);
  await page.keyboard.down('Enter');await page.keyboard.down('Enter');await page.keyboard.up('Enter');
  assert.deepEqual(await page.evaluate(()=>commands.map(c=>c.cmd)),['play']);
  await page.keyboard.press('Enter');
  assert.deepEqual(await page.evaluate(()=>commands.map(c=>c.cmd)),['play','pause']);
  await page.locator('.search input').focus();await page.keyboard.press('Enter');
  assert.equal(await page.evaluate(()=>commands.length),2);
});

test('capture full workbench for visual review',async()=>{
  await page.locator('.paletteItem').filter({hasText:'repeater'}).click();
  const close=page.getByRole('button',{name:'关闭提示',exact:true});if(await close.count())await close.click();
  await mkdir(new URL('../testResults/',import.meta.url),{recursive:true});
  await page.screenshot({path:fileURLToPath(new URL('../testResults/interactionWorkbench.png',import.meta.url))});
});

test('ordinary torch clicks send wall state and facing on a side, and standing state on top',async()=>{
  await page.evaluate(()=>{
    kernel.cells.clear();commands.length=0;
    const cell={pos:[4,1,2],stateId:4,renderStateId:4,value:0,motion:0};
    kernel.cells.set('4,1,2',cell);
    kernel.dispatchEvent(new CustomEvent('cells',{detail:{full:true,changes:[cell]}}));
  });
  const pointAt=async pos=>page.evaluate(async pos=>{
    const THREE=await import('/node_modules/.vite/deps/three.js');
    const rect=document.querySelector('.viewportCanvas canvas').getBoundingClientRect();
    const camera=new THREE.PerspectiveCamera(42,rect.width/rect.height,.1,3000);
    camera.position.set(18,18,22);camera.lookAt(4,0,2);camera.updateMatrixWorld();
    const p=new THREE.Vector3(...pos).project(camera);
    return [rect.left+(p.x+1)*rect.width/2,rect.top+(1-p.y)*rect.height/2];
  },pos);
  await page.locator('.paletteItem').filter({hasText:'redstone_torch'}).click();
  for(const [point,pos,name,properties] of [
    [[4.99,1.5,2.5],[5,1,2],'minecraft:redstone_wall_torch',{facing:'east'}],
    [[4.5,1.5,2.99],[4,1,3],'minecraft:redstone_wall_torch',{facing:'south'}],
    [[4.5,1.99,2.5],[4,2,2],'minecraft:redstone_torch',{}],
  ]) {
    const count=await page.evaluate(()=>commands.length);
    await page.mouse.click(...await pointAt(point),{button:'right'});
    await page.waitForFunction(count=>commands.length>count,count);
    assert.deepEqual(await page.evaluate(()=>commands.at(-1)),{cmd:'place',body:{pos,name,properties}});
  }
});
