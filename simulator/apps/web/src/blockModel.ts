import * as THREE from 'three';
import type { BlockCell, BlockDef, Pos } from './api';
import { shortName } from './blockLabels';
import { DeviceTextures } from './deviceTextures';
import { blocksPlayer } from './playerCollision';

export type Handle = { pool: InstancePool; index: number };
export const box = new THREE.BoxGeometry(1, 1, 1);
export const cylinder = new THREE.CylinderGeometry(.5, .5, 1, 8);
const face = new THREE.PlaneGeometry(1, 1);
const color = new THREE.Color();
const matrix = new THREE.Matrix4();
const quaternion = new THREE.Quaternion();
const vector = new THREE.Vector3();
const scaleVector = new THREE.Vector3();
export const material = new THREE.MeshStandardMaterial({ roughness: .84, metalness: .08 });
export const directionVectors: Record<string, [number, number, number]> = { east: [1,0,0], west: [-1,0,0], up: [0,1,0], down: [0,-1,0], north: [0,0,-1], south: [0,0,1] };
export class InstancePool {
  mesh: THREE.InstancedMesh;
  positions: Array<Pos | undefined> = [];
  private free: number[] = [];
  private count = 0;
  private capacity = 32;
  constructor(readonly group: THREE.Group, readonly geometry: THREE.BufferGeometry, readonly visible = true, readonly poolMaterial: THREE.Material = material) { this.mesh = this.create(); }
  private create() {
    const mesh = new THREE.InstancedMesh(this.geometry, this.poolMaterial, this.capacity); mesh.count = this.count;
    mesh.visible = this.visible;
    mesh.instanceMatrix.setUsage(THREE.DynamicDrawUsage); mesh.castShadow = false; mesh.receiveShadow = true;
    mesh.boundingSphere = new THREE.Sphere(new THREE.Vector3(8,8,8), 15); mesh.userData.pool = this; this.group.add(mesh); return mesh;
  }
  add(pos: Pos, transform: THREE.Matrix4, tint: number): Handle {
    let index = this.free.pop(); if (index === undefined) index = this.count++;
    if (index >= this.capacity) {
      const old = this.mesh; this.capacity *= 2; this.mesh = this.create();
      for (let i = 0; i < old.count; ++i) { old.getMatrixAt(i, matrix); this.mesh.setMatrixAt(i, matrix); old.getColorAt(i, color); this.mesh.setColorAt(i, color); }
      this.group.remove(old); old.dispose();
    }
    this.positions[index] = pos; this.mesh.count = this.count; this.mesh.setMatrixAt(index, transform); this.mesh.setColorAt(index, color.setHex(tint)); this.dirty(); return { pool: this, index };
  }
  remove(index: number) { this.positions[index] = undefined; this.free.push(index); this.mesh.setMatrixAt(index, new THREE.Matrix4().makeScale(0,0,0)); this.dirty(); }
  private dirty() { this.mesh.instanceMatrix.needsUpdate = true; if (this.mesh.instanceColor) this.mesh.instanceColor.needsUpdate = true; }
  dispose() { this.group.remove(this.mesh); this.mesh.dispose(); }
}
export type Chunk = { group: THREE.Group; box: InstancePool; cylinder: InstancePool; pick?: InstancePool; surfaces?: Map<string, InstancePool> };

