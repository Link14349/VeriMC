// Controlled browser fixtures validate input dispatch, not C++ or Minecraft mechanics.
// Start simulator/apps/web with `npm run dev -- --port 5179 --strictPort`.
// Expose Playwright through NODE_PATH when it is installed outside this project.
import assert from 'node:assert/strict';
import { test, before, after } from 'node:test';
import { createRequire } from 'node:module';
import { mkdir } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';

const { chromium } = createRequire(import.meta.url)('playwright');
const base = process.env.VERIMC_TEST_URL ?? 'http://127.0.0.1:5179';
let browser;
before(async () => {
  browser = await chromium.launch({ channel: process.env.VERIMC_BROWSER ?? 'chrome', headless: true });
});
after(async () => { await browser?.close(); });

test('first-person viewport dispatches trusted browser input only while captured', async t => {
  const page = await browser.newPage({ viewport: { width: 1100, height: 800 } });
  await page.route('**/creative-harness', route => route.fulfill({ contentType: 'text/html', body: `
    <style>body{margin:0}#scene{width:1100px;height:800px}#capture{position:absolute;left:8px;top:8px}</style>
    <div id="scene"></div><button id="capture">Capture pointer</button>
    <script type="module">
      import { CircuitViewport } from '/src/viewport.ts';
      const apiUrl=performance.getEntriesByType('resource').find(entry=>new URL(entry.name).pathname==='/src/api.ts').name;
      const { connection }=await import(apiUrl);
      window.connection=connection;
      window.view=new CircuitViewport(document.querySelector('#scene'));
      window.calls=[]; window.locks=[]; window.consumeUse=false;
      view.onPick=(pos,tool,shift,face)=>calls.push({kind:'pick',pos,tool,shift,face});
      view.onUse=(pos,shift)=>{calls.push({kind:'use',pos,shift});return consumeUse&&!shift;};
      view.onPickBlock=pos=>calls.push({kind:'pickBlock',pos});
      view.onHotbarScroll=delta=>calls.push({kind:'scroll',delta});
      view.onPointerLockChange=locked=>locks.push(locked);
      document.querySelector('#capture').onclick=()=>view.requestPointerLock();
      connection.states.set(1,{stateId:1,name:'minecraft:stone',properties:{}});
      view.setPlacement('minecraft:stone',{});
      view.setFirstPerson(true);
      window.setScene=(positions,camera=[.5,1.5,4.5],target=[.5,1.5,.5])=>{
        connection.cells.clear();
        for(const pos of positions)connection.cells.set(pos.join(','),{pos,stateId:1,renderStateId:1,value:0,motion:0});
        connection.dispatchEvent(new CustomEvent('cells',{detail:{full:true,changes:[]}}));
        view.camera.position.fromArray(camera);view.camera.lookAt(...target);view.camera.updateMatrixWorld();
        calls.length=0;
      };
      setScene([[0,1,0]]);
    </script>` }));
  await page.goto(`${base}/creative-harness`);
  await page.waitForFunction(() => !!window.view);
  const capture = async () => {
    if (await page.evaluate(() => view.pointerLocked)) return;
    await page.locator('#capture').click();
    await page.waitForFunction(() => view.pointerLocked && document.pointerLockElement === view.renderer.domElement);
  };
  const release = async () => {
    await page.evaluate(() => view.releasePointerLock());
    await page.waitForFunction(() => !view.pointerLocked && document.pointerLockElement === null && locks.at(-1) === false);
  };
  const click = async (button = 'left') => {
    await page.mouse.down({ button });
    await page.mouse.up({ button });
  };
  const scene = async (positions, camera, target) => {
    await page.evaluate(({ positions, camera, target }) => setScene(positions, camera, target), { positions, camera, target });
    await page.evaluate(() => new Promise(resolve => requestAnimationFrame(resolve)));
  };
  try {
    await t.test('entering the mode alone neither moves nor edits; capture click is consumed', async () => {
      assert.equal(await page.evaluate(() => view.firstPerson), true);
      await page.locator('canvas').focus();
      const before = await page.evaluate(() => view.camera.position.toArray());
      await page.keyboard.down('w');
      await page.waitForTimeout(150);
      await page.keyboard.up('w');
      assert.deepEqual(await page.evaluate(() => view.camera.position.toArray()), before);
      // The first canvas click may capture the pointer, but must never break a block.
      await page.mouse.click(550, 400);
      assert.deepEqual(await page.evaluate(() => calls), []);
      await capture();
      assert.ok(await page.evaluate(() => locks.includes(true)));
    });

    await t.test('center crosshair breaks once on press and uses before placing', async () => {
      await capture();
      await scene([[0,1,0]]);
      await page.mouse.down();
      assert.deepEqual(await page.evaluate(() => calls.map(({kind,pos,tool}) => ({kind,pos,tool}))), [
        {kind:'pick',pos:[0,1,0],tool:'erase'},
      ]);
      await page.mouse.up();
      assert.equal(await page.evaluate(() => calls.length), 1);
      await page.evaluate(() => { calls.length=0;consumeUse=true; });
      await click('right');
      assert.deepEqual(await page.evaluate(() => calls), [{kind:'use',pos:[0,1,0],shift:false}]);
      await page.evaluate(() => { calls.length=0;consumeUse=false; });
      await click('right');
      assert.deepEqual(await page.evaluate(() => calls), [
        {kind:'use',pos:[0,1,0],shift:false},
        {kind:'pick',pos:[0,1,1],tool:'place',shift:false,face:'south'},
      ]);
      await page.evaluate(() => { calls.length=0;consumeUse=true; });
      await page.keyboard.down('Shift');
      await click('right');
      await page.keyboard.up('Shift');
      const shifted = await page.evaluate(() => calls);
      assert.equal(shifted.at(-1)?.tool, 'place');
      assert.equal(shifted.at(-1)?.shift, true);
      assert.ok(shifted.every(call => call.shift === true));
    });

    await t.test('middle click picks the aimed block and wheel changes the hotbar', async () => {
      await scene([[0,1,0]]);
      await click('middle');
      assert.deepEqual(await page.evaluate(() => calls), [{kind:'pickBlock',pos:[0,1,0]}]);
      const before = await page.evaluate(() => view.camera.position.toArray());
      await page.mouse.wheel(0,120);
      await page.waitForFunction(() => calls.some(call => call.kind === 'scroll'));
      await page.mouse.wheel(0,-120);
      await page.waitForFunction(() => calls.filter(call => call.kind === 'scroll').length === 2);
      assert.deepEqual(await page.evaluate(() => calls.filter(call => call.kind === 'scroll').map(call => Math.sign(call.delta))), [1,-1]);
      assert.deepEqual(await page.evaluate(() => view.camera.position.toArray()), before);
    });

    await t.test('all block interactions reach twenty blocks; no construction-plane fallback', async () => {
      // The aimed south faces are approximately 19.5 and 20.5 blocks from the eye.
      await scene([[0,1,-16]]);
      await page.evaluate(() => { consumeUse=false; });
      for (const button of ['left','right','middle']) await click(button);
      assert.deepEqual(await page.evaluate(() => calls), [
        {kind:'pick',pos:[0,1,-16],tool:'erase',shift:false,face:null},
        {kind:'use',pos:[0,1,-16],shift:false},
        {kind:'pick',pos:[0,1,-15],tool:'place',shift:false,face:'south'},
        {kind:'pickBlock',pos:[0,1,-16]},
      ]);
      await page.evaluate(() => { calls.length=0;consumeUse=true; });
      await click('right');
      assert.deepEqual(await page.evaluate(() => calls), [{kind:'use',pos:[0,1,-16],shift:false}]);
      await scene([[0,1,-17]]);
      await page.evaluate(() => { consumeUse=false; });
      for (const button of ['left','right','middle']) await click(button);
      assert.deepEqual(await page.evaluate(() => calls), []);
      await scene([], [.5,3,4], [.5,1,.5]);
      for (const button of ['left','right','middle']) await click(button);
      assert.deepEqual(await page.evaluate(() => calls), []);
    });

    await t.test('blocking input clears held movement, prevents edits and safely permits recapture', async () => {
      await scene([[0,1,0]]);
      const before = await page.evaluate(() => view.camera.position.toArray());
      await page.keyboard.down('w');
      await page.waitForTimeout(150);
      const moving = await page.evaluate(() => view.camera.position.toArray());
      assert.ok(moving[2] < before[2]-.1, 'captured WASD must move continuously');
      await page.evaluate(() => view.setInputBlocked(true));
      await page.waitForFunction(() => !view.pointerLocked && document.pointerLockElement === null && locks.at(-1) === false);
      const stopped = await page.evaluate(() => view.camera.position.toArray());
      await page.waitForTimeout(150);
      assert.deepEqual(await page.evaluate(() => view.camera.position.toArray()), stopped);
      await click();await click('right');
      assert.deepEqual(await page.evaluate(() => calls), []);
      await page.keyboard.up('w');
      await page.evaluate(() => view.setInputBlocked(false));
      await capture();
      const recaptured = await page.evaluate(() => view.camera.position.toArray());
      await page.waitForTimeout(150);
      assert.deepEqual(await page.evaluate(() => view.camera.position.toArray()), recaptured, 'old held keys must not resume');
      await scene([[0,1,0]]);
      await click();
      assert.equal(await page.evaluate(() => calls.at(-1)?.tool), 'erase');
      await release();
      assert.equal(await page.evaluate(() => locks.at(-1)), false);
      await page.evaluate(() => view.setFirstPerson(false));
      assert.equal(await page.evaluate(() => view.firstPerson), false);
    });
  } finally {
    await page.close();
  }
});

