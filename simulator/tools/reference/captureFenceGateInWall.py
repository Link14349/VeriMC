#!/usr/bin/env python3
"""`FenceGateBlock.IN_WALL` is recomputed on shape updates along the perpendicular axis.

`updateShape` only reacts when `directionToNeighbour.getAxis() == FACING.getClockWise().getAxis()`,
and then sets `IN_WALL` from `BlockTags.WALLS` on either side. A gate written with a stale
`IN_WALL` is corrected by the first such update; this is the second difference the random
differential (`fuzzRedstone.py`) found.

Walls themselves are an unimplemented block, so only the gate and a plain block are asserted;
the wall's own connection state is deliberately outside the palette and is not compared. The
intra-tick trace is therefore not enabled for this scenario.
"""
from captureRedstone import runCapture, state

commands = []
watch = []


def put(tick, pos, name, **props):
    commands.append({'tick': tick, 'pos': list(pos), 'stateId': state(name, **props)})


def clear(tick, pos):
    commands.append({'tick': tick, 'pos': list(pos), 'stateId': 0})


def gate(x, z, inWall):
    for dx in range(-2, 3):
        for dz in range(-2, 3):
            put(0, (x + dx, 1, z + dz), 'stone')
    pos = (x, 2, z)
    put(0, pos, 'oak_fence_gate', facing='north', in_wall='true' if inWall else 'false')
    watch.append(list(pos))
    # 2 gt: a wall appears on the perpendicular axis; 6 gt it goes away again.
    put(2, (x + 1, 2, z), 'cobblestone_wall')
    clear(6, (x + 1, 2, z))
    # 10 gt: a plain block on the facing axis, which must not touch IN_WALL.
    put(10, (x, 2, z - 1), 'white_wool')
    watch.append([x, 2, z - 1])


gate(6, 6, True)
gate(14, 6, False)

if __name__ == '__main__':
    runCapture(commands, watch, 14, 'java26_2FenceGateInWall', discardDrops=True)
