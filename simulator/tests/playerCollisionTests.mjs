import assert from 'node:assert/strict';
import { test, after } from 'node:test';
import { readFile, writeFile, mkdtemp, rm } from 'node:fs/promises';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { join } from 'node:path';
import ts from '../apps/web/node_modules/typescript/lib/typescript.js';
import { Box3, Vector3 } from '../apps/web/node_modules/three/build/three.module.js';

const scratch=await mkdtemp(fileURLToPath(new URL('../apps/web/node_modules/.playerCollision-',import.meta.url)));
after(()=>rm(scratch,{recursive:true,force:true}));
const source=await readFile(new URL('../apps/web/src/playerCollision.ts',import.meta.url),'utf8');
const output=ts.transpileModule(source,{compilerOptions:{target:ts.ScriptTarget.ES2022,module:ts.ModuleKind.ESNext}}).outputText;
await writeFile(join(scratch,'playerCollision.mjs'),output);
const { PlayerCollisionWorld, playerBounds, overlaps }=await import(pathToFileURL(join(scratch,'playerCollision.mjs')));
const movementSource=await readFile(new URL('../apps/web/src/playerMovement.ts',import.meta.url),'utf8');
await writeFile(join(scratch,'playerMovement.mjs'),ts.transpileModule(movementSource,{compilerOptions:{target:ts.ScriptTarget.ES2022,module:ts.ModuleKind.ESNext}}).outputText);
const { PlayerMovement }=await import(pathToFileURL(join(scratch,'playerMovement.mjs')));
const vec=(x,y,z)=>new Vector3(x,y,z);
const box=(min,max)=>new Box3(vec(...min),vec(...max));
const near=(actual,expected)=>assert.ok(Math.abs(actual-expected)<1e-6,`expected ${expected}, got ${actual}`);

test('swept player volume catches thin walls in both directions without point-camera tunneling',()=>{
  for(const axis of ['x','y','z'])for(const sign of [1,-1]) {
    const world=new PlayerCollisionWorld(), wall=box([-10,-10,-10],[10,10,10]);
    wall.min[axis]=0;wall.max[axis]=.01;world.set('wall',[wall]);
    const eye=vec(0,1.62,0);eye[axis]=sign>0?-5:5;
    const movement=vec(0,0,0);movement[axis]=sign*20;
    const accepted=world.move(eye,movement), destination=eye.clone().add(accepted), body=playerBounds(destination);
    near(sign>0?body.max[axis]:body.min[axis],sign>0?0:.01);
    assert.equal(world.intersects(destination),false);
    near(world.move(destination,movement)[axis],0);
    assert.ok(world.move(destination,movement.clone().negate())[axis]*sign<0,'moving away remains possible');
  }
});

test('corner walls stop both horizontal axes while a single wall allows sliding',()=>{
  const world=new PlayerCollisionWorld();
  world.set('north',[box([-10,-3,0],[10,4,1])]);
  const eye=vec(-2,1.62,-2), diagonal=vec(5,0,5);
  near(world.move(eye,diagonal).x,5);
  world.set('east',[box([0,-3,-10],[1,4,10])]);
  const result=eye.add(world.move(eye,diagonal));
  near(result.x,-.3);near(result.z,-.3);
  assert.equal(world.intersects(result),false);
});

test('touching a floor permits horizontal movement across adjacent blocks without vertical drift',()=>{
  const world=new PlayerCollisionWorld();
  for(let x=-8;x<8;x++)world.set(String(x),[box([x,0,0],[x+1,1,1])]);
  const eye=vec(-6.5,2.62,.5), delta=world.move(eye,vec(12,-5,0));
  near(delta.x,12);near(delta.y,0);
  assert.equal(world.intersects(eye.add(delta)),false);
});

