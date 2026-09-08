import { Vector3 } from 'three';
import type { PlayerCollisionWorld } from './playerCollision';

// Editor movement tuning in blocks / seconds, independent of the simulation tick.
const gravity = 24, jumpSpeed = 8, terminalSpeed = 60, doublePressMs = 300;
export class PlayerMovement {
  collisionEnabled = false;
  flying = true;
  private verticalSpeed = 0;
  private jumpRequested = false;
  private lastSpacePress = -Infinity;

  setCollisionEnabled(enabled: boolean) {
    this.collisionEnabled=enabled;
    this.setFlying(!enabled);
    this.clearInput();
  }
  setFlying(flying: boolean) {
    this.flying=!this.collisionEnabled || flying;
    this.verticalSpeed=0;
    this.jumpRequested=false;
  }
  pressSpace(time: number, repeat: boolean) {
    if (repeat || !this.collisionEnabled) return;
    if (time-this.lastSpacePress >= 0 && time-this.lastSpacePress <= doublePressMs) {
      this.setFlying(!this.flying);
      this.lastSpacePress=-Infinity;
    } else {
      this.lastSpacePress=time;
      this.jumpRequested=!this.flying;
    }
  }
  clearInput() { this.jumpRequested=false; this.lastSpacePress=-Infinity; }

  move(world: PlayerCollisionWorld, eye: Vector3, input: Vector3, seconds: number, sprint: boolean) {
    if (!(seconds > 0) || !Number.isFinite(seconds)) return new Vector3();
    const direction=input.clone();
    if (!this.flying) direction.y=0;
    const speed=this.flying ? (sprint?21.78:10.89) : (sprint?6.5:4.3);
    direction.normalize().multiplyScalar(speed);
    if (this.flying) {
      const delta=direction.multiplyScalar(seconds);
      return this.collisionEnabled ? world.move(eye,delta) : delta;
    }
    const position=eye.clone();
    if (this.jumpRequested && world.move(position,new Vector3(0,-.02,0)).y > -.02+1e-7) this.verticalSpeed=jumpSpeed;
    this.jumpRequested=false;
    // Small bounded steps keep gravity and contacts stable across display rates.
    for (let remaining=seconds;remaining>1e-9;) {
      const step=Math.min(remaining,1/120);
      remaining-=step;
      const nextSpeed=Math.max(-terminalSpeed,this.verticalSpeed-gravity*step);
      const delta=new Vector3(direction.x*step,(this.verticalSpeed+nextSpeed)*.5*step,direction.z*step);
      const accepted=world.move(position,delta);
      position.add(accepted);
      this.verticalSpeed=Math.abs(accepted.y-delta.y)>1e-9 ? 0 : nextSpeed;
    }
    return position.sub(eye);
  }
}
