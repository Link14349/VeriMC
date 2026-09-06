#!/usr/bin/env python3
"""Button footprints, arrow polling and manual press timelines in Java 26.2."""
from captureRedstone import commands, watch, setBlock, runCapture

commands.clear()
watch.clear()

def contact(tick, pos, arrows, pressedArrows=None):
    stimulus = {'arrows': arrows}
    if pressedArrows is not None: stimulus['pressedArrows'] = pressedArrows
    commands.append({'tick': tick, 'pos': list(pos), 'stimulus': stimulus})

def click(tick, pos): commands.append({'tick':tick, 'pos':list(pos), 'interact':True})

for index, (face, facing, support) in enumerate([
    ('floor','north',(0,-1,0)), ('ceiling','east',(0,1,0)),
    ('wall','east',(-1,0,0)), ('wall','west',(1,0,0)),
    ('wall','south',(0,0,-1)), ('wall','north',(0,0,1)),
]):
    for shallow in [False, True]:
        pos = (4 + index * 6, 2, 4 if not shallow else 12)
        setBlock(0, tuple(a+b for a,b in zip(pos,support)), 'stone')
        setBlock(0, pos, 'oak_button', face=face, facing=facing)
        watch.append(list(pos))
        contact(2, pos, 1, 0 if shallow else 1)
        contact(66, pos, 0)

for index, name in enumerate(['stone_button', 'polished_blackstone_button', 'bamboo_button', 'crimson_button']):
    pos = (4 + index * 6, 2, 24)
    setBlock(0, (pos[0],1,pos[2]), 'stone'); setBlock(0, pos, name, face='floor')
    watch.append(list(pos))
    contact(2, pos, 1); click(4, pos); click(10, pos); contact(35, pos, 0)
    click(65, pos)

# Editing a powered state does not remove the arrow or the existing scheduled tick.
setBlock(40, (4,2,4), 'oak_button', face='floor', powered='false')

runCapture(commands, watch, 100, 'java26_2Buttons', discardDrops=True)