test('spatial index follows actual off-cell geometry and discards replaced, deleted and reset shapes',()=>{
  const world=new PlayerCollisionWorld(), oldEye=vec(-8,1.62,-.5), newEye=vec(100,1.62,.5);
  world.set('source cell at 0,0,0',[box([-6,0,-1],[-5,2,0])]);
  near(world.move(oldEye,vec(10,0,0)).x,1.7);
  world.set('source cell at 0,0,0',[box([102,0,0],[103,2,1])]);
  near(world.move(oldEye,vec(10,0,0)).x,10);
  near(world.move(newEye,vec(10,0,0)).x,1.7);
  world.set('source cell at 0,0,0',[]);
  near(world.move(newEye,vec(10,0,0)).x,10);
  world.set('again',[box([102,0,0],[103,2,1])]);world.clear();
  near(world.move(newEye,vec(10,0,0)).x,10);
});

test('overlap recovery handles compound solids, stays stable at contact and has a bounded fallback',()=>{
  const world=new PlayerCollisionWorld();
  world.set('compound',[box([0,0,0],[1,1,1]),box([0,1,0],[1,2,1])]);
  const eye=vec(.5,1.62,.5), recovered=world.freePosition(eye);
  assert.equal(world.intersects(recovered),false);
  near(recovered.distanceTo(eye),.8);
  assert.deepEqual(world.freePosition(recovered),recovered);
  // Three long tiled beams require more than 64 successive exits in every direction.
  for(let index=-200;index<200;index++)for(const axis of ['x','y','z']) {
    const segment=box([-2,-2,-2],[2,2,2]);
    segment.min[axis]=index;segment.max[axis]=index+1;world.set(`${axis}${index}`,[segment]);
  }
  const rescued=world.freePosition(vec(0,0,0));
  assert.equal(world.intersects(rescued),false);
  near(rescued.y,201.62);
  assert.ok(rescued.toArray().every(Number.isFinite));
});

test('body overlap checks allow face contact but detect feet and the head above the eye',()=>{
  const body=playerBounds(vec(.5,1.62,.5));
  assert.equal(overlaps(body,box([.2,1.7,.2],[.8,2,.8])),true);
  assert.equal(overlaps(body,box([.2,-1,.2],[.8,.1,.8])),true);
  assert.equal(overlaps(body,box([.2,-1,.2],[.8,0,.8])),false);
  assert.equal(overlaps(body,box([.8,0,0],[1.8,2,1])),false);
});

const floorWorld=()=>{
  const world=new PlayerCollisionWorld();world.set('floor',[box([-20,0,-20],[20,1,20])]);return world;
};
const advance=(movement,world,eye,frames,dt=1/60,input=vec(0,0,0))=>{
  for(let i=0;i<frames;i++)eye.add(movement.move(world,eye,input,dt,false));
  return eye;
};

test('collision toggle switches between gravity walking and unrestricted weightless flight',()=>{
  const world=floorWorld(), movement=new PlayerMovement(), eye=vec(0,5,0);
  assert.equal(movement.collisionEnabled,false);assert.equal(movement.flying,true);
  advance(movement,world,eye,60);near(eye.y,5);
  advance(movement,world,eye,60,1/60,vec(0,-1,0));assert.ok(eye.y<0,'disabled collision permits crossing the floor');
  eye.set(0,5,0);movement.setCollisionEnabled(true);
  assert.equal(movement.flying,false);
  advance(movement,world,eye,120);near(eye.y,2.62);
  movement.setCollisionEnabled(false);movement.setFlying(false);
  assert.equal(movement.flying,true,'no-collision mode cannot accidentally enable gravity');
});

test('gravity works without movement keys and yields consistent fall distance at common display rates',()=>{
  const results=[];
  for(const fps of [30,60,144]) {
    const movement=new PlayerMovement();movement.setCollisionEnabled(true);
    const eye=vec(0,50,0);advance(movement,floorWorld(),eye,fps,1/fps);results.push(eye.y);
  }
  for(const y of results)near(y,38);
});

test('single Space jumps only from support and held ascent or descent does not bypass walking physics',()=>{
  const world=floorWorld(), movement=new PlayerMovement();movement.setCollisionEnabled(true);
  const eye=vec(0,2.62,0);movement.pressSpace(0,false);
  advance(movement,world,eye,15,1/60,vec(0,1,0));assert.ok(eye.y>3.5);
  movement.pressSpace(500,false);
  advance(movement,world,eye,90,1/60,vec(0,1,0));near(eye.y,2.62);
  advance(movement,world,eye,60,1/60,vec(0,-1,0));near(eye.y,2.62);
});

