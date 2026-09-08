import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { connection, type BlockCell, type BlockDef, type Pos, posKey } from './api';
import { DeviceTextures } from './deviceTextures';
import { box, cylinder, material, directionVectors, InstancePool, drawBlock, type Handle, type Chunk } from './blockModel';
import { surfacePlacement, type PlacementFace } from './surfacePlacement';
import { creativePlacement } from './creativeInteraction';
import { PlayerCollisionWorld, overlaps, playerBounds } from './playerCollision';
import { PlayerMovement } from './playerMovement';

import type { Tool, PickAction } from './interactionState';
type PickTarget = { pos: Pos; face: PlacementFace | null; hitHeight?: number };
const matrix = new THREE.Matrix4();
export class CircuitViewport {
  renderer: THREE.WebGLRenderer;
  scene = new THREE.Scene();
  camera = new THREE.PerspectiveCamera(42, 1, .1, 3000);
  controls: OrbitControls;
  private immersive = false;
  private inputBlocked = false;
  private inputFocused = true;
  private look = new THREE.Euler(0,0,0,'YXZ');
  private heldButton: number | null = null;
  private nextAction = 0;
  private savedOrbit: {position: THREE.Vector3; target: THREE.Vector3} | null = null;
  get firstPerson() { return this.immersive; }
  get pointerLocked() { return document.pointerLockElement === this.renderer.domElement; }
  onPointerLockChange: (locked: boolean) => void = () => {};
  onInputError: (message: string) => void = () => {};
  onPickBlock: (pos: Pos) => void = () => {};
  onHotbarScroll: (delta: number) => void = () => {};
  onUse: (pos: Pos, shift: boolean) => boolean = () => false;
  private activeTool: Tool = 'place';
  get tool(): Tool { return this.activeTool; }
  set tool(value: Tool) { this.activeTool = value; this.refreshHover(); }
  layer = 1;
  cutaway = false;
  hover: Pos | null = null;
  private chunks = new Map<string, Chunk>();
  private handles = new Map<string, Handle[]>();
  private raycaster = new THREE.Raycaster();
  private pointer = new THREE.Vector2();
  private plane = new THREE.Plane(new THREE.Vector3(0,1,0), -1);
  private grid: THREE.GridHelper;
  private axes = new THREE.AxesHelper(2.2);
  private selection = new THREE.Box3Helper(new THREE.Box3(new THREE.Vector3(), new THREE.Vector3(1,1,1)), 0xf0c88a);
  private ghost = new THREE.Group();
  private previewMaterial = new THREE.MeshStandardMaterial({ roughness: .84, transparent: true, opacity: .6, depthWrite: false });
  private previewChunk: Chunk;
  private previewHandles: Handle[] = [];
  private previewArrow: THREE.ArrowHelper;
  private placementKey = '';
  private placement: { name: string; properties: Record<string,string> | null } | null = null;
  private lastPointer: { clientX: number; clientY: number } | null = null;
  private clipPlane = new THREE.Plane(new THREE.Vector3(0,-1,0), 2000000);
  private sectionPlane = new THREE.Plane(new THREE.Vector3(0,0,0), 0);
  private deviceTextures = new DeviceTextures([this.clipPlane, this.sectionPlane]);
  private section: { axis: 'x'|'y'|'z'; maximum: number } | null = null;
  private resizeObserver: ResizeObserver;
  private frameId = 0;
  private down: { x: number; y: number; button: number; pointerId: number; dragged: boolean } | null = null;
  private movementKeys = new Set<string>();
  private lastFrame = performance.now();
  private moveRight = new THREE.Vector3();
  private moveForward = new THREE.Vector3();
  private moveDelta = new THREE.Vector3();
  private collisionWorld = new PlayerCollisionWorld();
  private movement = new PlayerMovement();
  onMovementModeChange: (mode: {collisionEnabled: boolean; flying: boolean}) => void = () => {};
  private previewCollisions: THREE.Box3[] = [];
  private selected: Pos | null = null;
  private lastFps = performance.now(); private frameCount = 0;
  onPick: (pos: Pos, tool: PickAction, additive: boolean, face: PlacementFace | null, hitHeight?: number) => void = () => {};
  onHover: (pos: Pos | null) => void = () => {};
  onFps: (value: number) => void = () => {};
  constructor(readonly container: HTMLDivElement) {
    this.renderer = new THREE.WebGLRenderer({ antialias: true, alpha: false, powerPreference: 'high-performance' });
    this.renderer.setPixelRatio(Math.min(devicePixelRatio, 2)); this.renderer.setClearColor(0x20272b); this.renderer.outputColorSpace = THREE.SRGBColorSpace; this.renderer.localClippingEnabled = true;
    material.clippingPlanes = [this.clipPlane, this.sectionPlane];
    this.renderer.domElement.tabIndex = 0;
    this.renderer.domElement.setAttribute('aria-label', '三维工作台，WASD 前后左右，Space 上升，Shift 下降');
    container.appendChild(this.renderer.domElement);
    this.scene.add(new THREE.HemisphereLight(0xf2f2e4, 0x4c5b69, 2.7)); const light = new THREE.DirectionalLight(0xffefcf, 3.1); light.position.set(-12,30,15); this.scene.add(light);
    this.camera.position.set(18,18,22); this.controls = new OrbitControls(this.camera, this.renderer.domElement); this.controls.target.set(4,0,2); this.controls.enableDamping = true; this.controls.dampingFactor = .12; this.controls.minDistance = 2; this.controls.maxDistance = 700;
    this.controls.mouseButtons = { LEFT: -1 as THREE.MOUSE, MIDDLE: THREE.MOUSE.PAN, RIGHT: THREE.MOUSE.ROTATE };
    this.renderer.domElement.addEventListener('keydown', this.keyDown);
    this.renderer.domElement.addEventListener('keyup', this.keyUp);
    this.renderer.domElement.addEventListener('blur', this.clearInput);
    this.renderer.domElement.addEventListener('focus', this.focusInput);
    window.addEventListener('blur', this.clearInput);
    window.addEventListener('focus', this.focusInput);
    document.addEventListener('visibilitychange', this.clearInput);
    document.addEventListener('pointerlockchange', this.lockChange);
    document.addEventListener('pointerlockerror', this.lockError);
    document.addEventListener('mousemove', this.mouseLook);
    document.addEventListener('pointerup', this.releaseButton);
    this.renderer.domElement.addEventListener('wheel', this.wheel, {passive:false});
    this.grid = new THREE.GridHelper(128,128,0x5c6668,0x343e42); this.grid.position.y = this.layer + .002; this.scene.add(this.grid);
    this.axes.position.set(-.5,.025,-.5); this.scene.add(this.axes);
    this.previewChunk = { group: this.ghost, box: new InstancePool(this.ghost, box, true, this.previewMaterial), cylinder: new InstancePool(this.ghost, cylinder, true, this.previewMaterial) };
    this.previewArrow = new THREE.ArrowHelper(new THREE.Vector3(0,0,1), new THREE.Vector3(.5,1.15,.5), .9, 0xf0c88a, .22, .13);
    this.previewArrow.visible = false; this.ghost.add(this.previewArrow);
    this.selection.visible = false; this.ghost.visible = false; this.scene.add(this.selection, this.ghost);
    this.resizeObserver = new ResizeObserver(() => this.resize()); this.resizeObserver.observe(container); this.resize();
    this.renderer.domElement.addEventListener('pointerdown', this.pointerDown); this.renderer.domElement.addEventListener('pointerup', this.pointerUp); this.renderer.domElement.addEventListener('pointermove', this.pointerMove); this.renderer.domElement.addEventListener('pointerleave', this.pointerLeave); this.renderer.domElement.addEventListener('pointercancel', this.clearInput); this.controls.addEventListener('change', this.refreshHover); this.renderer.domElement.addEventListener('contextmenu', event => event.preventDefault());
    connection.addEventListener('cells', this.applyChanges); this.rebuild(); this.animate();
  }
  setFirstPerson(value: boolean) {
    if (this.immersive === value) return;
    this.clearInput();
    if (value) {
      this.controls.update();
      this.savedOrbit = {position:this.camera.position.clone(),target:this.controls.target.clone()};
      this.controls.enabled = false;
      // 从正在观察的电路旁进入，避免在远离电路的轨道相机位置开始。
      const heading = this.camera.getWorldDirection(new THREE.Vector3());
      this.camera.position.copy(this.controls.target).addScaledVector(heading,-4);
      this.movement.setCollisionEnabled(this.movement.collisionEnabled);
      if(this.movement.collisionEnabled)this.camera.position.copy(this.collisionWorld.freePosition(this.camera.position));
      this.camera.fov = 70;
    } else {
      this.releasePointerLock();
      if (this.savedOrbit) {this.camera.position.copy(this.savedOrbit.position);this.controls.target.copy(this.savedOrbit.target);}
      this.controls.enabled = true; this.controls.update(); this.camera.fov = 42;
    }
    this.immersive = value;
    this.grid.visible = !value;
    this.axes.visible = !value;
    this.camera.updateProjectionMatrix();
    this.renderer.domElement.setAttribute('aria-label',value ? '第一人称搭建，点击捕获鼠标，E 物品栏，Esc 释放鼠标' : '三维工作台，WASD 前后左右，Space 上升，Shift 下降');
    this.refreshHover();
    this.reportMovementMode();
  }
  setCollisionEnabled(enabled: boolean) {
    this.clearInput();
    this.movement.setCollisionEnabled(enabled);
    if(enabled && this.immersive)this.camera.position.copy(this.collisionWorld.freePosition(this.camera.position));
    this.reportMovementMode();
    this.refreshHover();
  }
  private reportMovementMode() { this.onMovementModeChange({collisionEnabled:this.movement.collisionEnabled,flying:this.movement.flying}); }
  setInputBlocked(value: boolean) { this.inputBlocked = value; if (value) {this.clearInput();this.releasePointerLock();} this.refreshHover(); }
  requestPointerLock() {
    if (!this.immersive || this.inputBlocked || this.pointerLocked) return;
    this.renderer.domElement.focus({preventScroll:true});
    try {
      if (!this.renderer.domElement.requestPointerLock) {this.lockError();return;}
      const pending = this.renderer.domElement.requestPointerLock();
      pending?.catch(() => this.lockError());
    } catch { this.lockError(); }
  }
  releasePointerLock() { this.clearInput(); if (this.pointerLocked) document.exitPointerLock(); }
  private lockChange = () => {
    this.clearInput();
    if (this.pointerLocked && (!this.immersive || this.inputBlocked)) {document.exitPointerLock();return;}
    if (this.pointerLocked) {this.renderer.domElement.focus({preventScroll:true});this.focusInput();}
    this.onPointerLockChange(this.pointerLocked); this.refreshHover();
  };
  private lockError = () => this.onInputError('浏览器未能锁定鼠标，请点击“继续搭建”重试，或返回工作台。');
  private mouseLook = (event: MouseEvent) => {
    if (!this.immersive || !this.pointerLocked || this.inputBlocked) return;
    this.look.setFromQuaternion(this.camera.quaternion,'YXZ');
    this.look.y -= event.movementX * .002;
    this.look.x = Math.max(-Math.PI/2+.001,Math.min(Math.PI/2-.001,this.look.x-event.movementY*.002));
    this.camera.quaternion.setFromEuler(this.look); this.refreshHover();
  };
  private wheel = (event: WheelEvent) => {
    if (!this.immersive || this.inputBlocked || !this.pointerLocked || !event.deltaY) return;
    event.preventDefault(); this.onHotbarScroll(Math.sign(event.deltaY));
  };
  private releaseButton = () => { this.heldButton = null; };
  private centerPointer() { const rect=this.renderer.domElement.getBoundingClientRect();return {clientX:rect.left+rect.width/2,clientY:rect.top+rect.height/2}; }
  private creativeAction(button: number, shift: boolean) {
    if (!this.pointerLocked || this.inputBlocked) return;
    const event=this.centerPointer(), hit=this.locate(event,'select');
    if (button === 1) {if(hit)this.onPickBlock(hit.pos);return;}
    if (button === 0) {if(hit)this.onPick(hit.pos,'erase',shift,null);return;}
    if (button !== 2) return;
    if (hit && this.onUse(hit.pos,shift)) return;
    const target=this.locate(event,'place');
    if(target) {
      this.updatePreview(target.face,target.hitHeight);
      const body=playerBounds(this.camera.position), offset=new THREE.Vector3(...target.pos);
      if(this.movement.collisionEnabled) {
        const isDoor=this.placement?.name.endsWith('_door');
        if(this.previewCollisions.some(box=>overlaps(body,box.clone().translate(offset)) ||
          (isDoor && overlaps(body,box.clone().translate(offset).translate(new THREE.Vector3(0,1,0))))))return;
      }
      this.onPick(target.pos,'place',shift,target.face,target.hitHeight);
    }
  }
  placementProperties(face: PlacementFace | null, hitHeight?: number) {
    return creativePlacement(connection.catalog.find(item=>item.name===this.placement?.name),this.camera.getWorldDirection(new THREE.Vector3()),face,hitHeight);
  }
  private resize() { const { clientWidth: w, clientHeight: h } = this.container; this.renderer.setSize(w, h); this.camera.aspect = w / Math.max(h,1); this.camera.updateProjectionMatrix(); }
  private getChunk(pos: Pos): Chunk {
    const origin: Pos = pos.map(v => Math.floor(v / 16) * 16) as Pos, key = posKey(origin); let chunk = this.chunks.get(key);
    if (!chunk) { const group = new THREE.Group(); group.position.fromArray(origin); this.scene.add(group); chunk = { group, box: new InstancePool(group, box), cylinder: new InstancePool(group, cylinder) }; this.chunks.set(key, chunk); } return chunk;
  }
  private drawCell(cell: BlockCell) {
    const key = posKey(cell.pos); for (const handle of this.handles.get(key) ?? []) handle.pool.remove(handle.index); this.handles.delete(key);
    this.collisionWorld.set(key,[]);
    if (cell.stateId === 0) return;
    const def = connection.states.get(cell.renderStateId); if (!def) return;
    const collisions: THREE.Box3[] = [];
    this.handles.set(key, this.drawBlock(cell, def, this.getChunk(cell.pos),collisions));
    this.collisionWorld.set(key,collisions);
  }
  private drawBlock(cell: BlockCell, def: BlockDef, chunk: Chunk, collisions?: THREE.Box3[]): Handle[] {
    return drawBlock(cell,def,chunk,this.deviceTextures,chunk===this.previewChunk,collisions);
  }
  private applyChanges = (event: Event) => {
    const { full, changes } = (event as CustomEvent<{ full: boolean; changes: BlockCell[] }>).detail;
    if (full) this.rebuild(); else for (const cell of changes) this.drawCell(cell);
    if(this.immersive && this.movement.collisionEnabled)this.camera.position.copy(this.collisionWorld.freePosition(this.camera.position));
    this.refreshHover();
  };
  private rebuild() { for (const chunk of this.chunks.values()) { chunk.box.dispose(); chunk.cylinder.dispose(); chunk.pick?.dispose(); for(const pool of chunk.surfaces?.values() ?? [])pool.dispose(); this.scene.remove(chunk.group); } this.chunks.clear(); this.handles.clear(); this.collisionWorld.clear(); for (const cell of connection.cells.values()) this.drawCell(cell); }
  setLayer(layer: number, cutaway: boolean) {
    this.layer = Number.isFinite(layer) ? Math.max(-64, Math.min(319, Math.trunc(layer))) : this.layer;
    this.cutaway = cutaway; this.plane.constant = -this.layer;
    this.grid.position.y = this.layer + .002;
    this.clipPlane.constant = cutaway ? this.layer + 1.01 : 2000000;
    this.selection.visible = !!this.selected && this.positionVisible(this.selected);
    this.refreshHover();
  }
  setSection(axis: 'x'|'y'|'z'|'none', maximum = 0) {
    if (axis !== 'none' && !Number.isFinite(maximum)) return;
    this.section = axis === 'none' ? null : { axis, maximum: Math.trunc(maximum) };
    const normal: Pos = axis === 'y' ? [0,-1,0] : axis === 'z' ? [0,0,-1] : [-1,0,0];
    this.sectionPlane.normal.fromArray(normal);
    this.sectionPlane.constant = this.section ? this.section.maximum + 1.01 : 0;
    this.sectionPlane.normal.multiplyScalar(this.section ? 1 : 0);
    this.selection.visible = !!this.selected && this.positionVisible(this.selected);
    this.refreshHover();
  }
  private positionVisible(pos: Pos) {
    if (this.cutaway && pos[1] > this.layer) return false;
    return !this.section || pos[{x:0,y:1,z:2}[this.section.axis]] <= this.section.maximum;
  }
  setPlacement(name: string, properties: Record<string,string> | null) {
    this.placement = { name, properties };
    this.refreshHover();
  }
  private updatePreview(face: PlacementFace | null, hitHeight?: number) {
    // Older kernels may not have sent an unused block's default state yet.
    // Hide the model instead of inventing a facing or generating NaN geometry.
    const resolved = this.placement?.properties ? surfacePlacement(this.placement.name,{...this.placement.properties,...(this.immersive?this.placementProperties(face,hitHeight):{})},face) : null;
    if (!resolved) { this.placementKey = ''; this.previewCollisions=[]; return; }
    const { name, properties } = resolved;
    const key = JSON.stringify([name, Object.entries(properties).sort(([a],[b]) => a.localeCompare(b))]);
    if (key === this.placementKey) return;
    this.placementKey = key;
    for (const handle of this.previewHandles) handle.pool.remove(handle.index);
    const position = this.ghost.position.clone(); this.ghost.position.set(0,0,0);
    this.previewCollisions=[];
    this.previewHandles = this.drawBlock({ pos:[0,0,0], stateId:1, renderStateId:1, motion:0, value:0 }, { name, stateId:1, properties }, this.previewChunk,this.previewCollisions);
    this.ghost.position.copy(position);
    const direction = directionVectors[properties.facing];
    this.previewArrow.visible = !!direction;
    if (direction) {
      const output = name === 'minecraft:repeater' || name === 'minecraft:comparator';
      this.previewArrow.setDirection(new THREE.Vector3(...direction).multiplyScalar(output ? -1 : 1));
      this.previewArrow.setColor(name === 'minecraft:observer' ? 0x80bec4 : 0xf0c88a);
    }
  }
  select(pos: Pos | null) { this.selected = pos; this.selection.visible = !!pos && this.positionVisible(pos); if (pos) { this.selection.box.min.fromArray(pos).addScalar(-.007); this.selection.box.max.fromArray(pos).addScalar(1.007); } }
  fit() { const bounds = new THREE.Box3(); for (const cell of connection.cells.values()) bounds.expandByPoint(new THREE.Vector3(...cell.pos)); if (bounds.isEmpty()) bounds.set(new THREE.Vector3(-4,0,-4),new THREE.Vector3(4,0,4)); const center = bounds.getCenter(new THREE.Vector3()), size = bounds.getSize(new THREE.Vector3()).length(); this.controls.target.copy(center); this.camera.position.copy(center).add(new THREE.Vector3(1,1.25,1.4).multiplyScalar(Math.max(size*.65,8))); }
  top() { this.camera.position.copy(this.controls.target).add(new THREE.Vector3(0,Math.max(this.camera.position.distanceTo(this.controls.target),15),.001)); }
  private locate(event: { clientX: number; clientY: number }, tool: PickAction = this.tool): PickTarget | null {
    const rect = this.renderer.domElement.getBoundingClientRect(); if (!rect.width || !rect.height) return null; this.camera.updateMatrixWorld(); this.scene.updateMatrixWorld(); this.pointer.set((event.clientX-rect.left)/rect.width*2-1,-(event.clientY-rect.top)/rect.height*2+1); this.raycaster.setFromCamera(this.pointer,this.camera);
    // 第一人称搭建采用用户要求的加长交互距离，所有方块动作共用此射线。
    this.raycaster.far = this.immersive ? 20 : Infinity;
    const meshes: THREE.Object3D[] = []; for (const chunk of this.chunks.values()) { meshes.push(chunk.box.mesh,chunk.cylinder.mesh); if (chunk.pick) meshes.push(chunk.pick.mesh); }
    for (const hit of this.raycaster.intersectObjects(meshes)) {
      const pool = hit.object.userData.pool as InstancePool; const pos = pool.positions[hit.instanceId!];
      if (!pos || !this.positionVisible(pos)) continue;
      if (tool !== 'place') return { pos, face: null };
      if (!hit.face) return null;
      // 将实例几何的表面法线还原到世界坐标，再选择相邻方块格。
      pool.mesh.getMatrixAt(hit.instanceId!, matrix);
      matrix.premultiply(pool.mesh.matrixWorld);
      const normal = hit.face.normal.clone().transformDirection(matrix);
      const axis = Math.abs(normal.x) >= Math.abs(normal.y) && Math.abs(normal.x) >= Math.abs(normal.z) ? 0 : Math.abs(normal.y) >= Math.abs(normal.z) ? 1 : 2;
      const adjacent: Pos = [...pos]; adjacent[axis] += Math.sign(normal.getComponent(axis));
      const face: PlacementFace = axis === 0 ? (normal.x > 0 ? 'east' : 'west') : axis === 1 ? (normal.y > 0 ? 'up' : 'down') : (normal.z > 0 ? 'south' : 'north');
      return this.placementVisible(adjacent) ? { pos: adjacent, face, hitHeight:hit.point.y-pos[1] } : null;
    }
    if (tool !== 'place' || this.immersive) return null;
    const hit = this.raycaster.ray.intersectPlane(this.plane, new THREE.Vector3());
    const pos: Pos | null = hit ? [Math.floor(hit.x),this.layer,Math.floor(hit.z)] : null;
    return pos && this.placementVisible(pos) ? { pos, face: null } : null;
  }
  private placementVisible(pos: Pos) {
    return pos[1] >= -64 && pos[1] <= 319 && this.positionVisible(pos) && !connection.cells.has(posKey(pos));
  }
  private pointerDown = (event: PointerEvent) => {
    if (this.inputBlocked) return;
    this.renderer.domElement.focus({ preventScroll: true });
    if (this.immersive) {
      event.preventDefault();
      if (!this.pointerLocked) {this.requestPointerLock();return;}
      this.heldButton = event.button === 1 ? null : event.button;
      this.nextAction = performance.now()+250;
      this.creativeAction(event.button,event.shiftKey);return;
    }
    if (event.button === 0 || event.button === 2) this.down = { x:event.clientX, y:event.clientY, button:event.button, pointerId:event.pointerId, dragged:false };
  };
  private pointerUp = (event: PointerEvent) => {
    if (this.immersive || this.inputBlocked) return;
    const down = this.down; this.down = null;
    if (!down || event.pointerId !== down.pointerId || event.button !== down.button || down.dragged || Math.hypot(event.clientX-down.x,event.clientY-down.y) >= 5) return;
    // 工作台操作工具的左右键一致；搭建工具右键先使用命中器件，再考虑相邻放置。
    // 必须在拖动判定之后分派，避免右键旋转视角时误触器件。
    if(this.tool==='place' && event.button===2){
      const hit=this.locate(event,'select');
      if(hit && this.onUse(hit.pos,event.shiftKey))return;
    }
    const tool = this.tool === 'interact' ? 'interact' : event.button === 2 ? 'place' : this.tool === 'place' ? 'erase' : this.tool;
    const target = this.locate(event, tool); if (target) this.onPick(target.pos,tool,event.shiftKey,target.face,target.hitHeight);
  };
  private keyDown = (event: KeyboardEvent) => {
    if (this.inputBlocked || (this.immersive && !this.pointerLocked)) return;
    if (this.immersive) {
      if(event.code==='Escape'){event.preventDefault();this.releasePointerLock();return;}
      if (event.metaKey || event.altKey) {this.clearInput();return;}
      if (!['KeyW','KeyA','KeyS','KeyD','Space','ShiftLeft','ShiftRight','ControlLeft','ControlRight'].includes(event.code)) return;
      event.preventDefault();
      if(event.code==='Space') {
        const wasFlying=this.movement.flying;
        this.movement.pressSpace(event.timeStamp,event.repeat || this.movementKeys.has('Space'));
        if(this.movement.flying!==wasFlying)this.reportMovementMode();
      }
      this.movementKeys.add(event.code);return;
    }
    if (event.ctrlKey || event.metaKey || event.altKey) { this.movementKeys.clear(); return; }
    if (!['KeyW','KeyA','KeyS','KeyD','Space','ShiftLeft','ShiftRight'].includes(event.code)) return;
    event.preventDefault(); this.movementKeys.add(event.code);
  };
  private keyUp = (event: KeyboardEvent) => { this.movementKeys.delete(event.code); };
  private focusInput = () => { this.inputFocused=true; };
  private clearInput = (event?: Event) => {
    this.movementKeys.clear(); this.movement.clearInput(); this.down = null; this.heldButton = null;
    if(event?.type==='blur')this.inputFocused=false;
    if(event?.type==='visibilitychange')this.inputFocused=!document.hidden && document.hasFocus();
  };
  private moveCamera(seconds: number) {
    if(this.inputBlocked || (this.immersive && (!this.pointerLocked || !this.inputFocused || document.hidden)))return false;
    if (!this.movementKeys.size && (!this.immersive || (this.movement.flying && !this.movement.collisionEnabled))) return false;
    const held = (code: string) => Number(this.movementKeys.has(code));
    // 水平移动只取朝向，不随俯仰改变高度；俯视时仍保留左右方向。
    this.moveRight.set(1,0,0).applyQuaternion(this.camera.quaternion).setY(0).normalize();
    this.moveForward.set(this.moveRight.z,0,-this.moveRight.x);
    this.moveDelta.copy(this.moveRight).multiplyScalar(held('KeyD')-held('KeyA'));
    this.moveDelta.addScaledVector(this.moveForward, held('KeyW')-held('KeyS'));
    this.moveDelta.y = held('Space')-Number(this.movementKeys.has('ShiftLeft') || this.movementKeys.has('ShiftRight'));
    const sprint = this.immersive && (this.movementKeys.has('ControlLeft') || this.movementKeys.has('ControlRight'));
    if(this.immersive) {
      const wasFlying=this.movement.flying;
      this.moveDelta.copy(this.movement.move(this.collisionWorld,this.camera.position,this.moveDelta,seconds,sprint));
      if(this.movement.flying!==wasFlying)this.reportMovementMode();
    }
    else this.moveDelta.normalize().multiplyScalar(12*seconds);
    this.camera.position.add(this.moveDelta); this.controls.target.add(this.moveDelta);
    return this.moveDelta.lengthSq()>0;
  }
  private refreshHover = () => {
    const pointer = this.immersive ? (this.pointerLocked && !this.inputBlocked ? this.centerPointer() : null) : this.lastPointer;
    const target = pointer ? this.locate(pointer,this.immersive?'place':this.tool) : null;
    const pos = target?.pos ?? null;
    this.updatePreview(target?.face ?? null,target?.hitHeight);
    if ((pos ? posKey(pos) : null) !== (this.hover ? posKey(this.hover) : null)) this.onHover(pos);
    this.hover = pos; this.ghost.visible = !this.immersive && this.tool === 'place' && !!pos && !!this.placementKey;
    if (this.immersive) {
      const hit=pointer?this.locate(pointer,'select'):null;
      this.selection.visible=!!hit;
      if(hit){this.selection.box.min.fromArray(hit.pos).addScalar(-.007);this.selection.box.max.fromArray(hit.pos).addScalar(1.007);}
    }
    if (pos) this.ghost.position.fromArray(pos);
  };
  private pointerMove = (event: PointerEvent) => { if(this.immersive)return; if (this.down && Math.hypot(event.clientX-this.down.x,event.clientY-this.down.y) >= 5) this.down.dragged = true; this.lastPointer = { clientX:event.clientX, clientY:event.clientY }; this.refreshHover(); };
  private pointerLeave = () => { if(this.immersive)return; this.lastPointer = null; this.down = null; this.refreshHover(); };
  private animate = () => { this.frameId = requestAnimationFrame(this.animate); const now = performance.now(); const moved=this.moveCamera(Math.min((now-this.lastFrame)/1000,.05)); this.lastFrame = now;
    if(this.immersive) {
      if(this.heldButton!==null && now>=this.nextAction){this.nextAction=now+250;this.creativeAction(this.heldButton,this.movementKeys.has('ShiftLeft')||this.movementKeys.has('ShiftRight'));}
      if(moved || this.movementKeys.size)this.refreshHover();
    } else this.controls.update();
    this.renderer.render(this.scene,this.camera); this.frameCount++; if (now-this.lastFps > 1000) { this.onFps(Math.round(this.frameCount*1000/(now-this.lastFps))); this.lastFps=now; this.frameCount=0; } };
  dispose() { this.releasePointerLock(); document.removeEventListener('pointerlockchange',this.lockChange);document.removeEventListener('pointerlockerror',this.lockError);document.removeEventListener('mousemove',this.mouseLook);document.removeEventListener('pointerup',this.releaseButton); cancelAnimationFrame(this.frameId); window.removeEventListener('blur', this.clearInput); window.removeEventListener('focus',this.focusInput); document.removeEventListener('visibilitychange', this.clearInput); this.resizeObserver.disconnect(); connection.removeEventListener('cells',this.applyChanges); this.controls.removeEventListener('change', this.refreshHover); this.controls.dispose(); this.previewChunk.box.dispose(); this.previewChunk.cylinder.dispose(); this.previewChunk.pick?.dispose(); for(const pool of this.previewChunk.surfaces?.values() ?? [])pool.dispose(); this.previewMaterial.dispose(); this.previewArrow.dispose(); this.grid.dispose(); this.axes.dispose(); this.selection.dispose(); for (const c of this.chunks.values()) { c.box.dispose(); c.cylinder.dispose(); c.pick?.dispose(); for(const pool of c.surfaces?.values() ?? [])pool.dispose(); } this.deviceTextures.dispose(); this.renderer.dispose(); this.renderer.domElement.remove(); }
}
