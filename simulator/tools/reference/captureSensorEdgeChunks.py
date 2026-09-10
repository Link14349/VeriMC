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

The same 1-per-step rule also makes the second half of `areAdjacentChunksTicking`,
`getChunkNow(x, z) == null`, unreachable while the listener itself ticks: the listener's own chunk
is at most 32, so every neighbour is at most 33, which is still FULL.

With a chunk aligned origin the 48x48 region is exactly 3x3 chunks, addressed here as (0,0)..(2,2)
with relative coordinates `[16i, 16i+16)`. `prepareRegion` forces all nine; each scenario then keeps
only some of those tickets and the rest of the levels follow from the distance rule, which is what
`chunkLevels` computes and declares as the kernel's explicit input.

Three geometries are captured:

- `java26_2SensorEdgeChunks`: only (0,0) keeps its ticket, so the listener in the middle chunk
  (1,1) is at 32 and five of its neighbours are at 33.
- `java26_2SensorEdgeDiagonal`: (0,0), (1,0) and (0,1) keep theirs, which leaves (1,1) at 32 with
  exactly **one** failing neighbour, the diagonal (2,2). This isolates the diagonal term of the
  3x3 loop; an orthogonal-only implementation would deliver here.
- `java26_2SensorEdgeNegative`: the first geometry at negative chunk indices, for the floored
  division in the chunk-of-position mapping.

Every scenario also carries a **control** sensor in a chunk that keeps its ticket (level 31, all
eight neighbours at 32), driven by the same event at the same distance, so the only difference
between the two sensors is where the chunk border falls.

A second, closer event is emitted while the edge sensor is stuck. `Listener.handleGameEvent` bails
out on `data.getCurrentVibration() != null`, so it must be dropped entirely: if vanilla re-selected,
the eventual power would be 12 (distance 2) instead of 8 (distance 4).

The declared `chunkState` inputs are not trusted either: `watchChunkState` records vanilla's own
`shouldTickBlocksAt` / `isPositionEntityTicking` / `hasChunkAt` at one watch position per region
chunk, every frame, so "it really is a level 32 edge chunk" is evidence rather than a claim.
"""
import sys

from captureRedstone import state
from captureVanillaScenario import runVanillaCapture

# Fixed visit order for the nine region chunks; it fixes the command and watch order too.
chunkOrder = [(0, 0), (1, 0), (0, 1), (1, 1), (2, 0), (2, 1), (2, 2), (1, 2), (0, 2)]
controlChunk, edgeChunk = (0, 0), (1, 1)
stallTick, eventTick, secondEventTick, resumeTick, endTick = 2, 6, 20, 30, 78
# ChunkLevel: <=31 entity ticking, <=32 block ticking, <=33 full/loaded, above that inaccessible.
levelNames = {31: 'entityTicking', 32: 'blockTicking', 33: 'loaded'}


def cell(chunk, y=1):
    """The representative block position of a region chunk: its (8, y, 8) offset."""
    return [chunk[0] * 16 + 8, y, chunk[1] * 16 + 8]


def chunkLevels(forced):
    """Ticket level per region chunk: 31 at a forced chunk, +1 per Chebyshev step away from one."""
    levels = {}
    for chunk in chunkOrder:
        levels[chunk] = min(31 + max(abs(chunk[0] - f[0]), abs(chunk[1] - f[1])) for f in forced)
    return levels


def build(origin, fixtureName, forced):
    sensors = {'edge': cell(edgeChunk), 'control': cell(controlChunk)}
    lamps = {key: [pos[0] + 1, pos[1], pos[2]] for key, pos in sensors.items()}
    # Four blocks away is a travel time of floor(4.0) = 4 ticks and a delivered power of
    # max(1, 15 - floor(15/8 * 4)) = 8. Two blocks away would be 12, which is the tell-tale.
    sources = {key: [pos[0] - 4, pos[1], pos[2]] for key, pos in sensors.items()}
    nearSource = [sensors['edge'][0] - 2, 1, sensors['edge'][2]]
    dropped = [chunk for chunk in chunkOrder if chunk not in forced]
    levels = chunkLevels(forced)
    if levels[edgeChunk] != 32 or all(levels[(edgeChunk[0] + dx, edgeChunk[1] + dz)] <= 32
                                      for dx in (-1, 0, 1) for dz in (-1, 0, 1)):
        raise SystemExit('the edge chunk must be block ticking with a non-ticking neighbour')
    if levels[controlChunk] != 31:
        raise SystemExit('the control chunk must keep its ticket')

    commands = []

    def put(tick, pos, name, **props):
        commands.append({'tick': tick, 'pos': list(pos), 'stateId': state(name, **props)})

    for pos in [sensors['edge'], lamps['edge'], sensors['control'], lamps['control']]:
        put(0, [pos[0], 0, pos[2]], 'stone')
    put(0, sensors['edge'], 'sculk_sensor')
    put(0, sensors['control'], 'sculk_sensor')
    put(0, lamps['edge'], 'redstone_lamp')
    put(0, lamps['control'], 'redstone_lamp')
    # Drop the tickets. The kernel does not model ticket propagation, so the level that vanilla's
    # own distance rule produces is declared as explicit input next to the real `chunkForced` call,
    # and every frame then records whether vanilla agrees.
    for chunk in dropped:
        commands.append({'tick': stallTick, 'pos': cell(chunk), 'chunkForced': False})
    for chunk in dropped:
        commands.append({'tick': stallTick, 'pos': cell(chunk), 'chunkState': levelNames[levels[chunk]]})
    for key in ['edge', 'control']:
        commands.append({'tick': eventTick, 'pos': sources[key],
                         'stimulus': {'gameEvent': 'minecraft:block_place'}})
    commands.append({'tick': secondEventTick, 'pos': nearSource,
                     'stimulus': {'gameEvent': 'minecraft:block_place'}})
    for chunk in dropped:
        commands.append({'tick': resumeTick, 'pos': cell(chunk), 'chunkForced': True})
    for chunk in dropped:
        commands.append({'tick': resumeTick, 'pos': cell(chunk), 'chunkState': 'entityTicking'})

    watch = [sensors['control'], lamps['control'], sensors['edge'], lamps['edge']]
    watch += [cell(chunk) for chunk in chunkOrder if chunk not in (controlChunk, edgeChunk)]
    # The chunk borders have to fall at known relative coordinates, so this cannot go through
    # GameTest, which picks its own random origin.
    return runVanillaCapture(commands, watch, endTick, fixtureName, origin,
                             watchChunkState=True, requiresAlignedOrigin=True)


scenarios = {
    'ring': ([16, -59, 32], 'java26_2SensorEdgeChunks', [(0, 0)]),
    'diagonal': ([16, -59, 32], 'java26_2SensorEdgeDiagonal', [(0, 0), (1, 0), (0, 1)]),
    # Negative chunk indices exercise the floored division in the chunk-of-position mapping.
    'negative': ([-64, -59, -80], 'java26_2SensorEdgeNegative', [(0, 0)]),
}


if __name__ == '__main__':
    for key in sys.argv[1:] or list(scenarios):
        build(*scenarios[key])
