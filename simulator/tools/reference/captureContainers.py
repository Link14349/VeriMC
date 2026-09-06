#!/usr/bin/env python3
"""Container capacities, double-chest ordering, analog updates and opening pulses."""
from captureRedstone import commands, watch, setBlock, runCapture

commands.clear()
watch.clear()

def inputAt(tick, pos, **values):
    commands.append({'tick': tick, 'pos': list(pos), 'stimulus': values})

def slot(index, name, count):
    return {'slot': index, 'item': 'minecraft:' + name, 'count': count}

for x in range(1, 35):
    for z in range(1, 25):
        setBlock(0, (x, 1, z), 'stone')
for x, name in [(3, 'chest'), (9, 'barrel'), (15, 'trapped_chest')]:
    pos = (x, 2, 3)
    setBlock(0, pos, name)
    setBlock(0, (x, 2, 4), 'comparator', facing='north')
    inputAt(1, pos, inventory=[slot(0, 'stone', 64)])
    inputAt(4, pos, inventory=[slot(1, 'wooden_sword', 1), slot(2, 'ender_pearl', 16)])
    inputAt(7, pos, inventory=[slot(i, 'stone', 64) for i in range(27)])
    inputAt(10, pos, inventory=[slot(i, 'stone', 0) for i in range(27)])
    watch += [list(pos), [x, 2, 4]]

# Right half precedes left half; explicit raw paired states avoid player placement.
setBlock(0, (3, 2, 10), 'chest', type='left', facing='north')
setBlock(0, (4, 2, 10), 'chest', type='right', facing='north')
for x in [3, 4]:
    setBlock(0, (x, 2, 11), 'comparator', facing='north')
    watch += [[x, 2, 10], [x, 2, 11]]
inputAt(1, (3, 2, 10), inventory=[slot(i, 'stone', 64) for i in range(27)])
setBlock(5, (3, 3, 10), 'stone')
setBlock(8, (3, 3, 10), 'air')
inputAt(9, (4, 2, 10), inventory=[slot(27, 'wooden_sword', 1)])
setBlock(12, (4, 2, 10), 'air')

# A short opening interval avoids introducing a simulated player AI loop.
for x, name in [(3, 'barrel'), (10, 'trapped_chest')]:
    pos = (x, 2, 18)
    setBlock(0, pos, name)
    setBlock(0, (x+1, 2, 18), 'redstone_wire')
    inputAt(1, pos, viewers=1)
    inputAt(2, pos, viewers=3)
    inputAt(3, pos, viewers=0)
    watch += [list(pos), [x+1, 2, 18]]

runCapture(commands, watch, endTick=16, fixtureName='java26_2Containers')