test('two distinct Space presses toggle flight; repeats and input reset cannot create a double press',()=>{
  const movement=new PlayerMovement();movement.setCollisionEnabled(true);
  movement.pressSpace(10,false);movement.pressSpace(50,true);assert.equal(movement.flying,false);
  movement.pressSpace(100,false);assert.equal(movement.flying,true);
  movement.pressSpace(110,true);assert.equal(movement.flying,true);
  movement.pressSpace(500,false);movement.pressSpace(700,false);assert.equal(movement.flying,false);
  movement.pressSpace(1000,false);movement.clearInput();movement.pressSpace(1100,false);assert.equal(movement.flying,false);
});

test('enabled flight holds altitude and automatically returns to walking on landing',()=>{
  const world=floorWorld(), movement=new PlayerMovement();movement.setCollisionEnabled(true);movement.setFlying(true);
  const eye=vec(0,5,0);advance(movement,world,eye,60);near(eye.y,5);
  advance(movement,world,eye,60,1/60,vec(0,-1,0));near(eye.y,2.62);
  assert.equal(movement.flying,false);
  advance(movement,world,eye,30,1/60,vec(0,1,0));near(eye.y,2.62,'held ascent cannot continue flying after landing');
  movement.pressSpace(1000,false);advance(movement,world,eye,5);
  assert.equal(movement.flying,false,'a single Space only jumps after landing');
  movement.pressSpace(1100,false);advance(movement,world,eye,30,1/60,vec(0,1,0));
  assert.equal(movement.flying,true);assert.ok(eye.y>6,'double Space takes off again');
});

test('exact and idle foot contact ends flight but wall, ceiling and hovering do not',()=>{
  const world=floorWorld(), movement=new PlayerMovement();movement.setCollisionEnabled(true);
  movement.setFlying(true);
  let eye=vec(0,2.62+10.89*.05,0);
  eye.add(movement.move(world,eye,vec(0,-1,0),.05,false));near(eye.y,2.62);
  assert.equal(movement.flying,false,'exactly reaching a surface must count as landing');
  movement.setFlying(true);movement.move(world,eye,vec(0,0,0),.05,false);
  assert.equal(movement.flying,false,'standing on a block cannot remain in idle flight');
  movement.setFlying(true);movement.move(world,vec(0,2.63,0),vec(0,0,0),.05,false);
  assert.equal(movement.flying,true,'hovering above the block is not a landing');
  world.set('wall',[box([1,0,-2],[2,5,2])]);
  movement.move(world,vec(0,3,0),vec(1,0,0),.2,false);
  assert.equal(movement.flying,true,'side contact preserves flight');
  world.set('ceiling',[box([-2,4,-2],[2,5,2])]);
  movement.move(world,vec(0,3,0),vec(0,1,0),.2,false);
  assert.equal(movement.flying,true,'head contact preserves flight');
});

test('landing tests use final support so diagonal flight past a ledge remains airborne',()=>{
  const world=new PlayerCollisionWorld(), movement=new PlayerMovement();movement.setCollisionEnabled(true);movement.setFlying(true);
  world.set('platform',[box([0,0,0],[1,1,1])]);
  const eye=vec(.5,3,.5);eye.add(movement.move(world,eye,vec(1,-1,0),.2,false));
  assert.ok(eye.x>1.3,'horizontal movement leaves the platform');
  assert.equal(movement.flying,true,'no support beneath final feet means still airborne');
});

test('jumping hits a low ceiling and falling lands without tunneling at the terminal speed',()=>{
  const world=floorWorld(), movement=new PlayerMovement();movement.setCollisionEnabled(true);
  world.set('ceiling',[box([-2,3,-2],[2,4,2])]);
  const eye=vec(0,2.62,0);movement.pressSpace(0,false);
  for(let i=0;i<60;i++) { advance(movement,world,eye,1);assert.ok(eye.y<=2.82+1e-7); }
  near(eye.y,2.62);world.set('ceiling',[]);
  eye.y=200;advance(movement,world,eye,200,1/30);near(eye.y,2.62);
});