test('creative workbench supports fullscreen, nine slots and isolated E inventory', async t => {
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } });
  await page.route('**/api/bootstrap', route => route.fulfill({ json: { token:'test-fixture',projectFileVersion:1 } }));
  await page.routeWebSocket('**/socket?*', () => {});
  await page.goto(base);
  await page.locator('.viewportCanvas canvas').waitFor();
  await page.evaluate(async () => {
    const apiUrl=performance.getEntriesByType('resource').find(entry => new URL(entry.name).pathname === '/src/api.ts').name;
    const {connection}=await import(apiUrl);window.kernel=connection;window.commands=[];
    const names=['redstone_wire','repeater','comparator','redstone_torch','lever','redstone_lamp','stone','redstone_block','sticky_piston'];
    const properties=[{north:'side',east:'side',south:'side',west:'side',power:'0'},
      {facing:'north',delay:'1',powered:'false',locked:'false'},
      {facing:'north',mode:'compare',powered:'false'}, {lit:'true'},
      {facing:'north',face:'floor',powered:'false'}, {lit:'false'}, {}, {}, {facing:'north',extended:'false'}];
    names.forEach((name,index) => connection.states.set(index+1,{stateId:index+1,name:'minecraft:'+name,properties:properties[index]}));
    connection.catalog=[...connection.states.values()].map(def => ({name:def.name,defaultState:def.stateId,defaultProperties:def.properties,device:2,supportLevel:'implemented',
      properties:Object.fromEntries(Object.entries(def.properties).map(([key,value]) => [key,key === 'facing' ? ['north','east','south','west'] : [value]]))}));
    connection.connected=true;connection.status={...connection.status,name:'创造搭建交互测试',blocks:1};
    connection.request=async (cmd,body={}) => {commands.push({cmd,body});return {};};
    const cell={pos:[4,1,2],stateId:7,renderStateId:7,value:0,motion:0};
    connection.cells.set('4,1,2',cell);
    connection.dispatchEvent(new Event('catalog'));connection.dispatchEvent(new Event('status'));
    connection.dispatchEvent(new CustomEvent('cells',{detail:{full:true,changes:[cell]}}));
  });
  try {
    await t.test('fullscreen button reflects the actual browser fullscreen state', async () => {
      await page.getByTitle('进入全屏',{exact:true}).click();
      await page.waitForFunction(() => !!document.fullscreenElement);
      await page.getByTitle('退出全屏',{exact:true}).waitFor({state:'visible'});
      await page.waitForFunction(() => document.pointerLockElement === document.querySelector('.viewportCanvas canvas'));
      await page.keyboard.press('e');
      const inventory = page.locator('dialog.creativeInventory');
      await inventory.waitFor({state:'visible'});
      await page.waitForFunction(() => !document.pointerLockElement);
      await page.keyboard.press('Escape');
      await inventory.waitFor({state:'hidden'});
      await page.waitForFunction(() => document.pointerLockElement === document.querySelector('.viewportCanvas canvas'));
      assert.equal(await page.locator('.creativeResume').isVisible(), false, 'closing inventory must return directly to building');
      assert.equal(await page.evaluate(() => !!document.fullscreenElement), true, 'inventory Escape must preserve fullscreen');
      await page.evaluate(() => document.exitPointerLock());
      await page.waitForFunction(() => !document.pointerLockElement);
      await page.getByTitle('退出全屏',{exact:true}).click();
      await page.waitForFunction(() => !document.fullscreenElement);
      await page.getByTitle('进入全屏',{exact:true}).waitFor({state:'visible'});
      await page.evaluate(() => document.exitPointerLock());
      await page.waitForFunction(() => !document.pointerLockElement);
      await page.getByRole('button',{name:'返回工作台',exact:true}).click();
    });

    await t.test('E opens a modal inventory; text editing and modal keys cannot reach the world', async () => {
      // Chrome 使用 2 秒的鼠标锁请求窗口；独立场景之间等待窗口到期。
      await page.waitForTimeout(2200);
      await page.getByTitle('第一人称搭建',{exact:true}).click();
      await page.waitForFunction(() => document.querySelectorAll('.creativeHotbar button').length === 9);
      await page.keyboard.press('e');
      const inventory = page.locator('dialog.creativeInventory');
      await inventory.waitFor({state:'visible'});
      assert.equal(await inventory.evaluate(el => el.open), true);
      await page.waitForFunction(() => !document.pointerLockElement);
      const count = await page.evaluate(() => commands.length);
      const search = inventory.locator('input').first();
      await search.fill('ston');
      await search.press('e');
      assert.equal(await search.inputValue(), 'stone');
      assert.equal(await inventory.isVisible(), true);
      await search.fill('');
      await inventory.evaluate(el => {el.tabIndex=-1;el.focus();});
      for (const key of ['Enter','f','Delete','1','Space','w']) await page.keyboard.press(key);
      assert.equal(await page.evaluate(() => commands.length), count);
      await mkdir(new URL('../testResults/',import.meta.url),{recursive:true});
      await page.screenshot({path:fileURLToPath(new URL('../testResults/creativeInventory.png',import.meta.url))});
      await page.keyboard.press('e');
      await inventory.waitFor({state:'hidden'});
      await page.waitForFunction(() => document.pointerLockElement === document.querySelector('.viewportCanvas canvas'));
      await page.keyboard.press('e');
      await inventory.waitFor({state:'visible'});
      await page.keyboard.press('Escape');
      await inventory.waitFor({state:'hidden'});
      await page.waitForFunction(() => document.pointerLockElement === document.querySelector('.viewportCanvas canvas'));
      assert.equal(await page.locator('.creativeResume').isVisible(), false, 'inventory Escape must not show the resume panel');
      assert.equal(await page.evaluate(() => commands.length), count);
    });

    await t.test('inventory actually assigns, clears and searches without world edits', async () => {
      await page.waitForTimeout(2200);
      const count=await page.evaluate(()=>commands.length);
      await page.keyboard.press('e');
      const inventory=page.locator('dialog.creativeInventory');
      await inventory.waitFor({state:'visible'});
      const search=inventory.locator('input').first();
      await search.fill('stone');
      await inventory.getByRole('button',{name:'石头 · minecraft:stone',exact:true}).click();
      const slot=inventory.locator('.creativeInventoryHotbar button').nth(0);
      await slot.click();
      assert.match(await slot.getAttribute('aria-label'),/石头/);
      await slot.click({button:'right'});
      assert.match(await slot.getAttribute('aria-label'),/空/);
      await inventory.getByRole('button',{name:'石头 · minecraft:stone',exact:true}).click({modifiers:['Shift']});
      assert.match(await slot.getAttribute('aria-label'),/石头/);
      await search.fill('stone');
      await search.press('1');
      assert.equal(await search.inputValue(),'stone1','search digits must stay in the input');
      await search.fill('');
      await page.setViewportSize({width:680,height:700});
      const bounds=await inventory.boundingBox();
      assert.ok(bounds.x>=0&&bounds.x+bounds.width<=680&&bounds.y>=0&&bounds.y+bounds.height<=700);
      await page.screenshot({path:fileURLToPath(new URL('../testResults/creativeInventoryCompact.png',import.meta.url))});
      await page.keyboard.press('Escape');
      await inventory.waitFor({state:'hidden'});
      await page.waitForFunction(() => document.pointerLockElement === document.querySelector('.viewportCanvas canvas'));
      assert.equal(await page.locator('.creativeResume').isVisible(), false, 'Escape from the search input must resume building');
      await page.setViewportSize({width:1440,height:1000});
      assert.equal(await page.evaluate(()=>commands.length),count);
    });

    await t.test('Escape in the world still releases capture and allows resuming', async () => {
      await page.waitForTimeout(2200);
      await page.waitForFunction(() => document.pointerLockElement === document.querySelector('.viewportCanvas canvas'));
      await page.keyboard.press('Escape');
      await page.waitForFunction(() => !document.pointerLockElement);
      await page.locator('.creativeResume').waitFor({state:'visible'});
      assert.equal(await page.locator('dialog.creativeInventory').isVisible(), false);
      await page.getByRole('button',{name:'继续搭建',exact:true}).click();
      await page.waitForFunction(() => document.pointerLockElement === document.querySelector('.viewportCanvas canvas'));
      await page.locator('.creativeResume').waitFor({state:'hidden'});
      assert.equal(await page.locator('.creativeResume').isVisible(), false);
    });

    await t.test('number keys select all nine hotbar slots and returning restores the workbench', async () => {
      await page.locator('.viewportCanvas canvas').focus();
      const slots = page.locator('.creativeHotbar button');
      assert.equal(await slots.count(), 9);
      for (let slot=0;slot<9;slot++) {
        await page.keyboard.press(String(slot+1));
        assert.equal(await slots.nth(slot).getAttribute('aria-pressed'), 'true', `key ${slot+1} must select its slot`);
        assert.equal(await page.locator('.creativeHotbar button[aria-pressed="true"]').count(), 1);
      }
      await page.screenshot({path:fileURLToPath(new URL('../testResults/creativeWorkbench.png',import.meta.url))});
      // Release capture explicitly before using the workbench controls with the cursor.
      await page.evaluate(() => document.exitPointerLock());
      await page.waitForFunction(() => !document.pointerLockElement);
      await page.getByRole('button',{name:'返回工作台',exact:true}).click();
      assert.equal(await page.locator('.creativeHotbar').count(), 0);
      assert.equal(await page.getByTitle('第一人称搭建',{exact:true}).isVisible(), true);
      assert.equal(await page.evaluate(() => document.pointerLockElement), null);
    });
  } finally {
    await page.close();
  }
});
