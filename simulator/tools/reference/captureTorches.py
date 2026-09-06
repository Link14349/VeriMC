#!/usr/bin/env python3
"""World-owned torch burnout history across removal and replacement."""
from captureRedstone import commands, watch, setBlock, runCapture

commands.clear()
watch.clear()
for x in [3, 12]:
    setBlock(0, (x, 1, 3), 'stone')
    setBlock(0, (x, 2, 3), 'redstone_torch')
    setBlock(0, (x, 3, 3), 'redstone_lamp')
    for cycle in range(7):
        setBlock(cycle * 4, (x-1, 1, 3), 'lever', face='wall', facing='west', powered='true')
        setBlock(cycle * 4 + 2, (x-1, 1, 3), 'lever', face='wall', facing='west', powered='false')
    if x == 3:
        setBlock(28, (x, 2, 3), 'air')
        setBlock(28, (x, 2, 3), 'redstone_torch')
    setBlock(28, (x-1, 1, 3), 'lever', face='wall', facing='west', powered='true')
    setBlock(30, (x-1, 1, 3), 'lever', face='wall', facing='west', powered='false')
    watch += [[x, 2, 3], [x, 3, 3]]

# Burnout count belongs to the old coordinate, not to the next placed torch.
setBlock(32, (20, 1, 3), 'stone')
setBlock(32, (20, 2, 3), 'redstone_torch')
watch.append([20, 2, 3])
runCapture(commands, watch, endTick=194, fixtureName='java26_2Torches')
