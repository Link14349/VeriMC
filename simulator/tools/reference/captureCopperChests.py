#!/usr/bin/env python3
"""All copper chest variant pairings with actual vanilla player-placement rules."""
from captureRedstone import commands,watch,setBlock,runCapture
commands.clear();watch.clear()
names=[prefix+age+'copper_chest' for prefix in ['', 'waxed_'] for age in ['', 'exposed_', 'weathered_', 'oxidized_']]
directions=[('north',(1,0,0)),('east',(0,0,1)),('south',(-1,0,0)),('west',(0,0,-1))]

def inventory(tick,pos,slots):commands.append({'tick':tick,'pos':list(pos),'stimulus':{'inventory':slots}})
def slot(index,count,item='stone'):return {'slot':index,'item':item,'count':count}
def playerPlace(tick,pos,name,facing):
    setBlock(tick,pos,name,facing=facing);commands[-1]['playerPlace']=True

for row,first in enumerate(names):
    for col,second in enumerate(names):
        facing,vector=directions[(row+col)%4]
        pos=(4+col*5,2,4+row*5);partner=tuple(a+b for a,b in zip(pos,vector))
        for p in [pos,partner]:setBlock(0,(p[0],1,p[2]),'stone')
        setBlock(0,pos,first,facing=facing)
        inventory(0,pos,[slot(0,8),slot(26,1,'wooden_sword')])
        playerPlace(1,partner,second,facing)
        inventory(2,pos,[slot(0,16,'ender_pearl')])
        # Replacing one half with another copper variant must retain both BEs.
        setBlock(4,pos,'waxed_weathered_copper_chest',facing=facing,type='left')
        setBlock(6,partner,'air')
        playerPlace(8,partner,'oxidized_copper_chest',facing)
        watch.extend([list(pos),list(partner)])

# Automation and analog read-through are exercised separately from pairing.
setBlock(0,(3,2,46),'hopper',facing='east');setBlock(0,(4,2,46),'barrel')
setBlock(0,(3,3,46),'oxidized_copper_chest')
inventory(0,(3,3,46),[slot(0,5)])
setBlock(3,(3,3,46),'waxed_copper_chest')
setBlock(0,(4,1,47),'stone');setBlock(0,(4,2,47),'comparator',facing='north')
watch.extend([[3,3,46],[3,2,46],[4,2,46],[4,2,47]])
runCapture(commands,watch,24,'java26_2CopperChests',discardDrops=True)
