#!/usr/bin/env python3
"""Losing support only removes a block on the direction each block class actually checks.

26.2 writes the direction into every `updateShape`: `FaceAttachedHorizontalDirectionalBlock`
checks `getConnectedDirection(state).getOpposite()`, torches, diodes, pressure plates, wire
and the lower door half check `DOWN`, wall torches / piston heads / tripwire hooks check
`FACING.getOpposite()`, and only `CarpetBlock` checks every direction.

Each group places an unsupported block, then pokes it from a direction the class does *not*
check (vanilla keeps it), and finally from the direction it does check (vanilla removes it).
This scenario was found by `fuzzRedstone.py`, which shrank a 1,162 command round down to the
two-command ceiling-lever case.
"""
from captureRedstone import runCapture, state

commands = []
watch = []


def put(tick, pos, name, **props):
    commands.append({'tick': tick, 'pos': list(pos), 'stateId': state(name, **props)})


def clear(tick, pos):
    commands.append({'tick': tick, 'pos': list(pos), 'stateId': 0})


def group(x, z, name, props, checkedOffset, unchecked=(0, 0, 1), support=None):
    """Place an unsupported block, poke it sideways, then poke from the checked direction."""
    for dx in range(-2, 3):
        for dz in range(-2, 3):
            put(0, (x + dx, 0, z + dz), 'stone')
    target = (x, 2, z)
    if support:
        put(0, (x + support[0], 2 + support[1], z + support[2]), 'stone')
    put(0, target, name, **props)
    watch.append(list(target))
    # 2 gt: a neighbour appears in a direction the class does not check.
    poke = (x + unchecked[0], 2 + unchecked[1], z + unchecked[2])
    put(2, poke, 'white_wool')
    watch.append(list(poke))
    # 6 gt: the same neighbour disappears, still not the checked direction.
    clear(6, poke)
    # 10 gt: something changes in the checked direction, which is what vanilla reacts to.
    checked = (x + checkedOffset[0], 2 + checkedOffset[1], z + checkedOffset[2])
    put(10, checked, 'white_wool')
    clear(11, checked)
    watch.append(list(checked))


# Levers and buttons: the checked direction is the opposite of the attachment face.
group(5, 5, 'lever', {'face': 'ceiling', 'facing': 'south'}, (0, 1, 0))
group(11, 5, 'lever', {'face': 'floor', 'facing': 'south'}, (0, -1, 0))
group(17, 5, 'lever', {'face': 'wall', 'facing': 'east'}, (-1, 0, 0), unchecked=(0, 0, 1))
group(23, 5, 'stone_button', {'face': 'ceiling', 'facing': 'south'}, (0, 1, 0))
# Standing torch, diode, pressure plate, wire and the lower door half all check DOWN.
group(29, 5, 'redstone_torch', {}, (0, -1, 0))
group(35, 5, 'repeater', {'facing': 'east'}, (0, -1, 0))
group(41, 5, 'stone_pressure_plate', {}, (0, -1, 0))
group(5, 13, 'redstone_wire', {}, (0, -1, 0))
group(11, 13, 'oak_door', {'facing': 'east', 'half': 'lower', 'hinge': 'left'}, (0, -1, 0))
# Wall torch and tripwire hook check the direction opposite their facing.
group(17, 13, 'redstone_wall_torch', {'facing': 'east'}, (-1, 0, 0), unchecked=(0, 0, 1))
group(23, 13, 'tripwire_hook', {'facing': 'east'}, (-1, 0, 0), unchecked=(0, 0, 1))
# Carpet is the one class that checks every direction, so the sideways poke already removes it.
group(29, 13, 'white_carpet', {}, (0, -1, 0))

def fenceGate(x, z, facing, look):
    """Opening a fence gate from behind flips its facing toward the player."""
    for dx in range(-2, 3):
        for dz in range(-2, 3):
            put(0, (x + dx, 0, z + dz), 'stone')
    pos = (x, 1, z)
    put(0, pos, 'oak_fence_gate', facing=facing)
    watch.append(list(pos))
    commands.append({'tick': 2, 'pos': list(pos), 'interact': True, 'playerFacing': look})
    commands.append({'tick': 6, 'pos': list(pos), 'interact': True, 'playerFacing': look})
    commands.append({'tick': 10, 'pos': list(pos), 'interact': True})


# Opened from behind (look is opposite the gate facing) the gate turns; from any other angle
# it keeps its facing. The last interact carries no look direction at all.
fenceGate(5, 21, 'north', 'south')
fenceGate(11, 21, 'north', 'north')
fenceGate(17, 21, 'east', 'west')
fenceGate(23, 21, 'west', 'north')

if __name__ == '__main__':
    runCapture(commands, watch, 14, 'java26_2SupportDirection', discardDrops=True, updateTraceLimit=200000)
