#!/usr/bin/env python3
"""Container-directed dropper timing; one occupied slot isolates random choice."""
from captureRedstone import commands, watch, setBlock, runCapture

commands.clear()
watch.clear()

def inventory(tick,pos,slots): commands.append({'tick':tick,'pos':list(pos),'stimulus':{'inventory':slots}})

for i, (facing, vector) in enumerate([('east',(1,0,0)),('west',(-1,0,0)),('up',(0,1,0)),('down',(0,-1,0)),('north',(0,0,-1)),('south',(0,0,1))]):
    pos=(4+i*7,3,4)
    target=tuple(a+b for a,b in zip(pos,vector))
    power=(pos[0],pos[1],pos[2]+1) if facing not in ['north','south'] else (pos[0]+1,pos[1],pos[2])
    setBlock(0,pos,'dropper',facing=facing);setBlock(0,target,'barrel')
    inventory(0,pos,[{'slot':4,'item':'stone','count':2}]);watch.extend([list(pos),list(target)])
    for tick,name in [(1,'redstone_block'),(2,'air'),(3,'redstone_block'),(4,'air'),(6,'redstone_block'),(7,'air')]: setBlock(tick,power,name)

setBlock(0,(4,3,16),'dropper',facing='east');setBlock(0,(5,3,16),'hopper',facing='east');setBlock(0,(6,3,16),'barrel')
inventory(0,(4,3,16),[{'slot':0,'item':'stone','count':2}])
for tick,name in [(1,'redstone_block'),(2,'air'),(9,'redstone_block'),(10,'air')]:setBlock(tick,(3,3,16),name)
watch.extend([[4,3,16],[5,3,16],[6,3,16]])

# Full destination retains the selected item. Clearing one target slot allows
# the next trigger, without changing the one-slot source selection assumption.
setBlock(0,(12,3,16),'dropper',facing='east');setBlock(0,(13,3,16),'barrel')
inventory(0,(12,3,16),[{'slot':2,'item':'wooden_sword','count':1}])
inventory(0,(13,3,16),[{'slot':slot,'item':'stone','count':64} for slot in range(27)])
setBlock(1,(11,3,16),'redstone_block');setBlock(2,(11,3,16),'air')
inventory(8,(13,3,16),[{'slot':9,'item':'stone','count':0}]);setBlock(9,(11,3,16),'redstone_block')
watch.extend([[12,3,16],[13,3,16]])

# Quasi-connectivity needs a real neighbor notification.
setBlock(0,(20,3,16),'dropper',facing='east');setBlock(0,(21,3,16),'chest');setBlock(0,(21,4,16),'stone')
inventory(0,(20,3,16),[{'slot':8,'item':'ender_pearl','count':2}])
setBlock(1,(20,5,16),'redstone_block');setBlock(8,(19,3,16),'stone')
watch.extend([[20,3,16],[21,3,16]])

runCapture(commands,watch,30,'java26_2Droppers')
