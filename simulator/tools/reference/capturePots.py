#!/usr/bin/env python3
"""Single-slot pot capacities, six insertion faces and hopper notification rules."""
from captureRedstone import commands,watch,setBlock,runCapture
commands.clear();watch.clear()
def inventory(tick,pos,item='stone',count=0,slot=0):
    commands.append({'tick':tick,'pos':list(pos),'stimulus':{'inventory':[{'slot':slot,'item':item,'count':count}]}})
def comparator(pos):
    x,y,z=pos;setBlock(0,(x+1,y-1,z),'stone');setBlock(0,(x+1,y,z),'comparator',facing='west');watch.append([x+1,y,z])

# Every ordinary-item fullness threshold, then explicit emptying and removal.
for x,item,maximum,facing in [(3,'stone',64,'north'),(13,'snowball',16,'east'),(23,'wooden_sword',1,'south'),(33,'knowledge_book',1,'west')]:
    pos=(x,2,3);setBlock(0,pos,'decorated_pot',facing=facing);watch.append(list(pos));comparator(pos)
    setBlock(0,(x,3,3),'stone') # Pot readout is not blocked by an overhead cube.
    for tick in range(65):inventory(tick,pos,item,tick%(maximum+1))
    inventory(66,pos);setBlock(68,pos,'air')

# A dropper can insert from all six sides; the pot has no face-specific slots.
for i,(facing,dx,dy,dz) in enumerate([('east',1,0,0),('west',-1,0,0),('up',0,1,0),('down',0,-1,0),('north',0,0,-1),('south',0,0,1)]):
    source=(3+i*7,3,12);target=(source[0]+dx,source[1]+dy,source[2]+dz)
    setBlock(0,source,'dropper',facing=facing);setBlock(0,target,'decorated_pot')
    inventory(0,source,'snowball',2);watch.extend([list(source),list(target)])
    power=(source[0],source[1],source[2]+(1 if dz==-1 else -1))
    for tick in (1,9):setBlock(tick,power,'redstone_block');setBlock(tick+1,power,'air')

# Mixed/overflow insertion: skip a mismatched stack, then stop at capacity.
setBlock(0,(3,2,22),'hopper',facing='east');setBlock(0,(4,2,22),'decorated_pot')
inventory(0,(4,2,22),'snowball',15);inventory(0,(3,2,22),'stone',3);inventory(0,(3,2,22),'snowball',3,1)
watch.extend([[3,2,22],[4,2,22]]);comparator((4,2,22))
inventory(17,(4,2,22));inventory(33,(4,2,22))

# A blocked extraction removes/restores without setChanged, so it can sleep.
setBlock(0,(13,3,22),'decorated_pot');setBlock(0,(13,2,22),'hopper',facing='east')
inventory(0,(13,3,22),'snowball',2)
for slot in range(5):inventory(0,(13,2,22),'stone',63,slot)
watch.extend([[13,3,22],[13,2,22]]);comparator((13,3,22))
inventory(7,(13,2,22),slot=1)
runCapture(commands,watch,72,'java26_2Pots')
