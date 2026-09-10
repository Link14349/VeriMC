#!/usr/bin/env python3
"""`RepeaterBlock.updateShape` refreshes LOCKED for every axis except the facing axis.

That includes the Y axis, so a vertical shape update recomputes a stale LOCKED value.
The stale value is produced by writing the block state directly, which is what the editor
does; no redstone-only history is known that leaves LOCKED stale (see the issue comment).
"""
from captureRedstone import runCapture, state

commands = []
watch = []


def put(tick, pos, name, **props):
    commands.append({'tick': tick, 'pos': list(pos), 'stateId': state(name, **props)})


def group(x, z, updateAt, sideDiode):
    for dx in range(-3, 3):
        for dz in range(-3, 3):
            put(0, (x + dx, 1, z + dz), 'stone')
    repeater = (x, 2, z)
    if sideDiode:
        # A real side repeater whose output points into the tested one, so LOCKED is true.
        put(0, (x, 2, z - 1), 'repeater', facing='north')
        put(0, (x, 2, z - 2), 'redstone_block')
        watch.append([x, 2, z - 1])
    put(0, repeater, 'repeater', facing='east', locked='true')
    watch.append(list(repeater))
    put(4, updateAt, 'stone')
    watch.append(list(updateAt))


# Vertical shape update on a stale LOCKED value.
group(10, 8, (10, 3, 8), False)
# Horizontal control: both engines already refresh here.
group(10, 16, (10, 2, 17), False)
# Vertical update while the lock is genuine, so the recomputed value stays true.
group(10, 24, (10, 3, 24), True)

if __name__ == '__main__':
    runCapture(commands, watch, 12, 'java26_2RepeaterLockRefresh')
