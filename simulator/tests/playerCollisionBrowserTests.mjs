// Start simulator/apps/web with `npm run dev -- --port 5179 --strictPort`.
// Install Playwright in the environment or expose it through NODE_PATH.
import assert from 'node:assert/strict';
import { test, before, after } from 'node:test';
import { createRequire } from 'node:module';

const { chromium } = createRequire(import.meta.url)('playwright');
const base = process.env.VERIMC_TEST_URL ?? 'http://127.0.0.1:5179';
let browser;
before(async () => {
  browser = await chromium.launch({ channel: process.env.VERIMC_BROWSER ?? 'chrome', headless: true });
});
after(async () => { await browser?.close(); });

test('creative UI exposes optional collisions and reflects walking and double-Space flight', async () => {
  const page=await browser.newPage({viewport:{width:1440,height:1000}});
  const errors=[];
  page.on('pageerror',error=>errors.push(error.message));
  await page.route('**/api/bootstrap',route=>route.fulfill({json:{token:'test-fixture',projectFileVersion:1}}));
  await page.routeWebSocket('**/socket?*',()=>{});
  try {
    await page.goto(base);
    await page.locator('.viewportCanvas canvas').waitFor();
    await page.getByTitle('第一人称搭建',{exact:true}).click();
    await page.waitForFunction(()=>document.pointerLockElement===document.querySelector('.viewportCanvas canvas')&&document.activeElement===document.pointerLockElement);
    const mode=page.locator('.creativeTop > span');
    const off=page.getByRole('button',{name:'碰撞与重力：关',exact:true});
    assert.equal(await off.getAttribute('aria-pressed'),'false');
    assert.match(await mode.innerText(),/自由飞行/);
    await page.evaluate(()=>document.exitPointerLock());
    await page.waitForFunction(()=>!document.pointerLockElement);
    await off.click();
    const on=page.getByRole('button',{name:'碰撞与重力：开',exact:true});
    assert.equal(await on.getAttribute('aria-pressed'),'true');
    assert.match(await mode.innerText(),/行走 · 重力/);
    await page.getByRole('button',{name:'继续搭建',exact:true}).click();
    await page.waitForFunction(()=>document.pointerLockElement===document.querySelector('.viewportCanvas canvas')&&document.activeElement===document.pointerLockElement);
    await page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(resolve)));
    await page.keyboard.press('Space');
    await page.waitForTimeout(70);
    await page.keyboard.press('Space');
    await page.waitForFunction(()=>document.querySelector('.creativeTop > span').textContent.includes('飞行 · 碰撞'));
    assert.equal(await on.getAttribute('aria-pressed'),'true','collision stays enabled while flying');
    await page.evaluate(()=>document.exitPointerLock());
    await page.waitForFunction(()=>!document.pointerLockElement);
    await on.click();
    assert.equal(await off.getAttribute('aria-pressed'),'false');
    assert.match(await mode.innerText(),/自由飞行/);
    assert.deepEqual(errors,[],'movement mode UI must not produce browser errors');
  } finally {
    await page.close();
  }
});
test('first-person player volume collides with blocks while flying and building', async t => {
  const page = await browser.newPage({ viewport: { width: 1100, height: 800 } });
  const errors = [];
  page.on('pageerror', error => { errors.push(error.message); console.error(error); });
  await page.route('**/player-collision-harness', route => route.fulfill({ contentType: 'text/html', body: `
    <style>body{margin:0}#scene{width:1100px;height:800px}#capture{position:absolute;left:8px;top:8px}</style>
    <div id="scene"></div><button id="capture">Capture pointer</button>
    <script type="module">
      import { CircuitViewport } from '/src/viewport.ts';
      const apiUrl=performance.getEntriesByType('resource').find(entry=>new URL(entry.name).pathname==='/src/api.ts').name;
      const { connection }=await import(apiUrl);
      window.connection=connection;
      connection.states.set(1,{stateId:1,name:'minecraft:stone',properties:{}});
      for(const [stateId,name,properties] of [
        [2,'stone_slab',{type:'bottom'}],[3,'stone_slab',{type:'top'}],
        [4,'iron_door',{facing:'north',hinge:'left',half:'lower',open:'false'}],
        [5,'iron_door',{facing:'north',hinge:'left',half:'lower',open:'true'}],
        [6,'iron_trapdoor',{facing:'north',half:'bottom',open:'false'}],
        [7,'iron_trapdoor',{facing:'north',half:'bottom',open:'true'}],
        [8,'redstone_wire',{north:'none',east:'none',south:'none',west:'none',power:'0'}],
        [9,'redstone_torch',{lit:'true'}],[10,'lever',{face:'floor',facing:'north',powered:'false'}],
        [11,'rail',{shape:'north_south'}],
        [12,'oak_fence_gate',{facing:'north',open:'true'}],
        [13,'oak_fence_gate',{facing:'north',open:'false'}],
      ])connection.states.set(stateId,{stateId,name:'minecraft:'+name,properties});
      connection.catalog=[{name:'minecraft:stone',defaultState:1,defaultProperties:{},device:2,properties:{},supportLevel:'implemented'}];
      window.view=new CircuitViewport(document.querySelector('#scene'));
      window.calls=[];
      window.movementModeChanges=[];
      view.onPick=(pos,tool,shift,face)=>calls.push({pos,tool,shift,face});
      view.onMovementModeChange=mode=>movementModeChanges.push(mode);
      document.querySelector('#capture').onclick=()=>view.requestPointerLock();
      view.setPlacement('minecraft:stone',{});
      view.setCollisionEnabled(true);
      view.setFirstPerson(true);
      view.movement.setFlying(true);
      window.setScene=(positions,camera=[.5,1.62,3],target=[.5,1.62,0])=>{
        view.movementKeys.clear();
        if(view.movement.collisionEnabled)view.movement.setFlying(true);
        connection.cells.clear();
        for(const entry of positions){
          const cell=Array.isArray(entry)?{pos:entry,stateId:1,renderStateId:1,value:0,motion:0}:entry;
          connection.cells.set(cell.pos.join(','),cell);
        }
        connection.dispatchEvent(new CustomEvent('cells',{detail:{full:true,changes:[]}}));
        view.camera.position.fromArray(camera);view.controls.target.fromArray(target);
        view.camera.lookAt(...target);view.camera.updateMatrixWorld();
        calls.length=0;
        movementModeChanges.length=0;
      };
      window.moveSteps=(codes,steps=10)=>{
        view.movementKeys.clear();
        for(const code of codes)view.movementKeys.add(code);
        for(let step=0;step<steps;step++)view.moveCamera(.05);
        view.movementKeys.clear();
        return view.camera.position.toArray();
      };
      setScene([[0,0,0],[0,1,0]]);
    </script>` }));
  await page.goto(`${base}/player-collision-harness`);
  try {
    await page.waitForFunction(() => !!window.moveSteps);
  } catch (error) {
    throw new Error(`Collision fixture did not initialize: ${errors.join('; ') || error.message}`);
  }
  const capture = async () => {
    if (!await page.evaluate(() => view.pointerLocked)) await page.locator('#capture').click();
    await page.waitForFunction(() => view.pointerLocked && document.pointerLockElement === view.renderer.domElement && document.activeElement===view.renderer.domElement);
    await page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(resolve)));
  };
  const scene = (positions, camera, target) => page.evaluate(
    ({ positions, camera, target }) => setScene(positions, camera, target), { positions, camera, target });
  const move = (codes, steps) => page.evaluate(({ codes, steps }) => moveSteps(codes, steps), { codes, steps });
  const walking = () => page.evaluate(() => {view.setCollisionEnabled(false);view.setCollisionEnabled(true);});
  const cell = (pos,stateId,renderStateId=stateId,motion=0) => ({pos,stateId,renderStateId,value:0,motion});
  const update = changes => page.evaluate(changes => {
    for(const cell of changes)connection.cells.set(cell.pos.join(','),cell);
    connection.dispatchEvent(new CustomEvent('cells',{detail:{full:false,changes}}));
  },changes);
  const near = (actual, expected, label) => assert.ok(Math.abs(actual-expected)<2e-5, `${label}: expected ${expected}, got ${actual}`);
  try {
    await t.test('trusted Ctrl+W input stops the body at a wall without tunneling', async () => {
      await capture();
      await scene([[0,0,0],[0,1,0]]);
      await page.keyboard.down('Control');
      await page.keyboard.down('w');
      await page.waitForFunction(()=>view.camera.position.z<=1.29502,{},{timeout:3000});
      await page.waitForTimeout(100);
      await page.keyboard.up('w');
      await page.keyboard.up('Control');
      const position=await page.evaluate(() => view.camera.position.toArray());
      near(position[0],.5,'horizontal center');
      near(position[1],1.62,'flight height');
      near(position[2],1.295,'stone south face plus player radius');
      assert.equal(await page.evaluate(() => view.movement.flying),true,'sideways wall contact must preserve flight');
      assert.deepEqual(await page.evaluate(() => movementModeChanges),[],'wall contact must not report a mode change');
      const stopped=await page.evaluate(() => view.camera.position.toArray());
      await page.waitForTimeout(100);
      assert.deepEqual(await page.evaluate(() => view.camera.position.toArray()),stopped);
    });

    await t.test('deterministic maximum frame steps preserve sliding and negative coordinates', async () => {
      await scene([[0,0,0],[0,1,0],[1,0,0],[1,1,0],[2,0,0],[2,1,0]], [.5,1.62,1.5]);
      const sliding=await move(['ControlLeft','KeyW','KeyD'],2);
      assert.ok(sliding[0]>.8,'diagonal motion must slide along the wall');
      near(sliding[2],1.295,'sliding wall clearance');
      await scene([[-2,0,-2],[-2,1,-2]], [-1.5,1.62,1], [-1.5,1.62,-3]);
      const negative=await move(['ControlLeft','KeyW'],10);
      near(negative[2],-.705,'negative-coordinate south face plus radius');
    });

    await t.test('descending feet and ascending head stop at floor and ceiling', async () => {
      await scene([[0,0,0],[0,4,0]], [.5,3,.5], [.5,3,-1]);
      const floor=await move(['ShiftLeft'],10);
      near(floor[1],2.615,'floor top plus eye height');
      assert.equal(await page.evaluate(() => view.movement.flying),false,'descending onto a block must exit flight');
      assert.deepEqual(await page.evaluate(() => movementModeChanges),[{collisionEnabled:true,flying:false}],'landing must notify the viewport consumer once');
      const ceiling=await page.evaluate(() => {view.movement.setFlying(true);return moveSteps(['Space'],10);});
      near(ceiling[1],3.825,'ceiling bottom minus height above eyes');
      near(ceiling[0],.5,'vertical flight preserves x');
      near(ceiling[2],.5,'vertical flight preserves z');
      assert.equal(await page.evaluate(() => view.movement.flying),true,'head contact with a ceiling must preserve flight');
      assert.deepEqual(await page.evaluate(() => movementModeChanges),[{collisionEnabled:true,flying:false}],'ceiling contact must not report another mode change');
    });

    await t.test('landing restores jumping and a later double Space can start flight again', async () => {
      await scene([[0,0,0]],[.5,3,.5],[.5,3,-1]);
      near((await move(['ShiftLeft'],10))[1],2.615,'flight lands on the floor');
      assert.equal(await page.evaluate(() => view.movement.flying),false,'landed flight must become walking');
      near((await move(['ShiftLeft'],4))[1],2.615,'continued descent input must keep a landed player on the floor');
      assert.deepEqual(await page.evaluate(() => movementModeChanges),[{collisionEnabled:true,flying:false}],'remaining on the floor must not repeat the landing notification');
      await page.keyboard.press('Space');
      await page.waitForFunction(() => view.camera.position.y>2.8);
      assert.equal(await page.evaluate(() => view.movement.flying),false,'one Space after landing must only jump');
      await page.waitForFunction(() => Math.abs(view.camera.position.y-2.615)<2e-5);
      await page.waitForTimeout(350);
      await page.keyboard.press('Space');
      await page.waitForTimeout(70);
      await page.keyboard.press('Space');
      assert.equal(await page.evaluate(() => view.movement.flying),true,'a new double Space must permit takeoff after landing');
      const takeoffHeight=await page.evaluate(() => view.camera.position.y);
      assert.ok((await move(['Space'],2))[1]>takeoffHeight+.5,'restarted flight must ascend');
      assert.deepEqual(await page.evaluate(() => movementModeChanges),[{collisionEnabled:true,flying:false},{collisionEnabled:true,flying:true}],'landing and the subsequent takeoff must both update the consumer');
    });

    await t.test('incremental removal immediately opens the former collision area', async () => {
      await scene([[0,0,0],[0,1,0]]);
      near((await move(['ControlLeft','KeyW'],10))[2],1.295,'wall blocks movement before removal');
      await page.evaluate(() => {
        const changes=[[0,0,0],[0,1,0]].map(pos=>({pos,stateId:0,renderStateId:0,value:0,motion:0}));
        for(const cell of changes)connection.cells.delete(cell.pos.join(','));
        connection.dispatchEvent(new CustomEvent('cells',{detail:{full:false,changes}}));
      });
      const position=await move(['KeyW'],6);
      assert.ok(position[2]<-.5,'removed blocks must no longer stop the player');
    });

    await t.test('upper and lower slabs preserve their partial block heights', async () => {
      for(const [stateId,top] of [[2,.4975],[3,.9975]]) {
        await scene([cell([0,0,0],stateId)],[.5,3,.5],[.5,3,-1]);
        near((await move(['ShiftLeft'],10))[1],top+1.62,'slab surface plus eye height');
      }
    });

    await t.test('door and fence gate state changes replace their collision geometry', async () => {
      for(const [closed,opened] of [[4,5],[13,12]]) {
        await scene([cell([0,0,0],closed)]);
        const stopped=await move(['ControlLeft','KeyW'],10);
        assert.ok(stopped[2]>.8&&stopped[2]<1.32,'closed doorway must stop the player at its thin shape');
        await update([cell([0,0,0],opened)]);
        assert.ok((await move(['KeyW'],8))[2]<-.5,'opening the doorway must release the passage');
      }
    });

    await t.test('trapdoor changes between a horizontal floor and a thin vertical obstacle', async () => {
      await scene([cell([0,0,0],6)],[.5,3,.5],[.5,3,-1]);
      near((await move(['ShiftLeft'],10))[1],1.8075,'closed trapdoor top plus eye height');
      await update([cell([0,0,0],7)]);
      assert.ok((await move(['ShiftLeft'],10))[1]<0,'opened trapdoor must remove the former floor and let the landed player fall');
      await scene([cell([0,0,0],7)]);
      const stopped=await move(['ControlLeft','KeyW'],10);
      assert.ok(stopped[2]>=1.295&&stopped[2]<1.32,'opened trapdoor remains a thin vertical obstacle');
    });

    await t.test('redstone controls and rails remain passable despite their visible geometry', async () => {
      await scene([8,9,10,11].map((stateId,index)=>cell([0,0,-index],stateId)));
      const position=await move(['KeyW'],16);
      near(position[2],3-10.89*.05*16,'non-solid devices must not shorten the flight path');
    });

    await t.test('moving geometry collides across its source cell and spatial bucket boundary', async () => {
      // An eastward piston movement renders the stone half a cell west of [4,0,0].
      const halfway=1|2|(5<<3)|(1<<6),finished=1|2|(5<<3)|(2<<6);
      await scene([cell([4,0,0],999,1,halfway)],[2,1.62,.5],[6,1.62,.5]);
      near((await move(['ControlLeft','KeyW'],10))[0],3.205,'moving stone uses its translated geometry');
      await update([cell([4,0,0],999,1,finished)]);
      near((await move(['ControlLeft','KeyW'],10))[0],3.705,'motion update removes the previous collision shape');
    });

    await t.test('entering first person corrects an entry position inside blocks', async () => {
      await page.evaluate(() => {
        view.setFirstPerson(false);
        setScene([[0,0,4],[0,1,4]],[.5,1.62,8.5],[.5,1.62,.5]);
        view.controls.update();
        view.setFirstPerson(true);
        window.entryWasWalking=!view.movement.flying;
        view.movement.setFlying(true);
      });
      const position=await page.evaluate(() => view.camera.position.toArray());
      const overlaps=([x,y,z],blockY) => x+.3>.005+1e-6 && x-.3<.995-1e-6 &&
        y+.18>blockY+.005+1e-6 && y-1.62<blockY+.995-1e-6 &&
        z+.3>4.005+1e-6 && z-.3<4.995-1e-6;
      assert.ok(position.every(Number.isFinite),'resolved position must be finite');
      assert.ok(!overlaps(position,0)&&!overlaps(position,1),'entry must leave the entire player outside both blocks');
      assert.ok(position.some((value,index)=>Math.abs(value-[.5,1.62,4.5][index])>1e-4),'overlapping entry must be corrected');
      assert.equal(await page.evaluate(() => entryWasWalking),true,'entering first person with collisions enabled defaults to walking');
      await capture();
    });

    await t.test('placing a solid block through the player is blocked while a clear target still works', async () => {
      await scene([[0,0,0]], [.5,1.62,1.7], [.5,.5,.5]);
      await page.evaluate(() => view.creativeAction(2,false));
      assert.deepEqual(await page.evaluate(() => calls.filter(call=>call.tool==='place')),[], 'body-overlapping placement must not reach the editor');
      await scene([[0,0,0]], [.5,1.62,3.5], [.5,.5,.5]);
      await page.evaluate(() => view.creativeAction(2,false));
      assert.deepEqual(await page.evaluate(() => calls),[{pos:[0,0,1],tool:'place',shift:false,face:'south'}]);
    });

    await t.test('door placement checks the automatically added upper half against the player', async () => {
      await scene([[0,0,0]],[.5,3.7,1.2],[.5,1,.5]);
      await page.evaluate(() => view.setPlacement('minecraft:iron_door',{facing:'north',hinge:'left',half:'lower',open:'false'}));
      assert.deepEqual(await page.evaluate(() => view.locate(view.centerPointer(),'place')?.pos),[0,1,0]);
      await page.evaluate(() => view.creativeAction(2,false));
      assert.deepEqual(await page.evaluate(() => calls),[],'the upper half alone overlaps the player and must prevent placement');
      await scene([[0,0,0]],[.5,3.7,3],[.5,1,.5]);
      await page.evaluate(() => view.creativeAction(2,false));
      assert.deepEqual(await page.evaluate(() => calls.map(({pos,tool})=>({pos,tool}))),[{pos:[0,1,0],tool:'place'}]);
      await page.evaluate(() => view.setPlacement('minecraft:stone',{}));
    });

    await t.test('disabling collision permits wall traversal and removes gravity', async () => {
      await scene([[0,2,0],[0,3,0]],[.5,3.5,3],[.5,3.5,0]);
      await page.evaluate(() => view.setCollisionEnabled(false));
      assert.equal(await page.evaluate(() => view.movement.collisionEnabled),false);
      const stationary=await page.evaluate(() => view.camera.position.toArray());
      await page.waitForTimeout(200);
      assert.deepEqual(await page.evaluate(() => view.camera.position.toArray()),stationary,'free flight must not fall without input');
      const crossed=await move(['KeyW'],10);
      assert.ok(crossed[2]<-.5,'disabled collisions permit crossing solid blocks');
      near(crossed[1],3.5,'free flight preserves altitude');
      const raised=await move(['Space'],2);
      assert.ok(raised[1]>crossed[1]+.5,'Space ascends in free flight');
      near((await move(['ShiftLeft'],2))[1],crossed[1],'Shift descends in free flight');
      await scene([[0,0,0]],[.5,3,.5],[.5,3,-1]);
      assert.ok((await move(['ShiftLeft'],10))[1]<0,'free flight must descend through a block without landing');
      assert.equal(await page.evaluate(() => view.movement.flying),true,'disabled collisions must preserve flight while crossing a floor');
      assert.deepEqual(await page.evaluate(() => movementModeChanges),[],'crossing a floor without collision must not report a mode change');
    });

    await t.test('enabling collision defaults to walking, falls while idle, jumps, and keeps Shift above the floor', async () => {
      await scene([[0,0,0]],[.5,4,.5],[.5,4,-1]);
      await walking();
      assert.equal(await page.evaluate(() => view.movement.collisionEnabled),true);
      assert.equal(await page.evaluate(() => view.movement.flying),false);
      await page.waitForFunction(() => Math.abs(view.camera.position.y-2.615)<2e-5);
      await page.keyboard.down('Shift');
      await page.waitForTimeout(150);
      await page.keyboard.up('Shift');
      near(await page.evaluate(() => view.camera.position.y),2.615,'Shift must not descend through the walking floor');
      await page.keyboard.press('Space');
      await page.waitForFunction(() => view.camera.position.y>2.8);
      assert.equal(await page.evaluate(() => view.movement.flying),false,'one Space press jumps without toggling flight');
      await page.waitForFunction(() => Math.abs(view.camera.position.y-2.615)<2e-5);
      await page.waitForTimeout(100);
      near(await page.evaluate(() => view.camera.position.y),2.615,'jump returns to a stable floor height');
    });

    await t.test('two Space presses toggle collision flight and walking while held-key repeats do not', async () => {
      await scene([[0,0,0]],[.5,4,.5],[.5,4,-1]);
      await walking();
      await page.waitForTimeout(350);
      await page.keyboard.press('Space');
      await page.waitForTimeout(70);
      await page.keyboard.press('Space');
      assert.equal(await page.evaluate(() => view.movement.flying),true,'two separate presses must enter flight');
      const hovering=await page.evaluate(() => view.camera.position.toArray());
      await page.waitForTimeout(200);
      assert.deepEqual(await page.evaluate(() => view.camera.position.toArray()),hovering,'collision flight has no gravity');
      await scene([[0,0,0],[0,1,0]]);
      near((await move(['ControlLeft','KeyW'],10))[2],1.295,'flight with collisions enabled still stops at walls');
      await scene([[0,0,0]],[.5,4,.5],[.5,4,-1]);
      await page.waitForTimeout(350);
      await page.keyboard.press('Space');
      await page.waitForTimeout(70);
      await page.keyboard.press('Space');
      assert.equal(await page.evaluate(() => view.movement.flying),false,'another double press returns to walking');
      await page.waitForFunction(() => view.camera.position.y<3.9);
      await page.waitForTimeout(350);
      await page.evaluate(() => {
        window.spaceRepeats=[];
        view.renderer.domElement.addEventListener('keydown',event=>{if(event.code==='Space')spaceRepeats.push(event.repeat);});
      });
      await page.keyboard.down('Space');
      await page.waitForTimeout(70);
      await page.keyboard.down('Space');
      await page.keyboard.up('Space');
      assert.deepEqual(await page.evaluate(() => spaceRepeats),[false,true],'browser must identify the second held-key event as repeat');
      assert.equal(await page.evaluate(() => view.movement.flying),false,'a repeated held Space must not count as a second press');
    });

    await t.test('losing pointer lock or window focus freezes gravity until input resumes', async () => {
      await scene([[0,0,0]],[.5,6,.5],[.5,6,-1]);
      await walking();
      await page.waitForFunction(() => view.camera.position.y<5.9);
      await page.evaluate(() => view.releasePointerLock());
      await page.waitForFunction(() => !view.pointerLocked && document.pointerLockElement===null);
      const paused=await page.evaluate(() => view.camera.position.toArray());
      await page.waitForTimeout(200);
      assert.deepEqual(await page.evaluate(() => view.camera.position.toArray()),paused,'unlocked mode must not continue falling');
      await capture();
      await page.waitForFunction(previous => view.camera.position.y<previous-.05,paused[1]);
      await scene([[0,0,0]],[.5,6,.5],[.5,6,-1]);
      await walking();
      const blurred=await page.evaluate(()=>{
        window.dispatchEvent(new Event('blur'));
        const before=view.camera.position.toArray();
        for(let step=0;step<10;step++)view.moveCamera(.05);
        return {before,after:view.camera.position.toArray(),locked:view.pointerLocked};
      });
      assert.equal(blurred.locked,true,'focus loss may preserve browser pointer lock');
      assert.deepEqual(blurred.after,blurred.before,'window blur must freeze even explicitly stepped gravity');
      const focused=await page.evaluate(()=>{
        window.dispatchEvent(new Event('focus'));
        for(let step=0;step<5;step++)view.moveCamera(.05);
        return view.camera.position.toArray();
      });
      assert.ok(focused[1]<blurred.before[1]-.05,'focus restoration resumes gravity');
      await page.evaluate(() => view.movement.setFlying(true));
    });

    await t.test('returning to the orbit workbench preserves unrestricted camera movement', async () => {
      await page.evaluate(() => view.setFirstPerson(false));
      await scene([[0,0,0],[0,1,0]]);
      assert.equal(await page.evaluate(() => view.firstPerson),false);
      const position=await move(['KeyW'],10);
      assert.ok(position[2]<-1,'orbit camera must retain its existing movement through the scene');
      assert.equal(await page.evaluate(() => document.pointerLockElement),null);
    });
    assert.deepEqual(errors,[],'collision scenarios must not produce browser errors');
  } finally {
    await page.close();
  }
});
