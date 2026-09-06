#!/usr/bin/env python3
"""Original hook spans, crossings, contact polling, cutting and support loss."""
from captureRedstone import commands, watch, setBlock, runCapture

commands.clear()
watch.clear()

def hook(tick, pos, facing, placed=False):
    setBlock(tick, pos, 'tripwire_hook', facing=facing)
    if placed: commands[-1]['placedBy'] = True

def contact(tick, pos, count):
    commands.append({'tick': tick, 'pos': list(pos), 'stimulus': {'entities': count}})

def horizontalLine(x, z, distance, lateHook=False):
    setBlock(0, (x-1, 2, z), 'stone')
    setBlock(0, (x+distance+1, 2, z), 'stone')
    hook(0, (x, 2, z), 'east')
    if not lateHook: hook(0, (x+distance, 2, z), 'west')
    for wireX in range(x+1, x+distance): setBlock(0, (wireX, 2, z), 'tripwire')
    if lateHook: hook(1, (x+distance, 2, z), 'west', True)
    watch.extend([[wireX, 2, z] for wireX in range(x, x+distance+1)])

horizontalLine(2, 3, 6)
contact(5, (5,2,3), 1); contact(7, (5,2,3), 0)
# A new arrival immediately after release is blocked by the zero-delay recheck.
contact(15, (5,2,3), 1); contact(18, (5,2,3), 0)
setBlock(30, (5,2,3), 'air')
setBlock(42, (5,2,3), 'tripwire')
commands.append({'tick': 55, 'pos': [5,2,3], 'stimulus': {'shear': True}})

horizontalLine(2, 7, 6, True)
setBlock(20, (1,2,7), 'air')  # Hook loses its attachment face.

# Opposite horizontal axis, no block underneath the wire.
setBlock(0, (17,2,1), 'stone'); setBlock(0, (17,2,9), 'stone')
hook(0, (17,2,2), 'south'); hook(0, (17,2,8), 'north')
for z in range(3,8): setBlock(0, (17,2,z), 'tripwire')
watch.extend([[17,2,z] for z in range(2,9)])
contact(3, (17,2,5), 2); contact(6, (17,2,5), 0)
setBlock(25, (17,2,9), 'air')

horizontalLine(2, 20, 41)  # Forty wire blocks are valid.
horizontalLine(2, 24, 42)  # Forty-one wire blocks are too long.
horizontalLine(2, 28, 1)   # Adjacent hooks are not a wire span.
contact(5, (5,2,20), 1); contact(9, (5,2,20), 0)
contact(5, (5,2,24), 1); contact(9, (5,2,24), 0)

horizontalLine(25, 8, 6)
setBlock(0, (28,2,4), 'stone'); setBlock(0, (28,2,12), 'stone')
hook(0, (28,2,5), 'south'); hook(0, (28,2,11), 'north')
for z in [6,7,9,10]: setBlock(0, (28,2,z), 'tripwire')
watch.extend([[28,2,z] for z in [5,6,7,9,10,11]])
contact(2, (28,2,8), 1); contact(4, (28,2,8), 0)
setBlock(25, (28,2,8), 'air')

runCapture(commands, watch, endTick=69, fixtureName='java26_2Tripwire', discardDrops=True)