test('walking steps smoothly onto low devices and slabs without jumping or entering flight',()=>{
  for(const height of [.125,.375,.5,.6]) {
    const world=floorWorld(), movement=new PlayerMovement();movement.setCollisionEnabled(true);
    world.set('low step',[box([1,1,-2],[10,1+height,2])]);
    const eye=vec(0,2.62,0);
    for(let frame=0;frame<60;frame++) {
      advance(movement,world,eye,1,1/60,vec(1,0,0));
      assert.ok(eye.y<=2.62+height+1e-6,'low steps do not cause a full jump');
      assert.equal(world.intersects(eye),false);
    }
    near(eye.x,4.3);near(eye.y,2.62+height);assert.equal(movement.flying,false);
  }
});

test('walking automatically jumps onto one-block obstacles in all horizontal directions',()=>{
  for(const axis of ['x','z'])for(const sign of [-1,1]) {
    const world=floorWorld(), movement=new PlayerMovement();movement.setCollisionEnabled(true);
    const obstacle=box([-2,1,-2],[2,2,2]);obstacle.min[axis]=sign>0?1:-10;obstacle.max[axis]=sign>0?10:-1;
    world.set('step',[obstacle]);const eye=vec(0,2.62,0), input=vec(0,0,0);input[axis]=sign;
    let highest=eye.y;
    for(let frame=0;frame<90;frame++) {
      advance(movement,world,eye,1,1/60,input);highest=Math.max(highest,eye.y);
      assert.equal(world.intersects(eye),false,'the jump arc cannot enter the obstacle');
    }
    assert.ok(eye[axis]*sign>3,'walking continues onto the raised platform');
    near(eye.y,3.62);assert.ok(highest>3.7,'a full block uses a real jump arc');
    assert.equal(movement.flying,false,'automatic jumps never toggle flight');
  }
});

test('tall walls, blocked headroom and Shift suppress automatic jumping',()=>{
  for(const kind of ['wall','ceiling','shift']) {
    const world=floorWorld(), movement=new PlayerMovement();movement.setCollisionEnabled(true);
    world.set('obstacle',[box([1,1,-2],[5,kind==='wall'?3:2,2])]);
    if(kind==='ceiling')world.set('ceiling',[box([-2,2.9,-2],[5,4,2])]);
    const eye=vec(0,2.62,0);
    advance(movement,world,eye,120,1/60,vec(1,kind==='shift'?-1:0,0));
    near(eye.x,.7);near(eye.y,2.62);assert.equal(movement.flying,false);
  }
});

test('low steps also respect headroom and unsupported players cannot step or auto-jump in midair',()=>{
  const world=floorWorld(), movement=new PlayerMovement();movement.setCollisionEnabled(true);
  world.set('low step',[box([1,1,-2],[5,1.125,2])]);
  world.set('ceiling',[box([-2,2.9,-2],[5,4,2])]);
  const eye=vec(0,2.62,0);advance(movement,world,eye,60,1/60,vec(1,0,0));
  near(eye.x,.7);near(eye.y,2.62);
  world.set('ceiling',[]);world.set('low step',[box([1,1,-2],[5,2,2])]);
  eye.set(.7,2.82,0);advance(movement,world,eye,6,1/60,vec(1,0,0));
  assert.ok(eye.y<2.82&&eye.y>2.62,'airborne movement continues falling instead of starting another jump');
  near(eye.x,.7);
});

test('automatic jumping leaves collision flight and no-collision movement unchanged',()=>{
  const world=floorWorld();world.set('step',[box([1,1,-2],[5,2,2])]);
  const movement=new PlayerMovement();movement.setCollisionEnabled(true);movement.setFlying(true);
  const eye=vec(0,3,0);advance(movement,world,eye,60,1/60,vec(1,0,0));
  near(eye.x,.7);near(eye.y,3);assert.equal(movement.flying,true);
  movement.setCollisionEnabled(false);eye.set(0,2.62,0);
  advance(movement,world,eye,30,1/60,vec(1,0,0));
  assert.ok(eye.x>5);near(eye.y,2.62);
});
