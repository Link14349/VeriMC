#!/usr/bin/env python3
"""Neighbour notifications carry the originating block, and three-way rails read it.

`RailBlock.updateState` only re-runs `updateDir` when the notifying block is a signal
source and the rail has three potential connections; `DoorBlock.neighborChanged` skips
when the notifying block is the door itself. Redstone wire, redstone torch and piston
movement all pass an explicit source block that is not the notified position's own block.
"""
from captureRedstone import runCapture, state

commands = []
watch = []


def put(tick, pos, name, **props):
    commands.append({'tick': tick, 'pos': list(pos), 'stateId': state(name, **props)})


def clear(tick, pos):
    commands.append({'tick': tick, 'pos': list(pos), 'stateId': 0})


def floor(x, z, minX, maxX):
    for dx in range(minX, maxX + 1):
        for dz in range(-2, 3):
            put(0, (x + dx, 1, z + dz), 'stone')


def junction(x, z):
    """A normal rail with three potential connections, so `updateState` can act."""
    rail = (x, 2, z)
    put(0, rail, 'rail', shape='east_west')
    for neighbour in [(x, 2, z - 1), (x, 2, z + 1), (x + 1, 2, z)]:
        put(0, neighbour, 'rail')
        watch.append(list(neighbour))
    watch.append(list(rail))
    return rail


def wireJunction(x, z, gap):
    """A wire `gap` blocks west of the junction, with conductors in between."""
    floor(x, z, -gap - 2, 2)
    junction(x, z)
    for step in range(1, gap):
        put(0, (x - step, 2, z), 'stone')
    wire = (x - gap, 2, z)
    put(0, wire, 'redstone_wire')
    watch.append(list(wire))
    put(10, (x - gap - 1, 2, z), 'redstone_block')
    clear(20, (x - gap - 1, 2, z))
    clear(30, wire)


# Distance 1: the notified position is the wire itself, so both engines agree.
wireJunction(10, 6, 1)
# Distance 2 and 3: the notification passes through a conductor, so the source block matters.
wireJunction(10, 16, 2)
wireJunction(10, 26, 3)

# A redstone torch placed and removed two blocks away uses the same explicit-source path.
floor(30, 6, -4, 2)
junction(30, 6)
put(0, (29, 2, 6), 'stone')
put(10, (28, 2, 6), 'redstone_torch')
clear(20, (28, 2, 6))

# Piston movement notifies the emptied position with the block that used to be there.
# The piston must be powered from behind: a block directly in front never powers it.
floor(30, 16, -3, 3)
junction(30, 16)
put(0, (30, 3, 16), 'redstone_block')
put(0, (29, 3, 16), 'piston', facing='east')
put(10, (28, 3, 16), 'redstone_block')
watch.extend([[30, 3, 16], [31, 3, 16], [29, 3, 16]])

# The upper door half is notified from the lower half's position; vanilla passes the wire.
floor(30, 26, -3, 3)
put(0, (31, 2, 26), 'oak_door', facing='east', half='lower', hinge='left')
put(0, (31, 3, 26), 'oak_door', facing='east', half='upper', hinge='left')
put(0, (30, 2, 26), 'redstone_wire')
watch.extend([[31, 2, 26], [31, 3, 26], [30, 2, 26]])
put(10, (29, 2, 26), 'redstone_block')
clear(20, (29, 2, 26))

if __name__ == '__main__':
    runCapture(commands, watch, 40, 'java26_2RailNotificationSource')
