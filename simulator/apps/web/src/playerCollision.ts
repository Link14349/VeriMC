import { Box3, Vector3 } from 'three';

// Editor movement dimensions; this is not a Minecraft entity simulation.
export const playerWidth = .6, playerHeight = 1.8, playerEyeHeight = 1.62;
const epsilon = 1e-7, bucketSize = 4;
type Axis = 'x' | 'y' | 'z';
const axes: Axis[] = ['x','y','z'];
type Collider = { boxes: Box3[]; buckets: string[] };

export function playerBounds(eye: Vector3) {
  return new Box3(
    new Vector3(eye.x-playerWidth/2,eye.y-playerEyeHeight,eye.z-playerWidth/2),
    new Vector3(eye.x+playerWidth/2,eye.y-playerEyeHeight+playerHeight,eye.z+playerWidth/2),
  );
}

export function overlaps(a: Box3, b: Box3) {
  return axes.every(axis => a.max[axis] > b.min[axis]+epsilon && a.min[axis] < b.max[axis]-epsilon);
}

// Selection geometry is also used for wires and controls, which must not act as walls.
export function blocksPlayer(name: string, properties: Record<string,string>) {
  const short = name.replace(/^minecraft:/,'');
  return !['air','redstone_wire','redstone_torch','redstone_wall_torch','lever','tripwire','tripwire_hook','rail'].includes(short)
    && !short.endsWith('_button') && !short.endsWith('pressure_plate') && !short.endsWith('_rail')
    && !(short.endsWith('_fence_gate') && properties.open === 'true');
}

function bucketKeys(bounds: Box3) {
  const keys: string[] = [];
  for (let x=Math.floor(bounds.min.x/bucketSize);x<=Math.floor(bounds.max.x/bucketSize);x++)
    for (let y=Math.floor(bounds.min.y/bucketSize);y<=Math.floor(bounds.max.y/bucketSize);y++)
      for (let z=Math.floor(bounds.min.z/bucketSize);z<=Math.floor(bounds.max.z/bucketSize);z++) keys.push(`${x},${y},${z}`);
  return keys;
}

export class PlayerCollisionWorld {
  private cells = new Map<string, Collider>();
  private buckets = new Map<string, Set<Collider>>();

  clear() { this.cells.clear(); this.buckets.clear(); }
  set(key: string, boxes: Box3[]) {
    const previous = this.cells.get(key);
    if (previous) {
      for (const bucket of previous.buckets) {
        const entries = this.buckets.get(bucket)!;
        entries.delete(previous);
        if (!entries.size) this.buckets.delete(bucket);
      }
      this.cells.delete(key);
    }
    if (!boxes.length) return;
    const bounds = new Box3();
    for (const box of boxes) bounds.union(box);
    const collider = { boxes, buckets:bucketKeys(bounds) };
    this.cells.set(key,collider);
    for (const key of collider.buckets) {
      let entries = this.buckets.get(key);
      if (!entries) { entries=new Set(); this.buckets.set(key,entries); }
      entries.add(collider);
    }
  }

  private query(bounds: Box3) {
    const found = new Set<Collider>();
    for (const key of bucketKeys(bounds)) for (const collider of this.buckets.get(key) ?? []) found.add(collider);
    return [...found].flatMap(collider => collider.boxes);
  }

  intersects(eye: Vector3) {
    const body = playerBounds(eye);
    return this.query(body).some(box => overlaps(body,box));
  }

  // Clip the whole displacement against the swept volume, including thin obstacles.
  // Resolve vertical motion first, then slide along the remaining horizontal axis.
  move(eye: Vector3, delta: Vector3) {
    const body = playerBounds(eye), swept = body.clone().union(body.clone().translate(delta));
    const obstacles = this.query(swept), accepted = delta.clone();
    const order: Axis[] = Math.abs(delta.x) >= Math.abs(delta.z) ? ['y','x','z'] : ['y','z','x'];
    for (const axis of order) {
      let distance = accepted[axis];
      if (!distance) continue;
      for (const box of obstacles) {
        if (!axes.every(other => other === axis || (body.max[other] > box.min[other]+epsilon && body.min[other] < box.max[other]-epsilon))) continue;
        if (distance > 0 && body.max[axis] <= box.min[axis]+epsilon) distance=Math.min(distance,Math.max(0,box.min[axis]-body.max[axis]));
        else if (distance < 0 && body.min[axis] >= box.max[axis]-epsilon) distance=Math.max(distance,Math.min(0,box.max[axis]-body.min[axis]));
      }
      accepted[axis]=distance;
      body.min[axis]+=distance; body.max[axis]+=distance;
    }
    return accepted;
  }

  // Only used on entry / scene changes: recover from a block placed around the body.
  // Check six exits, including consecutive solids, rather than oscillating between faces.
  freePosition(eye: Vector3) {
    if (!this.intersects(eye)) return eye.clone();
    let best: Vector3 | null = null, bestDistance = Infinity;
    for (const axis of ['y','x','z'] as Axis[]) for (const sign of [1,-1]) {
      const candidate = eye.clone();
      for (let attempt=0;attempt<64;attempt++) {
        const body = playerBounds(candidate), hits = this.query(body).filter(box => overlaps(body,box));
        if (!hits.length) {
          const distance = candidate.distanceToSquared(eye);
          if (distance < bestDistance) { best=candidate.clone(); bestDistance=distance; }
          break;
        }
        const shift = sign > 0 ? Math.max(...hits.map(box => box.max[axis]-body.min[axis]))+epsilon
          : Math.min(...hits.map(box => box.min[axis]-body.max[axis]))-epsilon;
        candidate[axis]+=shift;
        if (candidate.distanceToSquared(eye) >= bestDistance) break;
      }
    }
    if (best) return best;
    // A very large solid import may exceed the bounded local search. Above all
    // geometry is always free; the global scan never runs during normal movement.
    let top = eye.y-playerEyeHeight;
    for (const cell of this.cells.values()) for (const box of cell.boxes) top=Math.max(top,box.max.y);
    return new Vector3(eye.x,top+playerEyeHeight+epsilon,eye.z);
  }
}
