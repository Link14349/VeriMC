#!/usr/bin/env python3
"""A block that finishes a piston move runs the full `Block.updateFromNeighbourShapes`.

`PistonMovingBlockEntity.finalTick` folds `updateShape` over all six directions before
placing the moved state, so a landed stair recomputes its corner shape, a landed note
block re-reads its instrument, and a landed observer schedules its pulse.
"""
from captureRedstone import runCapture, state

commands = []
watch = []


def put(tick, pos, name, **props):
    commands.append({'tick': tick, 'pos': list(pos), 'stateId': state(name, **props)})


def base(x, z, floorName='stone'):
    for dx in range(-3, 5):
        for dz in range(-2, 3):
            put(0, (x + dx, 1, z + dz), floorName)
    put(0, (x - 1, 2, z), 'piston', facing='east')
    put(4, (x - 2, 2, z), 'redstone_block')
    watch.append([x - 1, 2, z])


# Stairs: pushed upwards, so the only neighbour that changes afterwards is the piston head
# below it. A vertical shape update never recomputes SHAPE, so nothing else can fix it up.
for dx in range(-3, 3):
    for dz in range(-2, 3):
        put(0, (10 + dx, 1, 8 + dz), 'stone')
put(0, (10, 1, 8), 'piston', facing='up')
put(0, (10, 2, 8), 'oak_stairs', facing='north', half='bottom')
put(0, (10, 3, 9), 'oak_stairs', facing='west', half='bottom')
put(4, (9, 1, 8), 'redstone_block')
watch.extend([[10, 1, 8], [10, 2, 8], [10, 3, 8], [10, 3, 9]])

# Note block: the block underneath the landing position selects a different instrument.
base(10, 20)
put(0, (11, 1, 20), 'gold_block')
put(0, (10, 2, 20), 'note_block')
watch.extend([[10, 2, 20], [11, 2, 20]])

# Observer: every landing runs an update from its observed face, which starts a pulse.
base(10, 32)
put(0, (10, 2, 32), 'observer', facing='east')
put(0, (11, 2, 31), 'redstone_lamp')
watch.extend([[10, 2, 32], [11, 2, 32], [11, 2, 31], [12, 2, 32]])

if __name__ == '__main__':
    runCapture(commands, watch, 20, 'java26_2PistonLandingShape', discardDrops=True)