// Shared by the world, placement preview and static inventory thumbnails.
export function drawBlock(cell: BlockCell, def: BlockDef, chunk: Chunk, deviceTextures: DeviceTextures, preview = false, collisions?: THREE.Box3[]): Handle[] {
    const name = shortName(def.name), p = def.properties, handles: Handle[] = [];
    const collidable = collisions && blocksPlayer(def.name,p);
    const collisionHeight = name === 'repeater' || name === 'comparator' ? 1/8 : name === 'daylight_detector' ? 3/8
      : name === 'sculk_sensor' || name === 'calibrated_sculk_sensor' ? 1/2 : undefined;
    let x = cell.pos[0] - chunk.group.position.x, y = cell.pos[1] - chunk.group.position.y, z = cell.pos[2] - chunk.group.position.z;
    const moving = (cell.motion & 1) !== 0, extending = (cell.motion & 2) !== 0, source = (cell.motion & 4) !== 0, progress = ((cell.motion >> 6) & 3) / 2;
    const motionDir = directionVectors[['down','up','north','south','west','east'][(cell.motion >> 3) & 7]] ?? [0,0,0];
    if (moving && !(source && !extending)) { const offset = extending ? progress - 1 : 1 - progress; x += motionDir[0] * offset; y += motionDir[1] * offset; z += motionDir[2] * offset; }
    // Low devices collide only with their base; torches, rods, antennae and crystals remain selectable decorations.
    if(collidable && collisionHeight!==undefined)collisions.push(new THREE.Box3(new THREE.Vector3(x,y,z),new THREE.Vector3(x+1,y+collisionHeight,z+1)).translate(chunk.group.position));
    const transform = new THREE.Matrix4();
    const part = (center: [number,number,number], size: [number,number,number], tint: number, shape: 'box'|'cylinder' = 'box', rotation: number | THREE.Quaternion = 0) => {
      if (typeof rotation === 'number') quaternion.setFromAxisAngle(vector.set(0,1,0), rotation); else quaternion.copy(rotation);
      transform.compose(vector.set(x + center[0], y + center[1], z + center[2]), quaternion, scaleVector.fromArray(size)); handles.push(chunk[shape].add(cell.pos, transform, tint));
      if(collidable && collisionHeight===undefined)collisions.push(new THREE.Box3(new THREE.Vector3(-.5,-.5,-.5),new THREE.Vector3(.5,.5,.5)).applyMatrix4(transform).translate(chunk.group.position));
    };
    const surface = (key: string, center: THREE.Vector3, size: [number,number], rotation: THREE.Quaternion) => {
      const surfaces = chunk.surfaces ??= new Map<string, InstancePool>();
      let pool = surfaces.get(key);
      if (!pool) { pool = new InstancePool(chunk.group, face, true, deviceTextures.get(key, preview)); surfaces.set(key,pool); }
      transform.compose(vector.set(x+center.x,y+center.y,z+center.z),rotation,scaleVector.set(size[0],size[1],1));
      handles.push(pool.add(cell.pos,transform,0xffffff));
    };
    // Each face is instanced by texture and chunk; state updates replace only
    // this cell's handles. The same surfaces are used by the placement preview.
    const topSurface = (key: string, height: number, width: number, rotation = 0) => surface(key,new THREE.Vector3(.5,height,.5),[width,width],new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(0,1,0),rotation).multiply(new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(1,0,0),-Math.PI/2)));
    const cubeSurfaces = (keys: string[], orientation = new THREE.Quaternion(), width = .982, height = .982, depth = .982, center = new THREE.Vector3(.5,.5,.5)) => {
      const normals = [[0,0,-1],[0,0,1],[-1,0,0],[1,0,0],[0,1,0],[0,-1,0]];
      normals.forEach((n,i) => {
        if (!keys[i]) return;
        const normal = new THREE.Vector3(...n);
        const offset = normal.clone().multiply(new THREE.Vector3(width,height,depth)).multiplyScalar(.5).applyQuaternion(orientation).add(center);
        const rotation = orientation.clone().multiply(new THREE.Quaternion().setFromUnitVectors(new THREE.Vector3(0,0,1),normal));
        const turn = i===2?Math.PI/2:i===3?-Math.PI/2:i===5?Math.PI:0;
        rotation.multiply(new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(0,0,1),turn));
        surface(keys[i],offset,n[0]?[height,depth]:[width,n[1]?depth:height],rotation);
      });
    };
    const on = p.lit === 'true' || cell.value > 0, red = on ? new THREE.Color().setRGB(.28 + cell.value / 22,.035 + cell.value / 110,.025).getHex() : 0x4d2728;
    const facing = p.facing ?? 'north', dir = directionVectors[facing], angle = Math.atan2(dir[0], dir[2]);
    const torch = (cx: number, cz: number, lit: boolean, height = .5, base = 0, rotation = new THREE.Quaternion()) => {
      const center = (h: number) => new THREE.Vector3(0,h-base,0).applyQuaternion(rotation).add(new THREE.Vector3(cx,base,cz));
      part(center((base+height)/2).toArray(),[.125,height-base,.125],0x9c7651,'box',rotation);
      part(center(height-.0625).toArray(),[.126,.125,.126],lit ? 0xff5541 : 0x713637,'box',rotation);
      cubeSurfaces(Array(6).fill(`torch:${Number(lit)}`),rotation,.128,.127,.128,center(height-.0625));
    };
    if (name === 'redstone_wire') {
      part([.5,.024,.5],[.22,.042,.22],red);
      for (const d of ['north','east','south','west']) if (p[d] !== 'none') { const v = directionVectors[d]; part([.5 + v[0] * .27,.023,.5 + v[2] * .27], v[0] ? [.54,.04,.095] : [.095,.04,.54],red); if (p[d] === 'up') part([.5 + v[0] * .49,.5,.5 + v[2] * .49],v[0] ? [.045,1,.095] : [.095,1,.045],red); }
      topSurface(`wire:${Math.max(0,Math.min(15,Number(p.power ?? cell.value)))}`,.048,.38);
    } else if (name === 'repeater' || name === 'comparator') {
      const powered = p.powered === 'true', delay = Math.max(1,Math.min(4,Number(p.delay) || 1)), subtract = p.mode === 'subtract';
      part([.5,.0625,.5],[1,.125,1],0xa8ada5);
      topSurface(`${name}:${Number(powered)}:${name==='repeater'?delay:Number(subtract)}`,.126,1,angle);
      const v = directionVectors[facing];
      if (name === 'repeater') {
        // State facing points toward the input. The fixed output torch is
        // -5/16; the movable input torch advances from -1/16 to +5/16.
        torch(.5-v[0]*5/16,.5-v[2]*5/16,powered,7/16,2/16);
        const offset = (2*delay-3)/16;
        if (p.locked === 'true') part([.5+v[0]*offset,3/16,.5+v[2]*offset],[12/16,2/16,2/16],0x343b40,'box',angle);
        else torch(.5+v[0]*offset,.5+v[2]*offset,powered,7/16,2/16);
      } else {
        torch(.5-v[0]*5/16,.5-v[2]*5/16,subtract,subtract?5/16:4/16,2/16);
        for(const sign of [-1,1])torch(.5+v[0]*4/16+v[2]*sign*3/16,.5+v[2]*4/16-v[0]*sign*3/16,powered,7/16,2/16);
      }
    } else if (name === 'redstone_torch') torch(.5,.5,on,.62);
    else if (name === 'redstone_wall_torch') {
      const tilt = Math.PI/8;
      const rotation = new THREE.Quaternion().setFromUnitVectors(new THREE.Vector3(0,1,0),new THREE.Vector3(dir[0]*Math.sin(tilt),Math.cos(tilt),dir[2]*Math.sin(tilt)));
      torch(.5-dir[0]*.47,.5-dir[2]*.47,on,.82,.2,rotation);
    }
    else if (name === 'lever') { part([.5,.09,.5],[.46,.18,.46],0x747d7d); part([on ? .65 : .35,.36,.5],[.12,.5,.12],0xc4ad80); part([on ? .65 : .35,.6,.5],[.18,.09,.18],on ? 0xee6550 : 0x80776b); }
    else if (name.endsWith('_button')) part([.5,on ? .035 : .08,.5],[.35,on ? .07 : .16,.5],name.includes('stone') ? 0x9b9f95 : 0xba8c58);
    else if (name === 'redstone_lamp') {
      part([.5,.5,.5],[.98,.98,.98],on ? 0xffcf82 : 0x5c4938);
      cubeSurfaces(Array(6).fill(`lamp:${Number(p.lit==='true')}`));
    } else if (name.endsWith('copper_bulb')) {
      const oxidized = name.includes('oxidized') || name.includes('weathered'); part([.5,.5,.5],[.98,.98,.98],oxidized ? 0x527b65 : 0xac7151);
      for (const v of [.25,.5,.75]) { part([v,.996,.5],[.11,.014,.66],on ? 0xffda8c : 0x3f4944); part([.996,.5,v],[.014,.66,.11],on ? 0xffda8c : 0x3f4944); part([v,.5,.996],[.11,.66,.014],on ? 0xffda8c : 0x3f4944); }
    } else if (name === 'observer') {
      part([.5,.5,.5],[.98,.98,.98],0x818b8c);
      const state = Number(p.powered==='true'), orientation = new THREE.Quaternion().setFromUnitVectors(new THREE.Vector3(0,0,-1),new THREE.Vector3(...dir));
      cubeSurfaces([`observerFront:${state}`,`observerBack:${state}`,...Array(4).fill(`observerSide:${state}`)],orientation);
    } else if (name === 'piston' || name === 'sticky_piston' || name === 'piston_head') {
      const v = directionVectors[facing], sticky = name === 'sticky_piston' || p.type === 'sticky';
      const pistonPart = (offset: number, length: number, width: number, tint: number) => part([.5+v[0]*offset,.5+v[1]*offset,.5+v[2]*offset],[v[0]?length:width,v[1]?length:width,v[2]?length:width],tint);
      const orientation = new THREE.Quaternion().setFromUnitVectors(new THREE.Vector3(0,0,-1),new THREE.Vector3(...v));
      const headSurface = (offset: number) => surface(`pistonHead:${Number(sticky)}`,new THREE.Vector3(.5+v[0]*offset,.5+v[1]*offset,.5+v[2]*offset),[.99,.99],orientation.clone().multiply(new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(0,1,0),Math.PI)));
      if (name === 'piston_head') { pistonPart(.37,.25,.99,sticky?0x8da96a:0xbb9f70); pistonPart(-.12,.75,.22,0xa8a38c); }
      else {
        const open = p.extended === 'true' || (moving && source); pistonPart(open?-.125:0,open?.75:.99,.99,0x727d7a);
        if (!open || (moving && source && !extending)) { const headOffset = moving ? 1-progress : 0; pistonPart(.37+headOffset,.25,.99,sticky?0x8da96a:0xbb9f70); if(moving)pistonPart(headOffset*.5,.8,.22,0xaaa58e); }
        const center = new THREE.Vector3(.5,.5,.5).addScaledVector(new THREE.Vector3(...v),open?-.125:0);
        cubeSurfaces(['','',...Array(4).fill(`pistonSide:${Number(sticky)}`)],orientation,.992,.992,open?.752:.992,center);
        if(!open || (moving && source && !extending))headSurface(.497+(moving?1-progress:0));
      }
      if(name==='piston_head')headSurface(.497);
    } else if (name === 'tripwire') {
      const attached = p.attached === 'true';
      // Original wire selection volume covers the cell, not only its thin
      // rendered thread. Keep pick geometry invisible and pooled by chunk.
      const pick = chunk.pick ??= new InstancePool(chunk.group, box, false);
      transform.compose(vector.set(x+.5,y+(attached ? .109375 : .25),z+.5),quaternion.identity(),scaleVector.set(1,attached ? .09375 : .5,1));
      handles.push(pick.add(cell.pos,transform,0));
      const shade = p.disarmed === 'true' ? 0x8e9390 : p.powered === 'true' ? 0xd97743 : 0x596e74;
      const height = attached ? .09 : .035;
      part([.5,height,.5],[.09,.03,.09],shade);
      for (const direction of ['north','south','east','west']) if (p[direction] === 'true') {
        const v = directionVectors[direction];
        part([.5+v[0]*.25,height,.5+v[2]*.25],[v[0] ? .5 : .045,.035,v[2] ? .5 : .045],shade);
      }
    } else if (name === 'tripwire_hook') {
      const v = directionVectors[facing], attached = p.attached === 'true';
      const shade = p.powered === 'true' ? 0xe6ad69 : 0xb4beb9;
      part([.5-v[0]*.455,.28,.5-v[2]*.455],[v[0] ? .09 : .32,.5,v[2] ? .09 : .32],0x927c5b);
      const height = attached ? .11 : .29;
      part([.5-v[0]*.25,height,.5-v[2]*.25],[v[0] ? .42 : .055,.055,v[2] ? .42 : .055],shade);
      for (const side of [-1,1]) part([.5-v[0]*.065+v[2]*side*.07,height,.5-v[2]*.065-v[0]*side*.07],[v[0] ? .15 : .035,.045,v[2] ? .15 : .035],shade);
      part([.5+v[0]*.01,height,.5+v[2]*.01],[v[0] ? .035 : .17,.045,v[2] ? .035 : .17],shade);
      if (attached) part([.5+v[0]*.25,.09,.5+v[2]*.25],[v[0] ? .5 : .026,.026,v[2] ? .5 : .026],shade);
    } else if (name === 'rail' || name.endsWith('_rail')) {
      const shape = p.shape ?? 'north_south';
      const powered = p.powered === 'true';
      const tint = name === 'powered_rail' ? (powered ? 0xf2b365 : 0xaa935d) : name === 'activator_rail' ? (powered ? 0xe87f62 : 0x875953) : 0xa6b0ac;
      const railLine = (from: [number,number,number], to: [number,number,number], shade: number, width = .065, height = .05) => {
        const delta = new THREE.Vector3(to[0]-from[0], to[1]-from[1], to[2]-from[2]);
        const length = delta.length(); quaternion.setFromUnitVectors(new THREE.Vector3(0,0,1), delta.normalize());
        transform.compose(vector.set(x+(from[0]+to[0])/2,y+(from[1]+to[1])/2,z+(from[2]+to[2])/2),quaternion,scaleVector.set(width,height,length));
        handles.push(chunk.box.add(cell.pos,transform,shade));
      };
      const corner: Record<string,[number,number,number]> = {south_east:[1,1,Math.PI],south_west:[0,1,-Math.PI/2],north_west:[0,0,0],north_east:[1,0,Math.PI/2]};
      if (corner[shape]) {
        const [cx,cz,start] = corner[shape];
        const point = (angle: number, radius: number, h: number): [number,number,number] => [cx+Math.cos(angle)*radius,h,cz+Math.sin(angle)*radius];
        for (let i=0;i<4;++i) {
          const a=start+i*Math.PI/8, b=a+Math.PI/8;
          for (const radius of [.28,.72]) railLine(point(a,radius,.085),point(b,radius,.085),tint);
          railLine(point((a+b)/2,.17,.03),point((a+b)/2,.83,.03),0x7c6b51,.13,.04);
        }
      } else {
        const eastWest = shape === 'east_west' || shape === 'ascending_east' || shape === 'ascending_west';
        const coordinate = (along: number, across: number, h: number): [number,number,number] => {
          const rise = shape === 'ascending_east' || shape === 'ascending_south' ? along : shape.startsWith('ascending_') ? 1-along : 0;
          return eastWest ? [along,h+rise,across] : [across,h+rise,along];
        };
        for (const across of [.28,.72]) railLine(coordinate(0,across,.085),coordinate(1,across,.085),tint);
        for (const along of [.15,.5,.85]) railLine(coordinate(along,.12,.03),coordinate(along,.88,.03),0x7c6b51,.13,.04);
        if (name === 'detector_rail') railLine(coordinate(.2,.5,.1),coordinate(.8,.5,.1),powered ? 0xdf9774 : 0x9b8980,.34,.04);
      }
      if ((cell.motion & 2048) !== 0) {
        part([.5,.24,.5],[.8,.16,.8],0x465155);
        for (const edge of [.14,.86]) { part([edge,.48,.5],[.12,.38,.84],0x9ba7a8); part([.5,.48,edge],[.6,.38,.12],0x9ba7a8); }
      }
    } else if(name==='composter') {
      part([.5,.06,.5],[1,.12,1],0x73523b);
      for(const edge of [.06,.94]) {part([edge,.56,.5],[.12,.88,1],0x997049);part([.5,.56,edge],[.76,.88,.12],0x997049);}
      const level=Number(p.level),height=Math.max(2,1+Math.min(level,7)*2)/16;
      part([.5,height/2,.5],[.76,height,.76],level===8?0xa99b6d:0x624531);
      if(level===8)for(const x of [.32,.65])part([x,height+.015,.48],[.12,.025,.2],0xe3d6a1);
    } else if(name==='jukebox') {
      part([.5,.5,.5],[1,1,1],0x6b503c);part([.5,1.005,.5],[.86,.02,.86],0xa78257);
      part([.5,1.019,.5],[.65,.015,.12],0x2c3030);
      if(p.has_record==='true')part([.5,1.045,.5],[.22,.04,.2],0x76b8aa);
    } else if(name==='bell') {
      const ringing=(cell.motion&4096)!==0,attachment=p.attachment,v=directionVectors[facing];
      part([.5,.57,.5],[.38,.44,.38],ringing?0xf7d47c:0xc7a45d);
      part([.5,.3,.5],[.5,.13,.5],ringing?0xffdfa0:0xd9b56d);
      part([.5,.22,.5],[.12,.18,.12],0x74603a);
      if(attachment==='ceiling')part([.5,.9,.5],[.125,.2,.125],0x6a6862);
      else if(attachment==='floor') {
        for(const side of [-1,1])part([.5+(v[0]?0:side*.44),.48,.5+(v[2]?0:side*.44)],[.12,.96,.12],0x66513c);
        part([.5,.95,.5],[v[0]?.13:1,.12,v[2]?.13:1],0x66513c);
      }else {
        const double=attachment==='double_wall';part([.5+(double?0:v[0]*.2),.88,.5+(double?0:v[2]*.2)],[v[0]?(double?1:.6):.125,.125,v[2]?(double?1:.6):.125],0x66513c);
      }
    } else if (name === 'note_block') {
      part([.5,.5,.5],[1,1,1],0x79523b);
      part([.5,1.006,.5],[.88,.015,.88],p.powered==='true'?0xd4a675:0xad8056);
      for(const line of [.2,.4,.6,.8])part([line,1.018,.5],[.055,.015,.78],0x533d2d);
    } else if (name.endsWith('_head') || name.endsWith('_skull')) {
      const wall=name.includes('_wall_'),v=directionVectors[facing];
      const headColor=name.startsWith('zombie')?0x73965c:name.startsWith('creeper')?0x78a251:name.startsWith('dragon')||name.startsWith('wither')?0x393c40:0xc5b598;
      const cx=wall?.5-v[0]*.25:.5,cy=wall?.75:.25,cz=wall?.5-v[2]*.25:.5;
      part([cx,cy,cz],[.5,.5,.5],headColor);
      for(const eye of [-.12,.12])part([cx+(v[0]===0?eye:v[0]*.26),cy+.04,cz+(v[2]===0?eye:v[2]*.26)],[v[0]===0?.11:.025,.09,v[2]===0?.11:.025],0x30373a);
    } else if (name === 'sculk_sensor' || name === 'calibrated_sculk_sensor') {
      const active=p.sculk_sensor_phase==='active',cooling=p.sculk_sensor_phase==='cooldown';
      part([.5,.25,.5],[1,.5,1],0x224b50);
      part([.5,.48,.5],[.82,.06,.82],0x427478);
      for(const a of [.18,.82])for(const b of [.18,.82]) {
        part([a,.66,b],[.09,.4,.09],active?0xefd28d:cooling?0x557576:0x69c8ba);
        part([a,.86,b],[.18,.09,.18],active?0xf5b968:0x83d5c7);
      }
      if(name==='calibrated_sculk_sensor') {
        const back=directionVectors[facing];
        part([.5-back[0]*.35,.38,.5-back[2]*.35],[back[0]?.2:.45,.5,back[2]?.2:.45],0x9c86b7);
        part([.5,.7,.5],[.26,.4,.26],0xc1abe3,'box',Math.PI/4);
      }
    } else if (name.endsWith('_carpet')) {
      part([.5,.03125,.5],[1,.0625,1],0xd1cdbd);
    } else if (name === 'decorated_pot') {
      part([.5,.43,.5],[.875,.86,.875],0x9a5d42);
      part([.5,.89,.5],[.49,.12,.49],0xa96d4c);
      for(const edge of [.23,.77]) {
        part([edge,.98,.5],[.06,.04,.6],0xbd8258);
        part([.5,.98,edge],[.48,.04,.06],0xbd8258);
      }
      part([.5,.95,.5],[.48,.012,.48],0x342f29);
    } else if (name === 'chiseled_bookshelf') {
      const v=directionVectors[facing];part([.5,.5,.5],[.98,.98,.98],0xa18c65);
      for(let slot=0;slot<6;++slot) {
        const side=(slot%3-1)*.29,height=slot<3 ? .73 : .27;
        const center: [number,number,number]=[.5+v[0]*.5+v[2]*side,height,.5+v[2]*.5-v[0]*side];
        part(center,[v[0] ? .014 : .24,.35,v[2] ? .014 : .24],0x4c4a3e);
        if(p[`slot_${slot}_occupied`]==='true') {
          center[0]+=v[0]*.009;center[2]+=v[2]*.009;
          part(center,[v[0] ? .02 : .16,.29,v[2] ? .02 : .16],[0x855e51,0x819073,0x8b718b][slot%3]);
        }
      }
    } else if (name === 'dropper') {
      const v = directionVectors[facing];
      part([.5,.5,.5],[.99,.99,.99],0x778184);
      part([.5+v[0]*.498,.5+v[1]*.498,.5+v[2]*.498],[v[0] ? .012 : .79,v[1] ? .012 : .79,v[2] ? .012 : .79],0xa4adac);
      part([.5+v[0]*.506,.5+v[1]*.506,.5+v[2]*.506],[v[0] ? .015 : .3,v[1] ? .015 : .3,v[2] ? .015 : .3],0x293236);
    } else if (name === 'dispenser') {
      part([.5,.5,.5],[.99,.99,.99],0x778184);
      const orientation = new THREE.Quaternion().setFromUnitVectors(new THREE.Vector3(0,0,-1),new THREE.Vector3(...dir));
      cubeSurfaces(['dispenserFront','','','','',''],orientation,.992,.992,.992);
    } else if (name === 'hopper') {
      const tint = p.enabled === 'false' ? 0x596369 : 0x788487;
      for (const edge of [.0625,.9375]) { part([edge,.8,.5],[.125,.4,1],tint); part([.5,.8,edge],[.75,.4,.125],tint); }
      part([.5,.51,.5],[.75,.18,.75],tint); part([.5,.3,.5],[.42,.3,.42],tint);
      const v = directionVectors[facing];
      part([.5+v[0]*.28,.19+v[1]*.08,.5+v[2]*.28],[v[0] ? .56 : .25,v[1] ? .38 : .25,v[2] ? .56 : .25],tint);
      part([.5,.605,.5],[.73,.012,.73],0x283135);
    } else if (name.endsWith('chest')) {
      const v = directionVectors[facing], opened = (cell.motion & 1024) !== 0;
      const width = p.type !== 'single' ? 1 : .875;
      const sizeX=v[0] ? .875 : width,sizeZ=v[2] ? .875 : width;
      const copper=name.includes('copper_chest');
      const tint=copper?(name.includes('oxidized')?0x599885:name.includes('weathered')?0x69937a:name.includes('exposed')?0xa58d6e:0xb27755):name==='trapped_chest'?0x9a6446:0xab8550;
      const lid=copper?tint:0xc1995d;
      part([.5,.36,.5],[sizeX,.6,sizeZ],tint);
      if (opened) part([.5-v[0]*.34,1.04,.5-v[2]*.34],[v[0] ? .25 : sizeX,.78,v[2] ? .25 : sizeZ],lid);
      else part([.5,.8,.5],[sizeX,.27,sizeZ],lid);
      part([.5+v[0]*.45,.55,.5+v[2]*.45],[v[0] ? .06 : .13,.24,v[2] ? .06 : .13],name === 'trapped_chest' ? 0xc87f60 : 0xc8c3a1);
    } else if (name === 'barrel') {
      part([.5,.5,.5],[1,1,1],0xa18559);
      for (const height of [.15,.85]) { part([.5,height,.005],[1,.1,.02],0x4e5957); part([.5,height,.995],[1,.1,.02],0x4e5957); part([.005,height,.5],[.02,.1,1],0x4e5957); part([.995,height,.5],[.02,.1,1],0x4e5957); }
      const v = directionVectors[facing]; part([.5+v[0]*.505,.5+v[1]*.505,.5+v[2]*.505],[v[0] ? .02 : .7,v[1] ? .02 : .7,v[2] ? .02 : .7],p.open === 'true' ? 0x2a302c : 0xbba071);
    } else if (name.endsWith('_door') || name.endsWith('_trapdoor')) {
      const trap = name.endsWith('_trapdoor'), opened = p.open === 'true';
      const tint = name.includes('iron') ? 0xc1c7c2 : name.includes('copper') ? 0xb27a5b : 0xb39b69;
      if (trap && !opened) part([.5,p.half === 'top' ? .90625 : .09375,.5],[1,.1875,1],tint);
      else {
        const directions = ['north','east','south','west'];
        const side = directions[(directions.indexOf(facing) + (!trap && opened ? p.hinge === 'right' ? 3 : 1 : 0)) % 4];
        const v = directionVectors[side], cx = .5-v[0]*.40625, cz = .5-v[2]*.40625;
        part([cx,.5,cz],[v[0] ? .1875 : 1,1,v[2] ? .1875 : 1],tint);
        part([cx+v[2]*.28,.5,cz-v[0]*.28],[v[0] ? .2 : .09,.09,v[2] ? .2 : .09],0x484b40);
      }
    } else if (name.endsWith('_fence_gate')) {
      const v = directionVectors[facing], tint = 0xb39b69;
      for (const sign of [-1,1]) {
        part([.5+v[2]*.42*sign,.5,.5+v[0]*.42*sign],[.15,1,.15],tint);
        for (const height of [.35,.75]) {
          if (p.open === 'true') part([.5+v[2]*.4*sign+v[0]*.2,height,.5+v[0]*.4*sign+v[2]*.2],v[0] ? [.55,.13,.12] : [.12,.13,.55],tint);
          else part([.5+v[2]*.2*sign,height,.5+v[0]*.2*sign],v[0] ? [.12,.13,.4] : [.4,.13,.12],tint);
        }
      }
    } else if (name.endsWith('pressure_plate')) {
      const height = cell.value > 0 ? .03125 : .0625;
      const tint = name.startsWith('light_weighted') ? 0xd2b362 : name.startsWith('heavy_weighted') ? 0xb8c2c5 : name.includes('stone') ? 0x929d99 : 0xb4996a;
      part([.5,height/2,.5],[.875,height,.875],tint);
    } else if (name.endsWith('lightning_rod')) {
      const v = directionVectors[facing], tint = name.includes('oxidized') ? 0x679583 : 0xbb805d;
      part([.5-v[0]*.05,.5-v[1]*.05,.5-v[2]*.05],[v[0] ? .9 : .12,v[1] ? .9 : .12,v[2] ? .9 : .12],tint);
      part([.5+v[0]*.34,.5+v[1]*.34,.5+v[2]*.34],[.26,.26,.26],cell.value ? 0xffd292 : tint);
    } else if (name === 'daylight_detector') {
      part([.5,.1875,.5],[1,.375,1],0xa58a62);
      for (const dx of [.18,.5,.82]) for (const dz of [.18,.5,.82]) part([dx,.378,dz],[.27,.012,.27],p.inverted === 'true' ? 0x567497 : 0xc1c7b3);
    } else if (name === 'lectern') {
      part([.5,.06,.5],[1,.12,1],0xa48a5a); part([.5,.42,.5],[.4,.72,.4],0x9c7f51); part([.5,.86,.5],[.96,.25,.75],0xb5a071);
      if (p.has_book === 'true') { part([.5,1.01,.5],[.65,.09,.6],0x715844); part([.5,1.065,.5],[.59,.03,.56],0xe6dfbc); }
    } else if (name.endsWith('cauldron')) {
      part([.5,.2,.5],[1,.4,1],0x576567);
      for (const v of [.06,.94]) { part([v,.67,.5],[.12,.66,1],0x6f7c7d); part([.5,.67,v],[1,.66,.12],0x6f7c7d); }
      if (name !== 'cauldron') part([.5,.35+Number(p.level ?? 3)*.19,.5],[.76,.04,.76],name.startsWith('lava') ? 0xd07c3a : name.startsWith('water') ? 0x518baa : 0xdce5e0);
    } else if (name.endsWith('copper_golem_statue')) {
      const tint = name.includes('oxidized') ? 0x6a9f85 : 0xbc895e;
      part([.5,.67,.5],[.72,.6,.6],tint); part([.5,.24,.5],[.38,.38,.35],tint);
      for (const dx of [.21,.79]) part([dx,.42,.5],[.2,.38,.25],tint);
      part([.35,.73,.806],[.08,.09,.02],0x303d34); part([.65,.73,.806],[.08,.09,.02],0x303d34);
    } else if (name === 'target') {
      part([.5,.5,.5],[.98,.98,.98],0xd5cbb4);
      for (const v of Object.values(directionVectors)) for (const size of [.7,.38,.12]) {
        const offset=.494+.001/size;
        part([.5+v[0]*offset,.5+v[1]*offset,.5+v[2]*offset],[v[0] ? .005 : size,v[1] ? .005 : size,v[2] ? .005 : size],size===.38?0xd5cbb4:0xa94a3f);
      }
    }
    else {
      const colors: Record<string, number> = { stone:0x747c80,smooth_stone:0xa3aaa8,white_concrete:0xc5c8bb,light_gray_concrete:0x93978c,red_concrete:0x975347,blue_concrete:0x516f96,white_wool:0xd2cfc0,redstone_block:0xb2372a,glass:0x719d9e,slime_block:0x84b85f,honey_block:0xb99b42,obsidian:0x353041,bedrock:0x474b50,glowstone:0xb8a673,sea_lantern:0xb6cebe };
      const height = name.endsWith('_slab') && p.type !== 'double' ? .5 : 1; part([.5,(p.type === 'top' ? .5 : 0)+height / 2,.5],[.99,height*.99,.99],colors[name] ?? 0x9b9c89);
    }
    return handles;
  }
