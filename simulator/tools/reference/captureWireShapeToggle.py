#!/usr/bin/env python3
"""Right-clicking redstone wire only notifies directions whose connectivity really changed.

`RedStoneWireBlock.useWithoutItem` does nothing at all when the recomputed state equals the
old one, and `updatesOnShapeChange` only notifies a horizontal direction when
`RedstoneSide.isConnected()` differs there and the neighbour is a redstone conductor.
`SIDE` and `UP` are both "connected", so switching between them is not a change.

Quasi-connected pistons that were never notified act as the detectors.
"""
from captureRedstone import runCapture, state

commands = []
watch = []


def put(tick, pos, name, **props):
    commands.append({'tick': tick, 'pos': list(pos), 'stateId': state(name, **props)})


def interact(tick, pos):
    commands.append({'tick': tick, 'pos': list(pos), 'interact': True})


def budPiston(tick, pos, facing, powerOffset):
    """A retracted piston plus a diagonal power source that never notifies it."""
    put(tick, pos, 'piston', facing=facing)
    put(tick, (pos[0] + powerOffset[0], pos[1] + 1, pos[2] + powerOffset[2]), 'redstone_block')


def floor(x, z, minX, maxX, minZ, maxZ):
    for dx in range(minX, maxX + 1):
        for dz in range(minZ, maxZ + 1):
            put(0, (x + dx, 1, z + dz), 'stone')


# A: every side climbs onto a conductor, so the recomputed state equals the old one and
# vanilla passes without touching anything.
floor(10, 8, -3, 4, -3, 4)
for neighbour in [(9, 8), (11, 8), (10, 7), (10, 9)]:
    put(0, (neighbour[0], 2, neighbour[1]), 'stone')
    put(0, (neighbour[0], 3, neighbour[1]), 'redstone_wire')
put(2, (10, 2, 8), 'redstone_wire', north='up', east='up', south='up', west='up')
budPiston(4, (12, 2, 8), 'east', (0, 0, 1))
watch.extend([[10, 2, 8], [12, 2, 8], [13, 2, 8]])
interact(8, (10, 2, 8))

# B: a lone wire really is a cross, so toggling it to a dot changes every side and vanilla
# does notify the adjacent conductor. This is the control for group A.
floor(10, 20, -3, 4, -3, 3)
put(0, (11, 2, 20), 'stone')
put(2, (10, 2, 20), 'redstone_wire', north='side', east='side', south='side', west='side')
budPiston(4, (12, 2, 20), 'east', (0, 0, 1))
watch.extend([[10, 2, 20], [12, 2, 20], [13, 2, 20]])
interact(8, (10, 2, 20))

if __name__ == '__main__':
    runCapture(commands, watch, 20, 'java26_2WireShapeToggle')
