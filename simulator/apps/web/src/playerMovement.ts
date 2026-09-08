import { Vector3 } from 'three';
import type { PlayerCollisionWorld } from './playerCollision';

// Editor movement tuning in blocks / seconds, independent of the simulation tick.
const gravity = 24, jumpSpeed = 8, terminalSpeed = 60, doublePressMs = 300;
const stepHeight = .6, autoJumpHeight = 1, clearance = 1e-5;
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

  private stepUp(world: PlayerCollisionWorld, eye: Vector3, delta: Vector3, normal: Vector3) {
    const lift=world.move(eye,new Vector3(0,stepHeight,0)).y;
    if (lift <= clearance) return null;
    const raised=eye.clone().add(new Vector3(0,lift,0));
    const across=world.move(raised,new Vector3(delta.x,0,delta.z));
    if (across.x**2+across.z**2 <= normal.x**2+normal.z**2+1e-10) return null;
    const drop=world.move(raised.add(across),new Vector3(0,-lift,0)).y;
    const rise=lift+drop;
    return rise>clearance ? new Vector3(across.x,rise,across.z) : null;
  }

  private canAutoJump(world: PlayerCollisionWorld, eye: Vector3, horizontal: Vector3) {
    // Require both headroom for the jump and a reachable top ahead. A tall wall
    // or low ceiling should stop movement without repeatedly bouncing the player.
    const peak=jumpSpeed**2/(2*gravity);
    if (world.move(eye,new Vector3(0,peak,0)).y < peak-clearance) return false;
    const lift=autoJumpHeight+clearance;
    const raised=eye.clone().add(new Vector3(0,lift,0));
    const ahead=horizontal.clone().setY(0).normalize().multiplyScalar(.65);
    const across=world.move(raised,ahead);
    if (across.distanceToSquared(ahead)>1e-10) return false;
    const down=world.move(raised.add(across),new Vector3(0,-lift-.02,0));
    const rise=lift+down.y;
    return rise>stepHeight && rise<=autoJumpHeight+clearance;
  }

  move(world: PlayerCollisionWorld, eye: Vector3, input: Vector3, seconds: number, sprint: boolean) {
    if (!(seconds > 0) || !Number.isFinite(seconds)) return new Vector3();
    const direction=input.clone();
    if (!this.flying) direction.y=0;
    const speed=this.flying ? (sprint?21.78:10.89) : (sprint?6.5:4.3);
    direction.normalize().multiplyScalar(speed);
    if (this.flying) {
      const delta=direction.multiplyScalar(seconds);
      if (!this.collisionEnabled) return delta;
      const accepted=world.move(eye,delta);
      // Only foot support ends flight: walls and head contact must not do so.
      // Check the final position to include exact landings and exclude sliding
      // past a platform edge. Upward input still allows double-Space takeoff.
      if (delta.y <= 0 && world.move(eye.clone().add(accepted),new Vector3(0,-1e-5,0)).y > -1e-5+1e-7) {
        this.setFlying(false);
        this.clearInput();
      }
      return accepted;
    }
    const position=eye.clone();
    if (this.jumpRequested && world.move(position,new Vector3(0,-.02,0)).y > -.02+1e-7) this.verticalSpeed=jumpSpeed;
    this.jumpRequested=false;
    // Small bounded steps keep gravity and contacts stable across display rates.
    for (let remaining=seconds;remaining>1e-9;) {
      const step=Math.min(remaining,1/120);
      remaining-=step;
      let nextSpeed=Math.max(-terminalSpeed,this.verticalSpeed-gravity*step);
      const delta=new Vector3(direction.x*step,(this.verticalSpeed+nextSpeed)*.5*step,direction.z*step);
      let accepted=world.move(position,delta);
      const blockedHorizontally=Math.abs(accepted.x-delta.x)>1e-7 || Math.abs(accepted.z-delta.z)>1e-7;
      if (blockedHorizontally && this.verticalSpeed<=0 && world.move(position,new Vector3(0,-.02,0)).y>-.02+1e-7) {
        const stepped=this.stepUp(world,position,delta,accepted);
        if (stepped) { position.add(stepped); this.verticalSpeed=0; continue; }
        // Shift permits careful movement at an edge without automatic jumping.
        if (input.y>=0 && this.canAutoJump(world,position,delta)) {
          this.verticalSpeed=jumpSpeed;
          nextSpeed=this.verticalSpeed-gravity*step;
          delta.y=(this.verticalSpeed+nextSpeed)*.5*step;
          accepted=world.move(position,delta);
        }
      }
      position.add(accepted);
      this.verticalSpeed=Math.abs(accepted.y-delta.y)>1e-9 ? 0 : nextSpeed;
    }
    return position.sub(eye);
  }
}
