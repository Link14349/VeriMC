import * as THREE from 'three';

// Original pixel artwork, drawn in code. XK redstone display is a visual
// reference only; no resource-pack textures or models are bundled here.
const digits = ['111101101101111','010110010010111','111001111100111','111001111001111','101101111001001','111100111001111','111100111101111','111001001001001','111101111101111','111101111001111'];

export class DeviceTextures {
  private materials = new Map<string, THREE.MeshBasicMaterial>();
  constructor(private clippingPlanes: THREE.Plane[]) {}

  get(key: string, preview: boolean): THREE.MeshBasicMaterial {
    const cacheKey = `${key}:${preview}`;
    let material = this.materials.get(cacheKey);
    if (material) return material;
    if (preview) {
      material = this.get(key, false).clone();
      material.transparent = true; material.opacity = .6; material.depthWrite = false;
    } else {
      const canvas = document.createElement('canvas'); canvas.width = canvas.height = 32;
      const ctx = canvas.getContext('2d')!;
      const rect = (x: number, y: number, w: number, h: number, shade: string) => { ctx.fillStyle = shade; ctx.fillRect(x,y,w,h); };
      const label = (text: string, x: number, y: number, shade: string, scale = 1) => {
        for (const [i, char] of [...text].entries()) for (let p=0;p<15;p++) if (digits[Number(char)]?.[p]==='1') rect(x+i*4*scale+(p%3)*scale,y+Math.floor(p/3)*scale,scale,scale,shade);
      };
      const stone = (base = '#a5aaa5') => {
        rect(0,0,32,32,'#626b68'); rect(1,1,30,30,base);
        for(let y=2;y<30;y+=3)for(let x=2;x<30;x+=3)rect(x,y,2,1,(x*13+y*7)%5<2?'#bbc0b8':'#929c96');
        rect(1,1,30,1,'#dce0d5'); rect(1,1,1,30,'#cbd0c5');
      };
      const arrow = (down: boolean, shade: string) => {
        rect(14,10,4,12,shade);
        for(let i=0;i<5;i++)rect(15-i,down?24-i:7+i,2+i*2,1,shade);
      };
      const [kind, state='0', variant='0'] = key.split(':');
      const on = state === '1', signal = on ? '#ff4537' : '#682a2b';
      if (kind === 'repeater' || kind === 'comparator') {
        stone('#c6c9bd'); rect(3,3,26,26,'#d3d5ca');
        rect(14,4,4,24,signal);
        for (const x of [5,25]) for(let i=0;i<4;i++)rect(x,13+i*3,2,1,'#777f79');
        if (kind === 'repeater') {
          label(variant,23,4,'#263b3c',2);
          for(let i=0;i<4;i++)rect(4,13+i*3,4,2,i<Number(variant)?signal:'#9fa99d');
        } else {
          rect(8,22,16,2,signal); rect(8,22,2,5,signal); rect(22,22,2,5,signal);
          rect(23,7,6,2,'#263b3c'); if(variant==='0')rect(23,11,6,2,'#263b3c');
        }
        // Output is toward the single fixed torch, opposite the state's facing.
        rect(5,4,2,6,signal); rect(4,5,4,1,signal); rect(3,6,6,1,signal);
      } else if (kind === 'wire') {
        const power = Number(state); rect(0,0,32,32,power ? '#992e26':'#392626');
        rect(1,1,30,30,power ? '#cf3d2d':'#542a2b');
        label(String(power),power<10?10:2,6,'#fff0d1',4);
      } else if (kind.startsWith('observer')) {
        stone('#8b9692');
        if (kind === 'observerFront') {
          for(const x of [7,21]) {rect(x,9,5,6,'#303d3d');rect(x+1,10,2,2,'#d4d9ce');}
          rect(10,22,12,3,'#384746'); rect(7,19,3,3,'#384746');rect(22,19,3,3,'#384746');
        } else if (kind === 'observerBack') {
          rect(7,7,18,18,'#465452');rect(10,10,12,12,signal);rect(13,13,6,6,on?'#ffc082':'#87382f');
        } else {
          rect(5,4,22,24,'#53605c'); arrow(true,on?'#ff6e4e':'#d3d9cb');
          rect(3,4,2,4,signal);rect(27,24,2,4,signal);
        }
      } else if (kind === 'dispenserFront') {
        stone('#acb4ae');
        for(const x of [6,21]) {rect(x,7,5,4,'#35413f');rect(x,7,5,1,'#d3d9cc');}
        rect(10,14,12,15,'#606c65');rect(7,17,18,9,'#606c65');
        rect(12,16,8,11,'#26322f');rect(9,19,14,5,'#26322f');
        rect(12,16,8,2,'#141e1c');
      } else if (kind === 'lamp') {
        rect(0,0,32,32,'#44382e');
        for(let y=2;y<32;y+=8)for(let x=2;x<32;x+=8) {
          rect(x,y,6,6,on?'#ffd28a':'#786047');rect(x+1,y+1,3,3,on?'#ffedb5':'#a28458');
        }
      } else if (kind === 'pistonSide') {
        stone('#84918a');rect(2,2,28,6,'#bba477');rect(2,9,28,2,'#454e48');
        rect(6,14,20,13,'#465951');arrow(false,on?'#a9d56c':'#d2bd8b');
      } else if (kind === 'pistonHead') {
        rect(0,0,32,32,'#645b43');rect(2,2,28,28,on?'#91b663':'#bd9e6b');
        for(const y of [9,20])rect(2,y,28,1,on?'#73944e':'#937649');
        for(const x of [3,26])for(const y of [3,26])rect(x,y,3,3,'#636e65');
      } else if (kind === 'torch') {
        rect(0,0,32,32,on?'#ed3f2c':'#71342e');rect(5,4,22,24,on?'#ff7350':'#944a39');rect(10,8,12,16,on?'#ffcf8e':'#ad6750');
      }
      const map = new THREE.CanvasTexture(canvas);
      map.magFilter = map.minFilter = THREE.NearestFilter; map.generateMipmaps = false; map.colorSpace = THREE.SRGBColorSpace;
      material = new THREE.MeshBasicMaterial({ map, clippingPlanes: this.clippingPlanes, polygonOffset: true, polygonOffsetFactor: -1, polygonOffsetUnits: -1 });
    }
    this.materials.set(cacheKey, material); return material;
  }

  dispose() {
    const textures = new Set<THREE.Texture>();
    for(const material of this.materials.values()) { if(material.map)textures.add(material.map); material.dispose(); }
    for(const texture of textures)texture.dispose(); this.materials.clear();
  }
}
