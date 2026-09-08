#!/usr/bin/env python3
"""Comparators read an item frame hanging on the conductor in front of them.

`ComparatorBlock.getInputSignal` only looks at the second cell when the direct signal is
below 15 and the first cell is a redstone conductor; it then takes the larger of the frame
reading and that cell's own analog output. `getItemFrame` filters by
`entity.getDirection() == comparator facing` and only accepts exactly one candidate.

The frames are summoned by the capture harness; this is a bounded entity input, not a full
entity world simulation.
"""
from captureRedstone import runCapture, state

commands = []
watch = []


def put(tick, pos, name, **props):
    commands.append({'tick': tick, 'pos': list(pos), 'stateId': state(name, **props)})


def stimulate(tick, pos, **values):
    commands.append({'tick': tick, 'pos': list(pos), 'stimulus': values})


def frames(tick, mount, *entries):
    stimulate(tick, mount, itemFrames=[
        {'facing': facing, 'rotation': rotation, 'hasItem': hasItem} for facing, rotation, hasItem in entries])


def group(x, z, firstCell='stone', farCell=None, lever=False):
    for dx in range(-3, 5):
        for dz in range(-2, 3):
            put(0, (x + dx, 1, z + dz), 'stone')
    put(0, (x, 2, z), 'comparator', facing='east')
    put(0, (x + 1, 2, z), firstCell)
    if farCell:
        put(0, (x + 2, 2, z), farCell)
    put(0, (x - 1, 2, z), 'redstone_wire')
    if lever:
        put(0, (x + 1, 3, z), 'lever', face='floor', facing='north', powered='true')
    watch.extend([[x, 2, z], [x - 1, 2, z]])
    return (x + 1, 2, z)


# 1: rotation sweep, item toggle and removal on a plain conductor.
mount = group(6, 6)
frames(2, mount, ('east', 0, False))
frames(4, mount, ('east', 0, True))
frames(6, mount, ('east', 3, True))
frames(8, mount, ('east', 7, True))
frames(10, mount)

# 2: two candidates facing the same way are refused; removing one restores the reading.
mount = group(6, 12)
frames(2, mount, ('east', 3, True), ('east', 5, True))
frames(6, mount, ('east', 5, True))

# 3: only frames whose direction matches the comparator facing are candidates.
mount = group(6, 18)
frames(2, mount, ('north', 7, True))
frames(6, mount, ('north', 7, True), ('east', 7, True))

# 4: the first cell must be a redstone conductor.
mount = group(6, 24, firstCell='glass')
frames(2, mount, ('east', 7, True))

# 5: a direct signal of 15 short-circuits before the frame is ever consulted.
mount = group(6, 30, lever=True)
frames(2, mount, ('east', 3, True))

# 6: the frame competes with the second cell's own analog output; vanilla takes the larger.
mount = group(6, 36, farCell='hopper')
stimulate(1, (8, 2, 36), inventory=[{'slot': 0, 'item': 'stone', 'count': 64}])
frames(2, mount, ('east', 0, True))
frames(6, mount, ('east', 7, True))
frames(10, mount, ('east', 0, False))

if __name__ == '__main__':
    runCapture(commands, watch, 16, 'java26_2ItemFrameComparator', discardDrops=True)
