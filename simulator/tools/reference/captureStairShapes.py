#!/usr/bin/env python3
"""Original stair connection-shape scenarios.

Covers `StairBlock.getStairsShape` over all four facings and both halves: the four
inner/outer relations, the `canTakeShape` blocker that keeps a corner straight, vanilla
player placement (`getStateForPlacement`), removal of the partner, and the support-mask
consequence of an inner corner (a redstone wall torch on the newly sturdy side face).

Each arrangement sits in its own 5x5 cell, so no two arrangements share a neighbour.
"""
from captureRedstone import runCapture, state

commands = []
watch = []
HORIZONTAL = ['north', 'east', 'south', 'west']
OFFSET = {'north': (0, -1), 'east': (1, 0), 'south': (0, 1), 'west': (-1, 0)}
cellCount = 0


def clockWise(d):
    return HORIZONTAL[(HORIZONTAL.index(d) + 1) % 4]


def counterClockWise(d):
    return HORIZONTAL[(HORIZONTAL.index(d) + 3) % 4]


def opposite(d):
    return HORIZONTAL[(HORIZONTAL.index(d) + 2) % 4]


def relative(pos, direction):
    dx, dz = OFFSET[direction]
    return (pos[0] + dx, pos[1], pos[2] + dz)


def put(tick, pos, name, player=False, **props):
    command = {'tick': tick, 'pos': list(pos), 'stateId': state(name, **props)}
    if player:
        command['playerPlace'] = True
    commands.append(command)


def clear(tick, pos):
    commands.append({'tick': tick, 'pos': list(pos), 'stateId': 0})


def nextCell():
    global cellCount
    index, cellCount = cellCount, cellCount + 1
    return (4 + 5 * (index % 9), 2, 4 + 5 * (index // 9))


def arrangement(facing, half, kind, blocked=False, player=False, torch=False):
    centre = nextCell()
    for dx in (-1, 0, 1):
        for dz in (-1, 0, 1):
            put(0, (centre[0] + dx, 1, centre[2] + dz), 'stone')
    partnerDirection = facing if kind.startswith('outer') else opposite(facing)
    partnerFacing = counterClockWise(facing) if kind.endswith('left') else clockWise(facing)
    partner = relative(centre, partnerDirection)
    if blocked:
        # canTakeShape looks at the opposite side of the corner; an identical stair there
        # keeps the shape straight in vanilla.
        probe = opposite(partnerFacing) if kind.startswith('outer') else partnerFacing
        blocker = relative(centre, probe)
        put(0, blocker, 'oak_stairs', facing=facing, half=half)
        watch.append(list(blocker))
    if player:
        put(0, partner, 'oak_stairs', facing=partnerFacing, half=half)
        put(2, centre, 'oak_stairs', player=True, facing=facing, half=half)
    else:
        put(0, centre, 'oak_stairs', facing=facing, half=half)
        put(2, partner, 'oak_stairs', facing=partnerFacing, half=half)
    watch.extend([list(centre), list(partner)])
    if torch:
        # An inner corner turns the partner-facing side sturdy; the torch must survive
        # there and break again once the corner is removed.
        hook = relative(centre, partnerFacing)
        put(4, hook, 'redstone_wall_torch', facing=partnerFacing)
        watch.append(list(hook))
    clear(10, partner)


for half in ['bottom', 'top']:
    for facing in HORIZONTAL:
        for kind in ['outer_left', 'outer_right', 'inner_left', 'inner_right']:
            arrangement(facing, half, kind)
for half in ['bottom', 'top']:
    for facing in HORIZONTAL:
        for kind in ['outer_left', 'inner_left']:
            arrangement(facing, half, kind, blocked=True)
for half in ['bottom', 'top']:
    for facing in HORIZONTAL:
        for kind in ['outer_right', 'inner_right']:
            arrangement(facing, half, kind, player=True)
arrangement('north', 'bottom', 'inner_left', torch=True)
arrangement('north', 'bottom', 'inner_right', torch=True)
arrangement('east', 'top', 'inner_left', torch=True)

if __name__ == '__main__':
    runCapture(commands, watch, 16, 'java26_2StairShapes')
