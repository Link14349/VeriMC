#!/usr/bin/env python3
"""The level's random source state, compared every tick.

Everything else in this suite compares outcomes. This scenario compares the **random source
itself**: the raw 48 bit state of `Level.random` after every frame. Two sides can only agree on
that value if they drew exactly the same number of values from the same algorithm, so it catches a
device that consumes a draw the other side does not — something no outcome comparison detects
until the divergence happens to become visible.

The scenario sets an explicit seed on both sides at tick 0 and switches off the level's own
consumers for this scenario only: mob spawning (all six spawn rules) and the weather cycle draw
from the same random every tick and are not modelled. `randomTickSpeed` is already 0 everywhere.

Covered devices: a dropper (`getRandomSlot` reservoir sampling, which draws even for a single
occupied slot), a composter (probability roll per insertion) and a note block. Blocks that draw
nothing are also present so a spurious draw would show up.
"""
from captureRedstone import runCapture, state

commands = []
watch = []

SEED = 20260909


def put(tick, pos, name, **props):
    commands.append({'tick': tick, 'pos': list(pos), 'stateId': state(name, **props)})


def clear(tick, pos):
    commands.append({'tick': tick, 'pos': list(pos), 'stateId': 0})


def floor(x, z, radius=2):
    for dx in range(-radius, radius + 1):
        for dz in range(-radius, radius + 1):
            put(0, (x + dx, 0, z + dz), 'stone')


# A dropper firing into a chest: the slot choice draws even with one occupied slot.
# It must not eject into open air — that produces an external action the replay cannot resolve.
floor(4, 4)
put(0, (4, 1, 4), 'dropper', facing='east')
put(0, (5, 1, 4), 'chest', facing='north')
commands.append({'tick': 0, 'pos': [4, 1, 4],
                 'stimulus': {'inventory': [{'slot': 3, 'item': 'minecraft:stone', 'count': 4}]}})
put(2, (3, 1, 4), 'redstone_block')
clear(6, (3, 1, 4))
put(10, (3, 1, 4), 'redstone_block')
clear(14, (3, 1, 4))
watch += [[4, 1, 4], [5, 1, 4]]

# A composter: each insertion rolls against the material's probability.
floor(12, 4)
put(0, (12, 1, 4), 'composter')
for tick, item in ((3, 'minecraft:oak_leaves'), (5, 'minecraft:oak_leaves'), (7, 'minecraft:oak_leaves'),
                   (9, 'minecraft:wheat'), (11, 'minecraft:wheat'), (13, 'minecraft:cake')):
    commands.append({'tick': tick, 'pos': [12, 1, 4], 'stimulus': {'compostItem': item}})
watch.append([12, 1, 4])

# A note block and a plain redstone chain: neither should draw anything.
floor(20, 4)
put(0, (20, 1, 4), 'note_block')
put(0, (20, 2, 4), 'redstone_wire')
put(4, (19, 2, 4), 'redstone_block')
clear(8, (19, 2, 4))
put(12, (19, 2, 4), 'redstone_block')
watch += [[20, 1, 4], [20, 2, 4]]

floor(28, 4)
put(0, (28, 1, 4), 'repeater', facing='west', delay='3')
put(0, (29, 1, 4), 'redstone_lamp')
put(2, (27, 1, 4), 'redstone_block')
clear(9, (27, 1, 4))
watch += [[28, 1, 4], [29, 1, 4]]

if __name__ == '__main__':
    runCapture(commands, watch, 20, 'java26_2RandomState', randomSeed=SEED, discardDrops=True)
