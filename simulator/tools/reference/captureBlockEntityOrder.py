#!/usr/bin/env python3
"""The block entity tick order itself, compared every tick.

`Level.tickBlockEntities` iterates `Level.blockEntityTickers` in list order, so that list **is**
the execution order. It is a plain `List`, read reflectively; removed entries are skipped, the
same filter the game applies while iterating.

The kernel models the same thing as a monotonic registration rank per block entity. This scenario
checks that the two agree while block entities are added, removed and re-added, which is exactly
where a "register in placement order" model can drift from vanilla's list.

Covered: hoppers and bells (block entities with a ticker), placed in several orders, one removed
and re-placed mid-run, and one replaced by a different type at the same position. An idle jukebox
is present as a negative case: it has a block entity but no ticker, so it must **not** appear.

No daylight detector: its reading depends on the world clock, which differs between captures, so
it would make the whole scenario non-reproducible. Sky light in this world is not blocked by the
test structure's barrier shell.
"""
from captureRedstone import runCapture, state

commands = []
watch = []


def put(tick, pos, name, **props):
    commands.append({'tick': tick, 'pos': list(pos), 'stateId': state(name, **props)})


def clear(tick, pos):
    commands.append({'tick': tick, 'pos': list(pos), 'stateId': 0})


for x in range(1, 20):
    for z in range(1, 12):
        put(0, (x, 0, z), 'stone')

# Placed at tick 0 in this order; the list order must follow it.
for pos, name, props in [
        ((2, 1, 2), 'hopper', {'facing': 'down'}),
        ((4, 1, 2), 'bell', {'attachment': 'floor', 'facing': 'east'}),
        ((6, 1, 2), 'jukebox', {}),
        ((8, 1, 2), 'bell', {'attachment': 'floor', 'facing': 'north'}),
        ((10, 1, 2), 'hopper', {'facing': 'east'})]:
    put(0, pos, name, **props)
    watch.append(list(pos))

# Added later, so they land at the end of the list.
put(3, (2, 1, 6), 'hopper', facing='down')
put(5, (4, 1, 6), 'bell', attachment='floor', facing='south')
watch += [[2, 1, 6], [4, 1, 6]]

# Removed and re-placed: vanilla drops the removed ticker and appends a fresh one.
clear(8, (4, 1, 2))
put(11, (4, 1, 2), 'bell', attachment='floor', facing='east')

# Replaced in place by a different block entity type.
clear(9, (6, 1, 2))
put(9, (6, 1, 2), 'hopper', facing='north')

# Two more added in the same tick, so their relative order comes from the command order.
put(13, (6, 1, 6), 'hopper', facing='down')
put(13, (8, 1, 6), 'bell', attachment='floor', facing='north')
watch += [[6, 1, 6], [8, 1, 6]]

if __name__ == '__main__':
    runCapture(commands, watch, 18, 'java26_2BlockEntityOrder', watchBlockEntityOrder=True, discardDrops=True)
