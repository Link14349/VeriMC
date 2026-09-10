#!/usr/bin/env python3
"""A diode that loses its support is removed inside `neighborChanged`, not later.

`DiodeBlock.neighborChanged` drops and removes the diode as soon as `canSurvive` fails and
then issues `updateNeighborsAt(pos.relative(d), this)` for all six directions - 36 extra
notifications carrying the diode block. Those reach positions two blocks away from the
diode, which neither the removal `setBlock` nor `updateNeighborsInFront` covers.
"""
from captureRedstone import runCapture, state

commands = []
watch = []


def put(tick, pos, name, **props):
    commands.append({'tick': tick, 'pos': list(pos), 'stateId': state(name, **props)})


def clear(tick, pos):
    commands.append({'tick': tick, 'pos': list(pos), 'stateId': 0})


def group(x, z, diode, breakSupport, powered=False):
    for dx in range(-4, 4):
        for dz in range(-2, 6):
            put(0, (x + dx, 1, z + dz), 'stone')
    put(0, (x, 2, z), diode, facing='east')
    watch.append([x, 2, z])
    # The junction sits two blocks south of the diode, outside every other notification set.
    junction = (x, 2, z + 2)
    put(0, (x, 2, z + 1), 'rail')
    put(0, junction, 'rail')
    put(0, (x, 2, z + 3), 'rail')
    put(0, (x + 1, 2, z + 2), 'rail')
    watch.extend([[x, 2, z + 1], list(junction), [x, 2, z + 3], [x + 1, 2, z + 2]])
    if powered:
        # A driven diode loses its support mid-cycle, so the output chain is exercised too.
        put(0, (x + 1, 2, z), 'redstone_block')
        put(0, (x - 1, 2, z), 'redstone_wire')
        put(0, (x - 2, 2, z), 'redstone_lamp')
        watch.extend([[x - 1, 2, z], [x - 2, 2, z]])
    if breakSupport:
        clear(10, (x, 1, z))


group(8, 8, 'repeater', True)
group(8, 20, 'repeater', False)
group(28, 8, 'comparator', True)
group(28, 20, 'comparator', False)
group(8, 32, 'repeater', True, powered=True)
group(28, 32, 'comparator', True, powered=True)

if __name__ == '__main__':
    runCapture(commands, watch, 20, 'java26_2DiodeSupportBreak', discardDrops=True)
