#!/usr/bin/env python3
"""Original 26.2 scenarios for paired doors, environment devices and analog reads."""
from captureRedstone import commands, watch, setBlock, runCapture

commands.clear()
watch.clear()

def stimulate(tick, pos, **values):
    commands.append({'tick': tick, 'pos': list(pos), 'stimulus': values})

for x in range(1, 44):
    for z in range(1, 30):
        setBlock(0, (x, 1, z), 'stone')

# Upper-half power opens both halves. A hand operation can close a powered door.
for x, name in [(3, 'oak_door'), (9, 'iron_door'), (15, 'copper_door')]:
    setBlock(0, (x, 2, 3), name, half='lower')
    setBlock(0, (x, 3, 3), name, half='upper')
    setBlock(2, (x-1, 3, 3), 'redstone_block')
    if name != 'iron_door': commands.append({'tick': 4, 'pos': [x, 2, 3], 'interact': True})
    setBlock(6, (x-1, 3, 3), 'air')
    setBlock(8, (x, 1, 3), 'air')
    watch += [[x, 2, 3], [x, 3, 3]]
for x, name in [(23, 'oak_trapdoor'), (29, 'iron_trapdoor'), (35, 'oak_fence_gate')]:
    setBlock(0, (x, 2, 3), name)
    setBlock(2, (x-1, 2, 3), 'redstone_block')
    setBlock(6, (x-1, 2, 3), 'air')
    watch.append([x, 2, 3])

# Entity inputs count real armor stands and distinct item entities in the game.
for x, name in [(3, 'stone_pressure_plate'), (9, 'oak_pressure_plate'), (15, 'light_weighted_pressure_plate'), (21, 'heavy_weighted_pressure_plate')]:
    pos = (x, 2, 9)
    setBlock(0, pos, name)
    stimulate(1, pos, entities=1, livingEntities=0)
    stimulate(3, pos, entities=11, livingEntities=11)
    stimulate(13, pos, entities=0, livingEntities=0)
    setBlock(0, (x+1, 2, 9), 'redstone_wire')
    watch += [list(pos), [x+1, 2, 9]]

for x, facing in [(3, 'up'), (9, 'west'), (15, 'south')]:
    pos = (x, 2, 15)
    setBlock(0, pos, 'lightning_rod', facing=facing)
    stimulate(1, pos)
    stimulate(4, pos)  # Existing scheduled pulse end is not extended.
    watch.append(list(pos))

pos = (23, 2, 15)
setBlock(0, pos, 'lectern')
setBlock(0, (24, 2, 15), 'stone')
setBlock(0, (25, 2, 15), 'comparator', facing='west')
stimulate(1, pos, pages=15)
stimulate(4, pos, page=7)
stimulate(5, pos, page=14)
stimulate(10, pos, pages=1)
stimulate(14, pos, pages=0)
watch += [list(pos), [25, 2, 15]]

for x, name, props, change in [(3, 'water_cauldron', {'level': '1'}, {'level': '3'}), (9, 'beehive', {'honey_level': '0'}, {'honey_level': '5'}), (15, 'respawn_anchor', {'charges': '1'}, {'charges': '4'}), (21, 'end_portal_frame', {'eye': 'false'}, {'eye': 'true'}), (27, 'waxed_copper_golem_statue', {'copper_golem_pose': 'standing'}, {'copper_golem_pose': 'running'})]:
    setBlock(0, (x, 2, 23), name, **props)
    setBlock(0, (x+1, 2, 23), 'stone')
    setBlock(0, (x+2, 2, 23), 'comparator', facing='west')
    setBlock(5, (x, 2, 23), name, **change)
    watch += [[x, 2, 23], [x+2, 2, 23]]

runCapture(commands, watch, endTick=28, fixtureName='java26_2Devices')
