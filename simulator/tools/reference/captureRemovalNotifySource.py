#!/usr/bin/env python3
"""`affectNeighborsAfterRemoval` passes the removed block itself as the notifying block.

`LeverBlock.updateNeighbours` and `DiodeBlock.updateNeighborsInFront` both notify with
`this`, not with whatever now occupies the position. The three-way rail junction reads the
notifying block, and it sits one block beyond the notified position, so only these calls
can reach it.
"""
from captureRedstone import runCapture, state

commands = []
watch = []


def put(tick, pos, name, **props):
    commands.append({'tick': tick, 'pos': list(pos), 'stateId': state(name, **props)})


def clear(tick, pos):
    commands.append({'tick': tick, 'pos': list(pos), 'stateId': 0})


def junction(x, z):
    """Three potential connections, so `RailBlock.updateState` can act."""
    put(0, (x, 2, z), 'rail', shape='east_west')
    for neighbour in [(x, 2, z - 1), (x, 2, z + 1), (x - 1, 2, z)]:
        put(0, neighbour, 'rail')
        watch.append(list(neighbour))
    watch.append([x, 2, z])


def leverGroup(x, z, powered):
    for dx in range(-5, 3):
        for dz in range(-2, 3):
            put(0, (x + dx, 1, z + dz), 'stone')
    put(0, (x - 1, 2, z), 'stone')
    junction(x - 2, z)
    put(0, (x, 2, z), 'lever', face='wall', facing='east', powered='true' if powered else 'false')
    watch.append([x, 2, z])
    clear(8, (x, 2, z))


def diodeGroup(x, z, diode):
    for dx in range(-5, 3):
        for dz in range(-2, 3):
            put(0, (x + dx, 1, z + dz), 'stone')
    junction(x - 2, z)
    put(0, (x, 2, z), diode, facing='east')
    watch.append([x, 2, z])
    # The diode's own onPlace already notified the junction while it was unpowered.
    # Powering it with a plain setBlock notifies with the old block (air), which is not a
    # signal source, so only the removal notification can change the shape again.
    put(4, (x - 2, 3, z), 'redstone_block')
    clear(8, (x, 2, z))


# A powered lever notifies its attachment block's neighbours with the lever block.
leverGroup(12, 8, True)
# Unpowered levers skip the call entirely in vanilla, so this group must not change.
leverGroup(12, 18, False)
# Diodes always notify their output side on removal, again with the diode block.
diodeGroup(12, 28, 'repeater')
diodeGroup(12, 38, 'comparator')

if __name__ == '__main__':
    runCapture(commands, watch, 16, 'java26_2RemovalNotifySource', discardDrops=True)
