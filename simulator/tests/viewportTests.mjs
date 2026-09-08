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
  page.on('pageerror', error => console.error(error));
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
      window.pointAt=pos=>{ view.camera.updateMatrixWorld(); const p=new THREE.Vector3(...pos).project(view.camera); return [(p.x+1)*550,(1-p.y)*400]; };
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
  await page.mouse.move(...point); await page.mouse.click(...point,{button:'right'});
  assert.deepEqual(await page.evaluate(() => picks.at(-1)),{pos:[-2,2,-3],tool:'place'});
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
  assert.equal(geometry.visible,true); assert.deepEqual(geometry.position,[-2,2,-3]);
  assert.equal(geometry.world.length,geometry.preview.length);
  geometry.world.forEach((part,i)=>{
    assert.equal(part.color,geometry.preview[i].color);
    part.matrix.forEach((value,j)=>assert.ok(Math.abs(value-geometry.preview[i].matrix[j])<1e-5,'Float32 transforms must match after chunk translation'));
  });
});

test('layer and tool changes update an idle pointer preview immediately', async () => {
  const point=await page.evaluate(() => pointAt([.5,1,.5]));
  await page.mouse.move(...point);
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

test('building clicks remove or place while right drags never edit, including return to start', async () => {
  await page.evaluate(() => {
    view.setSection('none');view.setLayer(1,false);view.tool='place';view.controls.enableDamping=false;
    view.camera.position.set(-2,18,4);view.controls.target.set(-2,1,-3);view.controls.update();picks.length=0;
  });
  let point=await page.evaluate(()=>pointAt([-1.5,1.1,-2.5]));
  await page.mouse.click(...point);
  assert.deepEqual(await page.evaluate(()=>picks.at(-1)),{pos:[-2,1,-3],tool:'erase'});
  await page.mouse.click(...point,{button:'right'});
  assert.deepEqual(await page.evaluate(()=>picks.at(-1)),{pos:[-2,2,-3],tool:'place'});
  const camera=await page.evaluate(()=>view.camera.position.toArray());
  await page.mouse.down({button:'right'});await page.mouse.move(point[0]+70,point[1]+30,{steps:5});
  assert.notDeepEqual(await page.evaluate(()=>view.camera.position.toArray()),camera);
  await page.mouse.move(...point,{steps:5});await page.mouse.up({button:'right'});
  assert.equal(await page.evaluate(()=>picks.length),2);
  point=await page.evaluate(()=>pointAt([2.5,1,2.5]));
  await page.mouse.click(...point,{button:'right'});
  assert.deepEqual(await page.evaluate(()=>picks.at(-1)),{pos:[2,1,2],tool:'place'});
  for(const tool of ['select','probe','interact']) {
    await page.evaluate(tool=>{view.tool=tool;},tool);
    point=await page.evaluate(()=>pointAt([-1.5,1.1,-2.5]));await page.mouse.click(...point);
    assert.deepEqual(await page.evaluate(()=>picks.at(-1)),{pos:[-2,1,-3],tool});
  }
});

test('held movement follows heading horizontally, ascends and descends, and stops on blur', async () => {
  await page.evaluate(()=>{view.camera.position.set(0,12,12);view.controls.target.set(0,0,0);view.controls.update();});
  await page.locator('canvas').focus();
  const position=()=>page.evaluate(()=>view.camera.position.toArray());
  const samePosition=(actual,expected)=>actual.forEach((value,index)=>assert.ok(Math.abs(value-expected[index])<1e-8,'camera must remain stationary within floating-point precision'));
  const hold=async key=>{const before=await position();await page.keyboard.down(key);await page.waitForTimeout(150);await page.keyboard.up(key);return {before,after:await position()};};
  for(const [key,axis,sign] of [['w',2,-1],['s',2,1],['a',0,-1],['d',0,1],['Space',1,1],['Shift',1,-1]]) {
    const {before,after}=await hold(key);
    assert.ok((after[axis]-before[axis])*sign>.1,key+' must move continuously');
    for(let i=0;i<3;i++)if(i!==axis)assert.ok(Math.abs(after[i]-before[i])<1e-6,key+' must preserve other axes');
  }
  await page.evaluate(()=>{view.camera.position.set(12,12,0);view.controls.target.set(0,0,0);view.controls.update();});
  const rotated=await hold('w');assert.ok(rotated.after[0]<rotated.before[0]-.1);assert.ok(Math.abs(rotated.after[1]-rotated.before[1])<1e-6);
  await page.keyboard.down('w');
  await page.evaluate(()=>{const input=document.createElement('input');input.id='edit';document.body.append(input);input.focus();});
  const stopped=await position();await page.waitForTimeout(150);await page.keyboard.up('w');
  samePosition(await position(),stopped);
  await page.keyboard.press('Space');await page.keyboard.press('w');await page.keyboard.press('Shift');
  samePosition(await position(),stopped);
  await page.evaluate(()=>document.querySelector('#edit').remove());await page.locator('canvas').focus();
  await page.keyboard.down('Control');const modified=await hold('w');await page.keyboard.up('Control');
  samePosition(modified.after,modified.before);
});

test('render placement preview for visual review', async () => {
  await page.evaluate(() => {view.select(null);view.setLayer(1,false);view.tool='place';});
  const point=await page.evaluate(() => pointAt([.5,1,.5]));
  await page.mouse.move(...point);
  await page.evaluate(() => new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
  await mkdir(new URL('../testResults/',import.meta.url),{recursive:true});
  await page.screenshot({path:fileURLToPath(new URL('../testResults/placementPreview.png',import.meta.url))});
});

test('repeater spacing increases across all four delays and facings; lock replaces the movable torch', async () => {
  const cases=await page.evaluate(() => {
    const results=[];
    for(const [facing,v] of Object.entries({north:[0,0,-1],south:[0,0,1],east:[1,0,0],west:[-1,0,0]}))for(let delay=1;delay<=4;delay++)for(const locked of [false,true])for(const powered of [false,true]) {
      view.setPlacement('minecraft:repeater',{facing,delay:String(delay),locked:String(locked),powered:String(powered)});
      const parts=view.previewHandles.filter(h=>h.pool===view.previewChunk.box).map(h=>{
        const m=new THREE.Matrix4(),position=new THREE.Vector3(),size=new THREE.Vector3();h.pool.mesh.getMatrixAt(h.index,m);m.decompose(position,new THREE.Quaternion(),size);
        return {along:(position.x-.5)*v[0]+(position.z-.5)*v[2],height:size.y,width:size.x};
      });
      const stems=parts.filter(p=>Math.abs(p.height-5/16)<1e-6).map(p=>p.along).sort((a,b)=>a-b);
      const bar=parts.find(p=>Math.abs(p.width-12/16)<1e-6);
      const texture=view.previewHandles.find(h=>h.pool.geometry.type==='PlaneGeometry').pool.poolMaterial;
      results.push({delay,locked,powered,stems,bar:bar?.along,textureMatches:texture===view.deviceTextures.get(`repeater:${Number(powered)}:${delay}`,true)});
    }
    return results;
  });
  assert.equal(cases.length,64);
  for(const c of cases) {
    assert.equal(c.textureMatches,true);
    assert.equal(c.stems.length,c.locked?1:2);
    assert.ok(Math.abs(c.stems[0]+5/16)<1e-6,'fixed output torch');
    const movable=c.locked?c.bar:c.stems[1];
    assert.ok(Math.abs(movable-(-1/16+(c.delay-1)/8))<1e-6,'movable torch / lock position');
    assert.ok(Math.abs(movable-c.stems[0]-(c.delay+1)/8)<1e-6,'1–4 delays have 4, 6, 8, 10 pixel spacing');
  }
});

test('comparator mode indicator is independent of output, and observer faces rotate in six directions',async()=>{
  const result=await page.evaluate(()=>{
    const comparators=[];
    for(const mode of ['compare','subtract'])for(const powered of ['false','true']) {
      view.setPlacement('minecraft:comparator',{facing:'south',mode,powered});
      const front=view.previewHandles.filter(h=>h.pool===view.previewChunk.box).map(h=>{const m=new THREE.Matrix4();h.pool.mesh.getMatrixAt(h.index,m);return m.elements;}).filter(m=>Math.abs(m[14]-3/16)<1e-6);
      const head=front.find(m=>Math.abs(m[5]-.125)<1e-6);
      comparators.push({mode,powered,top:head[13]+head[5]/2,lit:view.previewHandles.filter(h=>h.pool.poolMaterial===view.deviceTextures.get('torch:1',true)).length});
    }
    const observers=[],pistons=[];
    for(const [facing,v] of Object.entries({north:[0,0,-1],south:[0,0,1],east:[1,0,0],west:[-1,0,0],up:[0,1,0],down:[0,-1,0]})) {
      view.setPlacement('minecraft:observer',{facing,powered:'true'});
      const center=key=>{const h=view.previewHandles.find(h=>h.pool.poolMaterial===view.deviceTextures.get(key,true));const m=new THREE.Matrix4();h.pool.mesh.getMatrixAt(h.index,m);return m.elements.slice(12,15).map((n,i)=>(n-.5)*v[i]);};
      const arrows=view.previewHandles.filter(h=>h.pool.poolMaterial===view.deviceTextures.get('observerSide:1',true)).map(h=>{const m=new THREE.Matrix4();h.pool.mesh.getMatrixAt(h.index,m);return new THREE.Vector3(0,-1,0).transformDirection(m).dot(new THREE.Vector3(...v));});
      observers.push({front:center('observerFront:1').reduce((a,b)=>a+b,0),back:center('observerBack:1').reduce((a,b)=>a+b,0),arrows});
      view.setPlacement('minecraft:piston',{facing,extended:'true'});
      const corners=view.previewHandles.filter(h=>h.pool.geometry.type==='PlaneGeometry').flatMap(h=>{
        const m=new THREE.Matrix4();h.pool.mesh.getMatrixAt(h.index,m);
        return [-.5,.5].flatMap(x=>[-.5,.5].map(y=>new THREE.Vector3(x,y,0).applyMatrix4(m).addScalar(-.5).dot(new THREE.Vector3(...v))));
      });
      pistons.push({min:Math.min(...corners),max:Math.max(...corners)});
    }
    return {comparators,observers,pistons};
  });
  for(const c of result.comparators){assert.ok(Math.abs(c.top-(c.mode==='subtract'?5/16:4/16))<1e-6);assert.equal(c.lit,(c.mode==='subtract'?6:0)+(c.powered==='true'?12:0));}
  for(const o of result.observers){assert.ok(Math.abs(o.front-.491)<1e-6);assert.ok(Math.abs(o.back+.491)<1e-6);for(const arrow of o.arrows)assert.ok(Math.abs(arrow+1)<1e-6);}
  for(const p of result.pistons){assert.ok(Math.abs(p.min+.501)<1e-6);assert.ok(Math.abs(p.max-.251)<1e-6,'extended piston textures must stop at the shortened body');}
});

test('render device material gallery for visual review',async()=>{
  await page.evaluate(()=>{
    connection.cells.clear();connection.states.clear();view.setSection('none');view.setLayer(0,false);view.tool='select';view.select(null);
    let id=1;
    const add=(name,properties,pos)=>{const stateId=id++;connection.states.set(stateId,{name:`minecraft:${name}`,stateId,properties});connection.cells.set(pos.join(','),{pos,stateId,renderStateId:stateId,motion:0,value:0});};
    for(let delay=1;delay<=4;delay++)for(let row=0;row<3;row++)add('repeater',{facing:'south',delay:String(delay),powered:String(row===1),locked:String(row===2)},[(delay-1)*2,0,row*2]);
    for(let i=0;i<4;i++)add('comparator',{facing:'south',powered:String(i%2===1),mode:i<2?'compare':'subtract'},[i*2,0,6]);
    for(let i=0;i<4;i++)add('observer',{facing:['south','east','up','north'][i],powered:String(i%2===1)},[i*2,0,8]);
    for(let i=0;i<4;i++)add(i<2?'piston':'sticky_piston',{facing:['south','up','east','up'][i],extended:'false'},[i*2,0,10]);
    for(let i=0;i<4;i++)add('redstone_wire',{power:String(i*5),north:'side',south:'side',east:'side',west:'side'},[i*2,0,12]);
    add('redstone_lamp',{lit:'false'},[8,0,8]);add('redstone_lamp',{lit:'true'},[8,0,10]);
    connection.dispatchEvent(new CustomEvent('cells',{detail:{full:true,changes:[]}}));
    view.camera.position.set(9,20,21);view.controls.target.set(4,0,6);view.controls.update();
  });
  await page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
  await page.screenshot({path:fileURLToPath(new URL('../testResults/deviceMaterials.png',import.meta.url))});
  await page.evaluate(()=>{view.camera.position.set(11,8,16);view.controls.target.set(5,0,9);view.controls.update();});
  await page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
  await page.screenshot({path:fileURLToPath(new URL('../testResults/deviceFaces.png',import.meta.url))});
  await page.evaluate(()=>{view.camera.position.set(3.5,11,3.6);view.controls.target.set(3.5,0,3.5);view.controls.update();});
  await page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
  await page.screenshot({path:fileURLToPath(new URL('../testResults/repeaterDelays.png',import.meta.url))});
});

test('torch face picking, preview and placed geometry agree on four walls and the top',async()=>{
  await page.evaluate(()=>{
    connection.cells.clear();connection.states.clear();view.setSection('none');view.setLayer(4,false);view.tool='place';view.select(null);
    connection.states.set(1,{stateId:1,name:'minecraft:stone',properties:{}});
    connection.cells.set('-2,4,-3',{pos:[-2,4,-3],stateId:1,renderStateId:1,motion:0,value:0});
    connection.dispatchEvent(new CustomEvent('cells',{detail:{full:true,changes:[]}}));
    view.controls.enableDamping=false;
    view.onPick=(pos,tool,_additive,face)=>picks.push({pos,tool,face});picks.length=0;
    view.setPlacement('minecraft:redstone_torch',{lit:'true'});
  });
  const directions={north:[0,0,-1],south:[0,0,1],east:[1,0,0],west:[-1,0,0],up:[0,1,0],down:[0,-1,0]};
  for(const [face,v] of Object.entries(directions)) {
    const point=await page.evaluate(v=>{
      const center=new THREE.Vector3(-1.5,4.5,-2.5);
      view.camera.position.copy(center).addScaledVector(new THREE.Vector3(...v),8);
      if(v[1])view.camera.position.z+=.01;
      view.controls.target.copy(center);view.controls.update();
      return pointAt(center.addScaledVector(new THREE.Vector3(...v),.49).toArray());
    },v);
    await page.mouse.move(...point);
    await page.mouse.click(...point,{button:'right'});
    const pos=[-2+v[0],4+v[1],-3+v[2]];
    assert.deepEqual(await page.evaluate(()=>picks.at(-1)),{pos,tool:'place',face});
    assert.equal(await page.evaluate(()=>view.ghost.visible),face!=='down');
    if(face==='down')continue;
    const name=face==='up'?'minecraft:redstone_torch':'minecraft:redstone_wall_torch';
    const properties=face==='up'?{lit:'true'}:{lit:'true',facing:face};
    const result=await page.evaluate(({name,properties,pos,v})=>{
      const stateId=2;
      const cell={pos,stateId,renderStateId:stateId,motion:0,value:0};
      connection.states.set(stateId,{name,properties,stateId});connection.cells.set(pos.join(','),cell);
      // Draw without refreshing the pointer so we can compare the placement preview to the placed block.
      view.drawCell(cell);
      const matrices=(handles,origin)=>handles.map(h=>{
        const m=new THREE.Matrix4();h.pool.mesh.getMatrixAt(h.index,m);
        m.elements[12]+=h.pool.group.position.x-origin[0];
        m.elements[13]+=h.pool.group.position.y-origin[1];
        m.elements[14]+=h.pool.group.position.z-origin[2];
        return m.elements;
      });
      const preview=matrices(view.previewHandles,pos),world=matrices(view.handles.get(pos.join(',')),pos);
      const stem=new THREE.Matrix4();view.previewChunk.box.mesh.getMatrixAt(view.previewHandles[0].index,stem);
      const tilt=new THREE.Vector3(0,1,0).transformDirection(stem);
      const textureCount=view.previewHandles.filter(h=>h.pool.poolMaterial===view.deviceTextures.get('torch:1',true)).length;
      connection.cells.delete(pos.join(','));view.drawCell({...cell,stateId:0});
      return {preview,world,tilt:tilt.toArray(),textureCount};
    },{name,properties,pos,v});
    assert.equal(result.preview.length,8,'torch must have a stem, head and six textured faces');
    assert.equal(result.textureCount,6);
    result.world.forEach((m,i)=>m.forEach((value,j)=>assert.ok(Math.abs(value-result.preview[i][j])<1e-5,`${face}: placed model must match preview`)));
    assert.ok(result.tilt[1]>.9);
    if(face!=='up')assert.ok(result.tilt[0]*v[0]+result.tilt[2]*v[2]>.3,'torch must lean away from its support');
    if(face==='south') {
      await page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(resolve)));
      await page.screenshot({path:fileURLToPath(new URL('../testResults/wallTorchPreview.png',import.meta.url))});
    }
  }
  // A copied wall torch also becomes a standing torch on the empty edit plane.
  await page.evaluate(()=>{
    view.camera.position.set(0,12,8);view.controls.target.set(0,4,0);view.controls.update();
    view.setPlacement('minecraft:redstone_wall_torch',{facing:'west',lit:'false'});
  });
  const point=await page.evaluate(()=>pointAt([.5,4,.5]));await page.mouse.move(...point);
  assert.deepEqual(await page.evaluate(()=>JSON.parse(view.placementKey)),['minecraft:redstone_torch',[['lit','false']]]);
  assert.equal(await page.evaluate(()=>view.ghost.visible),true);
});
