#!/usr/bin/env python3
"""The pending block tick queue itself, compared entry by entry every tick.

The intra-tick trace covers neighbour and shape updates. This scenario covers the other queue:
`LevelTicks`. Every frame records the whole pending set — position, block, trigger tick relative
to now, and priority — sorted by vanilla's own `ScheduledTick.DRAIN_ORDER`. The sub-tick serial
number itself is vanilla-internal and is not reported; only the order it produces is.

Block events are recorded the same way. They are normally empty at frame time because
`runBlockEvents` drains them inside the same tick; the field exists so that a scenario where they
are rescheduled would be caught rather than silently ignored.

Covered: repeaters at all four delays and both priorities, comparators, torches, observers,
pistons, and a burst that schedules many ticks in one game tick so the ordering among equal
trigger times is exercised.
"""
from captureRedstone import runCapture, state

commands = []
watch = []


def put(tick, pos, name, **props):
    commands.append({'tick': tick, 'pos': list(pos), 'stateId': state(name, **props)})


def clear(tick, pos):
    commands.append({'tick': tick, 'pos': list(pos), 'stateId': 0})


def floor(x, z, radius=2):
    for dx in range(-radius, radius + 1):
        for dz in range(-radius, radius + 1):
            put(0, (x + dx, 0, z + dz), 'stone')


# Repeaters at every delay: four different trigger ticks queued from one source change.
floor(4, 4, 3)
put(0, (2, 1, 4), 'redstone_block')
for index, delay in enumerate(('1', '2', '3', '4')):
    put(0, (3, 1, 4 + index), 'redstone_wire')
    put(0, (4, 1, 4 + index), 'repeater', facing='west', delay=delay)
    put(0, (5, 1, 4 + index), 'redstone_lamp')
    watch += [[4, 1, 4 + index], [5, 1, 4 + index]]
clear(6, (2, 1, 4))

# Torch, comparator and observer: different priorities and delays in the same queue.
floor(12, 4, 3)
put(0, (12, 1, 4), 'redstone_torch')
put(0, (12, 2, 4), 'redstone_lamp')
put(0, (11, 0, 4), 'redstone_block')
clear(8, (11, 0, 4))
watch += [[12, 1, 4], [12, 2, 4]]
put(0, (12, 1, 7), 'comparator', facing='west')
put(0, (11, 1, 7), 'redstone_block')
put(0, (13, 1, 7), 'redstone_lamp')
clear(10, (11, 1, 7))
watch += [[12, 1, 7], [13, 1, 7]]
put(0, (12, 1, 10), 'observer', facing='west')
put(0, (11, 1, 10), 'stone')
clear(4, (11, 1, 10))
put(12, (11, 1, 10), 'stone')
watch += [[12, 1, 10], [13, 1, 10]]

# A piston: its block event is added and drained inside one tick.
floor(20, 4, 3)
put(0, (20, 1, 4), 'piston', facing='east')
put(0, (21, 1, 4), 'stone')
put(2, (19, 1, 4), 'redstone_block')
clear(14, (19, 1, 4))
watch += [[20, 1, 4], [21, 1, 4], [22, 1, 4]]

# A burst: one redstone block change powers a wire line that queues eight repeater ticks at the
# same trigger tick, so their relative order comes only from priority and insertion order.
floor(28, 10, 5)
put(0, (27, 1, 10), 'redstone_block')
for index in range(8):
    dz = index - 4
    put(0, (28, 1, 10 + dz), 'redstone_wire')
    put(0, (29, 1, 10 + dz), 'repeater', facing='west', delay='1')
    watch.append([29, 1, 10 + dz])
clear(16, (27, 1, 10))

if __name__ == '__main__':
    runCapture(commands, watch, 24, 'java26_2ScheduledQueue', watchScheduled=True, discardDrops=True)
