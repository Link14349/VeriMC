#!/usr/bin/env python3
"""Blocks moved by a piston still run `affectNeighborsAfterRemoval` in vanilla.

`LevelChunk.setBlockState` calls it whenever `(flags & 1) != 0 || movedByPiston`, passing
`movedByPiston` down so each block decides. Lightning rods and observers do **not** check
it, so pushing a powered one still notifies its output side. The three-way rail reads the
notifying block, which makes the extra notification observable.
"""
from captureRedstone import runCapture, state

commands = []
watch = []


def put(tick, pos, name, **props):
    commands.append({'tick': tick, 'pos': list(pos), 'stateId': state(name, **props)})


def stimulate(tick, pos, **values):
    commands.append({'tick': tick, 'pos': list(pos), 'stimulus': values})


def group(x, z, powerRail):
    """Powered lightning rod pushed away from a three-way rail junction.

    The rod faces east, so `updateNeighbours` notifies the neighbours of the block one
    step west of it - two blocks from the rod, outside the piston's own update set.
    """
    for dx in range(-5, 3):
        for dz in range(-3, 3):
            put(0, (x + dx, 1, z + dz), 'stone')
    rod = (x, 2, z)
    put(0, rod, 'lightning_rod', facing='east')
    watch.append(list(rod))
    junction = (x - 2, 2, z)
    put(0, junction, 'rail', shape='east_west')
    for neighbour in [(x - 2, 2, z - 1), (x - 2, 2, z + 1), (x - 3, 2, z)]:
        put(0, neighbour, 'rail')
        watch.append(list(neighbour))
    watch.append(list(junction))
    put(0, (x, 2, z - 1), 'piston', facing='south')
    watch.extend([[x, 2, z - 1], [x, 2, z + 1]])
    # 4 gt: the rod turns on and notifies the junction while it is unpowered.
    stimulate(4, rod)
    if powerRail:
        # 6 gt: the junction becomes powered, but a redstone block placement notifies with
        # the old block (air), which is not a signal source, so the shape does not change.
        put(6, (x - 2, 3, z), 'redstone_block')
    # 7 gt: power the piston from behind so the still-powered rod is pushed at 8 gt.
    put(7, (x, 2, z - 2), 'redstone_block')


# Without the extra power change both notifications agree, so this group is a control.
group(8, 8, False)
# With it, the removal-time notification is the only thing that can flip the shape.
group(8, 20, True)

if __name__ == '__main__':
    runCapture(commands, watch, 30, 'java26_2PistonRemovalCallback')
