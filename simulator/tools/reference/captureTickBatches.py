#!/usr/bin/env python3
"""Facing observers with both ticks collected, within and across chunk borders."""
from captureRedstone import commands, watch, setBlock, runCapture

commands.clear()
watch.clear()
for x, z in [(3, 3), (15, 8), (31, 20), (-1, 30)]:
    setBlock(0, (x, 2, z), 'observer', facing='east')
    setBlock(0, (x+1, 2, z), 'observer', facing='west')
    setBlock(0, (x, 2, z), 'observer', facing='south')
    setBlock(0, (x, 2, z), 'observer', facing='east')
    watch += [[x, 2, z], [x+1, 2, z]]
runCapture(commands, watch, endTick=24, fixtureName='java26_2TickBatches')
