#!/usr/bin/env python3
"""A sculk sensor on a *block ticking* chunk whose 3x3 is not all block ticking never delivers.

`SculkSensorBlockEntity.VibrationUser.requiresAdjacentChunksToBeTicking()` returns true, and
`VibrationSystem.Ticker.receiveVibration` starts with

    if (user.requiresAdjacentChunksToBeTicking() && !areAdjacentChunksTicking(level, destination))
        return false;

so the vibration is neither delivered nor cleared: `travelTime` has already been decremented to 0,
stays 0, and the listener retries every single tick until the neighbourhood is ticking again.

Building the situation needs the real ticket geometry, not a claim about it. `ChunkMap`'s
FORCED_TICKET_LEVEL is `ChunkLevel.byStatus(ENTITY_TICKING)` = 31 and ticket levels grow by one per
Chebyshev step, so one forced chunk gives 31 at its centre, 32 (block ticking) on the first ring and
33 (loaded, *not* block ticking) on the second. That is why "entity ticking listener next to a
merely loaded chunk" cannot exist in vanilla — a 31 chunk's eight neighbours are 32 by construction.
The only vanilla shape that fails the gate is a listener on the **outer, block-ticking ring**.

With a chunk aligned origin the 48x48 region is exactly 3x3 chunks, addressed here as (0,0)..(2,2)
with relative coordinates `[16i, 16i+16)`. `prepareRegion` forces all nine; dropping every ticket
but (0,0)'s leaves

    (0,0) = 31 entityTicking
    (1,0) (0,1) (1,1) = 32 blockTicking
    (2,0) (0,2) (2,1) (1,2) (2,2) = 33 loaded, not block ticking

- The **edge** sensor sits in the middle chunk (1,1): block ticking itself, so its block entity
  keeps running, but its 3x3 covers (2,2) and friends at level 33, so the gate is false.
- The **control** sensor sits in the forced chunk (0,0): its eight neighbours are all at 32, so it
  delivers normally. Both sensors are driven by the same kind of event at the same distance in the
  same capture, so the only difference between them is where the chunk border falls.

A second, closer event is emitted while the edge sensor is stuck. `Listener.handleGameEvent` bails
out on `data.getCurrentVibration() != null`, so it must be dropped entirely: if vanilla re-selected,
the eventual power would be 12 (distance 2) instead of 8 (distance 4).

The declared `chunkState` inputs are not trusted either: `watchChunkState` records vanilla's own
`shouldTickBlocksAt` / `isPositionEntityTicking` / `hasChunkAt` at one watch position per region
chunk, every frame, so "it really is a level 32 edge chunk" is evidence rather than a claim.
"""
from captureRedstone import state
from captureVanillaScenario import runVanillaCapture

# One representative cell per region chunk; index into `watch` is the chunkStates row index.
edgeSensor, edgeLamp = [24, 1, 24], [25, 1, 24]           # chunk (1,1), level 32
controlSensor, controlLamp = [8, 1, 8], [9, 1, 8]         # chunk (0,0), level 31
edgeSource, edgeSourceNear = [20, 1, 24], [22, 1, 24]     # 4 and 2 blocks from the edge sensor
controlSource = [4, 1, 8]                                 # 4 blocks from the control sensor
# Probe cells, one per remaining region chunk, so every chunk's ticking flags are recorded.
probes = {(1, 0): [24, 1, 8], (0, 1): [8, 1, 24], (2, 0): [40, 1, 8], (2, 1): [40, 1, 24],
          (2, 2): [40, 1, 40], (1, 2): [24, 1, 40], (0, 2): [8, 1, 40]}
# Everything but (0,0) loses its ticket at `stallTick` and gets it back at `resumeTick`.
dropped = [(1, 0), (0, 1), (1, 1), (2, 0), (2, 1), (2, 2), (1, 2), (0, 2)]
stalled = {(1, 0): 'blockTicking', (0, 1): 'blockTicking', (1, 1): 'blockTicking',
           (2, 0): 'loaded', (2, 1): 'loaded', (2, 2): 'loaded', (1, 2): 'loaded', (0, 2): 'loaded'}
stallTick, eventTick, secondEventTick, resumeTick, endTick = 2, 6, 20, 30, 78


def cell(chunk):
    """A block position inside a region chunk, used as the address of a chunk level command."""
    return probes.get(chunk, [chunk[0] * 16 + 8, 1, chunk[1] * 16 + 8])


def main():
    commands = []

    def put(tick, pos, name, **props):
        commands.append({'tick': tick, 'pos': list(pos), 'stateId': state(name, **props)})

    for pos in [edgeSensor, edgeLamp, controlSensor, controlLamp]:
        put(0, [pos[0], 0, pos[2]], 'stone')
    put(0, edgeSensor, 'sculk_sensor')
    put(0, controlSensor, 'sculk_sensor')
    put(0, edgeLamp, 'redstone_lamp')
    put(0, controlLamp, 'redstone_lamp')
    # Drop the eight tickets, keeping only (0,0). The kernel does not model ticket propagation, so
    # the resulting level is declared as explicit input next to the real `chunkForced` call.
    for chunk in dropped:
        commands.append({'tick': stallTick, 'pos': cell(chunk), 'chunkForced': False})
    for chunk in dropped:
        commands.append({'tick': stallTick, 'pos': cell(chunk), 'chunkState': stalled[chunk]})
    for pos in [edgeSource, controlSource]:
        commands.append({'tick': eventTick, 'pos': pos, 'stimulus': {'gameEvent': 'minecraft:block_place'}})
    commands.append({'tick': secondEventTick, 'pos': edgeSourceNear,
                     'stimulus': {'gameEvent': 'minecraft:block_place'}})
    for chunk in dropped:
        commands.append({'tick': resumeTick, 'pos': cell(chunk), 'chunkForced': True})
    for chunk in dropped:
        commands.append({'tick': resumeTick, 'pos': cell(chunk), 'chunkState': 'entityTicking'})

    watch = [controlSensor, controlLamp, edgeSensor, edgeLamp] + [probes[c] for c in
             [(1, 0), (0, 1), (2, 0), (2, 1), (2, 2), (1, 2), (0, 2)]]
    # The chunk borders have to fall at known relative coordinates, so this cannot go through
    # GameTest, which picks its own random origin.
    return runVanillaCapture(commands, watch, endTick, 'java26_2SensorEdgeChunks',
                             [16, -59, 32], watchChunkState=True, requiresAlignedOrigin=True)


if __name__ == '__main__':
    main()
