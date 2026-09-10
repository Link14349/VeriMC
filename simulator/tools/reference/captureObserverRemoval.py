#!/usr/bin/env python3
"""Removing a powered observer only notifies its output side when a tick is still queued.

`ObserverBlock.affectNeighborsAfterRemoval` requires
`POWERED && level.getBlockTicks().hasScheduledTick(pos, this)`, and the query has to use
the removed block's own type. A quasi-connected piston that was never notified acts as the
detector: it only extends if the notification actually arrives.
"""
from captureRedstone import runCapture, state

commands = []
watch = []


def put(tick, pos, name, **props):
    commands.append({'tick': tick, 'pos': list(pos), 'stateId': state(name, **props)})


def clear(tick, pos):
    commands.append({'tick': tick, 'pos': list(pos), 'stateId': 0})


def detector(tick, x, z):
    """A retracted piston plus a diagonal power source that never notifies it."""
    put(tick, (x - 2, 2, z), 'piston', facing='west')
    put(tick, (x - 2, 3, z + 1), 'redstone_block')


def group(x, z, mode):
    for dx in range(-4, 3):
        for dz in range(-1, 3):
            put(0, (x + dx, 1, z + dz), 'stone')
    observer = (x, 2, z)
    put(0, observer, 'observer', facing='east')
    watch.extend([list(observer), [x - 2, 2, z], [x - 3, 2, z]])
    if mode == 'stale':
        # Same block, different state: no onPlace reset, and no tick is ever scheduled.
        detector(2, x, z)
        put(4, observer, 'observer', facing='east', powered='true')
        clear(6, observer)
    elif mode == 'queued':
        # A normal pulse; the detector is already in place, so the pulse itself trips it.
        detector(2, x, z)
        put(4, (x + 1, 2, z), 'stone')
        clear(7, observer)
    elif mode == 'lateDetector':
        # The detector appears after the pulse, so only the removal can trip it.
        put(4, (x + 1, 2, z), 'stone')
        detector(6, x, z)
        clear(7, observer)


group(10, 8, 'stale')
group(10, 20, 'queued')
group(10, 32, 'lateDetector')


def replaceGroup(x, z):
    """Remove and immediately replace the observer while its off tick is still queued."""
    for dx in range(-3, 3):
        for dz in range(-1, 2):
            put(0, (x + dx, 1, z + dz), 'stone')
    observer = (x, 2, z)
    put(0, observer, 'observer', facing='east')
    put(0, (x - 1, 2, z), 'redstone_lamp')
    watch.extend([list(observer), [x - 1, 2, z]])
    put(4, (x + 1, 2, z), 'stone')
    clear(7, observer)
    put(7, observer, 'observer', facing='east')


replaceGroup(10, 44)

if __name__ == '__main__':
    runCapture(commands, watch, 20, 'java26_2ObserverRemoval', discardDrops=True)
