import { useEffect, useState } from 'react';
import * as THREE from 'three';
import { connection } from './api';
import { shortName } from './blockLabels';
import { catalogDefaults } from './interactionState';
import { box, cylinder, drawBlock, InstancePool, type Chunk, type Handle } from './blockModel';
import { DeviceTextures } from './deviceTextures';
import './blockThumbnail.css';

type Job = { name: string; properties: Record<string,string>; resolve: (url: string) => void; reject: (error: unknown) => void };
const thumbnails = new Map<string, Promise<string>>();
const jobs: Job[] = [];
let scheduled = false;

// One offscreen context for a batch, never one WebGL canvas per item. The UI
// receives cached PNGs; it has no animation loop or live simulation subscription.
class ThumbnailRenderer {
  private renderer = new THREE.WebGLRenderer({ alpha: true, antialias: true });
  private scene = new THREE.Scene();
  private camera = new THREE.OrthographicCamera(-1,1,1,-1,.01,100);
  private textures = new DeviceTextures([]);
  private material = new THREE.MeshStandardMaterial({ roughness: .84, metalness: .08 });
  private group = new THREE.Group();
  private chunk: Chunk = { group:this.group, box:new InstancePool(this.group,box,true,this.material), cylinder:new InstancePool(this.group,cylinder,true,this.material) };
  private handles: Handle[] = [];

  constructor() {
    this.renderer.setSize(96,96,false); this.renderer.setPixelRatio(1);
    this.renderer.setClearColor(0,0); this.renderer.outputColorSpace = THREE.SRGBColorSpace;
    this.scene.add(this.group,new THREE.HemisphereLight(0xf2f2e4,0x4c5b69,2.7));
    const light = new THREE.DirectionalLight(0xffefcf,3.1);light.position.set(-12,30,15);this.scene.add(light);
  }

  render(name: string, properties: Record<string,string>): string {
    for(const handle of this.handles)handle.pool.remove(handle.index);
    const p = {...properties}, n = shortName(name);
    // Inventory poses expose the identifying face; gameplay orientation stays
    // in the world. Delay, mode, lit, locked and other state details are retained.
    if(p.facing) p.facing = n.includes('piston') || n.includes('lightning_rod') ? 'up' : 'south';
    if(p.face)p.face='floor';
    if(n.endsWith('_door'))p.half='lower';
    const cell = {pos:[0,0,0] as [number,number,number],stateId:1,renderStateId:1,motion:0,value:Number(p.power ?? (p.powered==='true'||p.lit==='true'?15:0))};
    this.handles=drawBlock(cell,{stateId:1,name,properties:p},this.chunk,this.textures);

    // Frame the visible parts only: a torch or button needs the same usable
    // thumbnail area as a full block, without including empty pool capacity.
    const bounds=new THREE.Box3(), transform=new THREE.Matrix4();
    for(const handle of this.handles) {
      if(!handle.pool.visible)continue;
      const geometry=handle.pool.geometry;if(!geometry.boundingBox)geometry.computeBoundingBox();
      handle.pool.mesh.getMatrixAt(handle.index,transform);
      bounds.union(geometry.boundingBox!.clone().applyMatrix4(transform));
    }
    const center=bounds.getCenter(new THREE.Vector3());
    this.camera.position.copy(center).add(new THREE.Vector3(3,4,5));
    this.camera.lookAt(center);this.camera.updateMatrixWorld();
    const cameraBounds=bounds.clone().applyMatrix4(this.camera.matrixWorldInverse);
    const size=cameraBounds.getSize(new THREE.Vector3());
    const half=Math.max(size.x,size.y,.1)*.57;
    this.camera.left=-half;this.camera.right=half;this.camera.top=half;this.camera.bottom=-half;this.camera.updateProjectionMatrix();
    this.renderer.render(this.scene,this.camera);
    return this.renderer.domElement.toDataURL('image/png');
  }

  dispose() {
    this.chunk.box.dispose();this.chunk.cylinder.dispose();this.chunk.pick?.dispose();
    for(const pool of this.chunk.surfaces?.values() ?? [])pool.dispose();
    this.textures.dispose();this.material.dispose();this.renderer.dispose();this.renderer.forceContextLoss();
  }
}

let renderer: ThumbnailRenderer | undefined;
function renderNext() {
  const job=jobs.shift();
  if(!job) {renderer?.dispose();renderer=undefined;scheduled=false;return;}
  try {renderer ??= new ThumbnailRenderer();job.resolve(renderer.render(job.name,job.properties));}
  catch(error) {job.reject(error);}
  // Yield between thumbnails so opening a large inventory does not monopolize
  // the frame. A completed batch releases its WebGL context.
  setTimeout(renderNext,0);
}

function thumbnail(name: string, properties: Record<string,string>) {
  const key=JSON.stringify([name,Object.entries(properties).sort(([a],[b])=>a.localeCompare(b))]);
  let cached=thumbnails.get(key);
  if(!cached) {
    cached=new Promise<string>((resolve,reject)=>jobs.push({name,properties,resolve,reject}));
    thumbnails.set(key,cached);
    cached.catch(()=>thumbnails.delete(key));
    if(thumbnails.size>256)thumbnails.delete(thumbnails.keys().next().value!);
    if(!scheduled){scheduled=true;setTimeout(renderNext,0);}
  }
  return cached;
}

export function BlockThumbnail({name,properties}: {name: string;properties?: Record<string,string>}) {
  const item=connection.catalog.find(item=>item.name===name);
  const resolved=properties ?? catalogDefaults(item,connection.states);
  const signature=resolved ? JSON.stringify(resolved) : null;
  const [result,setResult]=useState<{key:string;url:string}|null>(null);
  const key=JSON.stringify([name,signature]);
  useEffect(()=>{
    let current=true;
    if(signature)void thumbnail(name,JSON.parse(signature)).then(url=>{if(current)setResult({key,url});},()=>{if(current)setResult({key,url:''});});
    return ()=>{current=false;};
  },[name,signature,key]);
  return result?.key===key && result.url
    ? <img className="blockThumbnail" src={result.url} alt="" aria-hidden="true" draggable={false}/>
    : <span className="blockThumbnailPlaceholder" aria-hidden="true" title={result?.key===key?'缩略图不可用':'正在加载缩略图'}/>;
}
