#!/usr/bin/env python3
"""Native hopper chains, ticker order, locking, failed merges and idle wakeups."""
from captureRedstone import commands, watch, setBlock, runCapture

commands.clear()
watch.clear()

def inventory(tick, pos, stacks):
    commands.append({'tick': tick, 'pos': list(pos), 'stimulus': {'inventory': [
        {'slot': slot, 'item': 'minecraft:' + item, 'count': count} for slot, item, count in stacks]}})

# Same chain in opposite registration orders, crossing chunk boundaries.
for z, reverse in [(3, False), (8, True)]:
    for x in (range(18, 11, -1) if reverse else range(12, 19)):
        setBlock(0, (x, 2, z), 'hopper', facing='east')
        watch.append([x, 2, z])
    setBlock(0, (19, 2, z), 'barrel')
    watch.append([19, 2, z])
    inventory(0, (12, 2, z), [(0, 'stone', 12)])
    setBlock(11, (15, 3, z), 'redstone_block')
    setBlock(22, (15, 3, z), 'air')
    # Removal/replacement creates a new ticker at the end of the order.
    # Clear first: block destruction otherwise creates randomly scattered item
    # entities, which belong to the separate explicit drop-input scenario.
    inventory(30, (14, 2, z), [(0, 'stone', 0)])
    setBlock(30, (14, 2, z), 'air')
    setBlock(34, (14, 2, z), 'hopper', facing='east')

# Pull and eject in the same tick; locked lower hopper can still receive items.
for y in [3, 2]:
    setBlock(0, (4, y, 14), 'hopper', facing='down')
    watch.append([4, y, 14])
setBlock(0, (4, 1, 14), 'barrel')
setBlock(0, (4, 4, 14), 'chest')
watch += [[4, 1, 14], [4, 4, 14]]
inventory(0, (4, 4, 14), [(0, 'ender_pearl', 16), (1, 'wooden_sword', 1)])
setBlock(10, (3, 2, 14), 'redstone_block')
setBlock(24, (3, 2, 14), 'air')

# Full output sleeps, then inventory edit wakes it; blocked chest still accepts.
setBlock(0, (4, 2, 22), 'hopper', facing='east')
setBlock(0, (5, 2, 22), 'chest')
setBlock(0, (5, 3, 22), 'stone')
inventory(0, (5, 2, 22), [(i, 'stone', 64) for i in range(27)])
inventory(0, (4, 2, 22), [(0, 'ender_pearl', 3), (1, 'stone', 3)])
inventory(9, (5, 2, 22), [(0, 'stone', 63)])
inventory(20, (5, 2, 22), [(1, 'stone', 0)])
inventory(36, (4, 2, 22), [(0, 'stone', 2)])
watch += [[4, 2, 22], [5, 2, 22]]

# A sleeping empty hopper must wake on a newly placed or filled input container.
setBlock(0, (4, 2, 30), 'hopper', facing='east')
setBlock(0, (5, 2, 30), 'barrel')
setBlock(12, (4, 3, 30), 'barrel')
inventory(15, (4, 3, 30), [(0, 'stone', 2)])
inventory(40, (4, 3, 30), [(0, 'stone', 2)])
watch += [[4, 2, 30], [5, 2, 30], [4, 3, 30]]

runCapture(commands, watch, endTick=64, fixtureName='java26_2Hoppers')
