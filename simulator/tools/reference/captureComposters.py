#!/usr/bin/env python3
"""Compost maturation, sided insertion and virtual-container side effects."""
from captureRedstone import commands,watch,setBlock,runCapture
commands.clear();watch.clear()
def inventory(tick,pos,item,count=1,slot=0):commands.append({'tick':tick,'pos':list(pos),'stimulus':{'inventory':[{'slot':slot,'item':'minecraft:'+item,'count':count}]}})
for x in [3,12,21]:
    setBlock(0,(x,2,3),'composter',level='6');setBlock(0,(x,3,3),'hopper');setBlock(0,(x,1,3),'hopper',facing='east');setBlock(0,(x+1,1,3),'stone')
    inventory(0,(x,3,3),'pumpkin_pie',4)
    if x!=3:
        for slot in range(5):inventory(0,(x,1,3),'stone',64 if x==21 else 63,slot)
        inventory(30,(x,1,3),'air',0,4)
    watch.extend([[x,2,3],[x,3,3],[x,1,3]])
    setBlock(0,(x,1,4),'stone');setBlock(0,(x,2,4),'comparator',facing='north');watch.append([x,2,4])

for i,(facing,unit) in enumerate([('down',(0,-1,0)),('up',(0,1,0)),('north',(0,0,-1)),('south',(0,0,1)),('west',(-1,0,0)),('east',(1,0,0))]):
    p=(3+i*7,2,14);source=tuple(p[j]-unit[j] for j in range(3))
    setBlock(0,p,'composter');setBlock(0,source,'dropper',facing=facing);inventory(0,source,'pumpkin_pie',2)
    # Neighbor power rising edge schedules the ordinary dropper tick.
    power=(source[0]-1,source[1],source[2]) if unit[1] else (source[0],source[1]+1,source[2]);setBlock(1,power,'redstone_block');setBlock(6,power,'air')
    watch.extend([list(p),list(source)])

# Re-entering level seven retains an already scheduled maturation tick.
setBlock(0,(3,2,24),'composter',level='7');setBlock(4,(3,2,24),'composter',level='5');setBlock(9,(3,2,24),'composter',level='7');watch.append([3,2,24])
setBlock(0,(7,2,24),'sculk_sensor');watch.append([7,2,24])
commands.append({'tick':70,'pos':[3,2,24],'stimulus':{'compostItem':'minecraft:cake'}})
setBlock(71,(3,2,24),'composter',level='0');commands.append({'tick':72,'pos':[3,2,24],'stimulus':{'compostItem':'minecraft:cake'}})
runCapture(commands,watch,110,'java26_2Composters',forceLoadedNeighborhood=True,discardDrops=True)
