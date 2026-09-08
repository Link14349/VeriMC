// Start simulator/apps/web with `npm run dev -- --port 5179 --strictPort`.
// Install Playwright in the environment or expose it through NODE_PATH.
import assert from 'node:assert/strict';
import { test, before, after } from 'node:test';
import { createRequire } from 'node:module';
import { mkdir } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
const { chromium } = createRequire(import.meta.url)('playwright');
const base = process.env.VERIMC_TEST_URL ?? 'http://127.0.0.1:5179';
let browser, page;
before(async () => {
  browser = await chromium.launch({ channel: process.env.VERIMC_BROWSER ?? 'chrome', headless: true });
  page = await browser.newPage({ viewport: { width: 1100, height: 800 } });
  await page.route('**/viewport-harness', route => route.fulfill({ contentType: 'text/html', body: `
    <style>body{margin:0}#scene{width:1100px;height:800px}</style><div id="scene"></div>
    <script type="module">
      import * as THREE from '/node_modules/.vite/deps/three.js';
      import { CircuitViewport } from '/src/viewport.ts';
      const apiUrl=performance.getEntriesByType('resource').find(entry=>new URL(entry.name).pathname==='/src/api.ts').name;
      const { connection } = await import(apiUrl);
      window.THREE=THREE; window.connection=connection;
      window.view=new CircuitViewport(document.querySelector('#scene'));
      window.picks=[]; view.onPick=(pos,tool)=>picks.push({pos,tool});
      window.pointAt=pos=>{ const p=new THREE.Vector3(...pos).project(view.camera); return [(p.x+1)*550,(1-p.y)*400]; };
    </script>` }));
  await page.goto(`${base}/viewport-harness`);
  await page.waitForFunction(() => !!window.view);
});
after(async () => { await browser?.close(); });

test('placement reuses world geometry and preserves negative world coordinates', async () => {
  await page.evaluate(() => {
    const def={stateId:1,name:'minecraft:repeater',properties:{facing:'west',delay:'1',locked:'false',powered:'false'}};
    connection.states.set(1,def);
    const cell={pos:[-2,1,-3],stateId:1,renderStateId:1,value:0,motion:0};
    connection.cells.set('-2,1,-3',cell);
    connection.dispatchEvent(new CustomEvent('cells',{detail:{full:true,changes:[]}}));
    view.setPlacement(def.name,def.properties); view.tool='place';
    view.camera.position.set(-2,18,4); view.controls.target.set(-2,1,-3); view.controls.update();
  });
  const point=await page.evaluate(() => pointAt([-1.5,1,-2.5]));
  await page.mouse.move(...point); await page.mouse.click(...point);
  assert.deepEqual(await page.evaluate(() => picks.at(-1)),{pos:[-2,1,-3],tool:'place'});
  const geometry=await page.evaluate(() => {
    const world=view.handles.get('-2,1,-3'), preview=view.previewHandles;
    const parts=handles=>handles.map(h=>{
      const m=new THREE.Matrix4(); h.pool.mesh.getMatrixAt(h.index,m);
      const c=new THREE.Color(); h.pool.mesh.getColorAt(h.index,c);
      return {matrix:m.elements.map((v,i)=>i===12?v-14:i===13?v-1:i===14?v-13:v),color:c.getHex()};
    });
    const previewParts=preview.map(h=>{const m=new THREE.Matrix4();h.pool.mesh.getMatrixAt(h.index,m);const c=new THREE.Color();h.pool.mesh.getColorAt(h.index,c);return {matrix:m.elements,color:c.getHex()};});
    return {world:parts(world),preview:previewParts,position:view.ghost.position.toArray(),visible:view.ghost.visible};
  });
  assert.equal(geometry.visible,true); assert.deepEqual(geometry.position,[-2,1,-3]);
  assert.equal(geometry.world.length,geometry.preview.length);
  geometry.world.forEach((part,i)=>{
    assert.equal(part.color,geometry.preview[i].color);
    part.matrix.forEach((value,j)=>assert.ok(Math.abs(value-geometry.preview[i].matrix[j])<1e-5,'Float32 transforms must match after chunk translation'));
  });
});

test('layer and tool changes update an idle pointer preview immediately', async () => {
  await page.evaluate(() => view.setLayer(4,true));
  assert.equal(await page.evaluate(() => view.hover[1]),4);
  assert.equal(await page.evaluate(() => view.grid.position.y),4.002);
  assert.equal(await page.evaluate(() => view.ghost.position.y),4);
  await page.evaluate(() => { view.tool='select'; });
  assert.equal(await page.evaluate(() => view.ghost.visible),false);
  await page.evaluate(() => { view.tool='place'; });
  assert.equal(await page.evaluate(() => view.ghost.visible),true);
  await page.evaluate(() => view.setLayer(Infinity,false));
  assert.equal(await page.evaluate(() => view.layer),4);
  await page.evaluate(() => view.setLayer(-100,false));
  assert.equal(await page.evaluate(() => view.layer),-64);
  await page.evaluate(() => view.setLayer(400,false));
  assert.equal(await page.evaluate(() => view.layer),319);
});

test('rotation refreshes directional preview, cutaway selection and pointer exit', async () => {
  await page.evaluate(() => {
    view.setLayer(1,false);
    view.setPlacement('minecraft:repeater',{facing:'east',delay:'4'});
  });
  const direction=await page.evaluate(() => new THREE.Vector3(0,1,0).applyQuaternion(view.previewArrow.quaternion).toArray());
  assert.ok(Math.abs(direction[0]+1)<1e-6);
  await page.evaluate(() => {view.select([0,3,0]);view.setLayer(1,true);});
  assert.equal(await page.evaluate(() => view.selection.visible),false);
  await page.evaluate(() => view.setLayer(3,true));
  assert.equal(await page.evaluate(() => view.selection.visible),true);
  await page.evaluate(() => view.renderer.domElement.dispatchEvent(new PointerEvent('pointerleave')));
  assert.equal(await page.evaluate(() => view.ghost.visible),false);
  assert.equal(await page.evaluate(() => view.hover),null);
});

test('three-axis section hides selection and placement without mutating world cells', async () => {
  const before=await page.evaluate(()=>JSON.stringify([...connection.cells]));
  for (const axis of ['x','y','z']) {
    await page.evaluate(axis=>{view.select([2,2,2]);view.setLayer(3,false);view.setSection(axis,1);},axis);
    assert.equal(await page.evaluate(()=>view.selection.visible),false);
    await page.evaluate(axis=>view.setSection(axis,2),axis);
    assert.equal(await page.evaluate(()=>view.selection.visible),true);
  }
  await page.evaluate(()=>{view.setLayer(1,false);view.setSection('x',-1);view.tool='place';});
  const point=await page.evaluate(()=>pointAt([.5,1,.5]));
  await page.mouse.move(...point);
  assert.equal(await page.evaluate(()=>view.hover),null);
  await page.evaluate(()=>view.setSection('none'));
  assert.deepEqual(await page.evaluate(()=>view.hover),[0,1,0]);
  assert.equal(await page.evaluate(()=>JSON.stringify([...connection.cells])),before);
});

test('render placement preview for visual review', async () => {
  await page.evaluate(() => {view.select(null);view.setLayer(1,false);view.tool='place';});
  const point=await page.evaluate(() => pointAt([.5,1,.5]));
  await page.mouse.move(...point);
  await page.evaluate(() => new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
  await mkdir(new URL('../testResults/',import.meta.url),{recursive:true});
  await page.screenshot({path:fileURLToPath(new URL('../testResults/placementPreview.png',import.meta.url))});
});
