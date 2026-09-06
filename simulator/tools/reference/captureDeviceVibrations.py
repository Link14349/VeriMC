#!/usr/bin/env python3
"""Native device events entering unmodified vanilla sensor listeners."""
from captureRedstone import commands,watch,setBlock,runCapture
commands.clear();watch.clear()
def stimulus(tick,pos,**values):commands.append({'tick':tick,'pos':list(pos),'stimulus':values})
def interact(tick,pos):commands.append({'tick':tick,'pos':list(pos),'interact':True})
def sensor(pos):
    setBlock(0,pos,'sculk_sensor');watch.append(list(pos))
    x,y,z=pos;setBlock(0,(x+1,y-1,z),'stone');setBlock(0,(x+1,y,z),'comparator',facing='west');watch.append([x+1,y,z])

for x,z,name in [(3,4,'lever'),(3,16,'stone_button'),(3,28,'stone_pressure_plate'),(3,40,'oak_door'),(27,4,'oak_trapdoor'),(27,16,'oak_fence_gate'),(27,28,'barrel'),(27,40,'chiseled_bookshelf')]:
    pos=(x,2,z);setBlock(0,(x,1,z),'stone')
    setBlock(0,pos,name,**({'face':'floor'} if name in ['lever','stone_button'] else {}))
    if name=='oak_door':setBlock(0,(x,3,z),name,half='upper')
    watch.append(list(pos));sensor((x+4,2,z))
    if name in ['lever','stone_button']:
        interact(4,pos);interact(54,pos)
    elif name=='stone_pressure_plate':
        stimulus(4,pos,entities=1);stimulus(5,pos,entities=0)
        stimulus(54,pos,entities=1);stimulus(55,pos,entities=0)
    elif name in ['oak_door','oak_trapdoor','oak_fence_gate']:
        for tick in [4,54]:setBlock(tick,(x-1,2,z),'redstone_block');setBlock(tick+2,(x-1,2,z),'air')
        if name=='oak_door':interact(104,pos);interact(106,pos)
    elif name=='barrel':
        for tick in [4,54]:stimulus(tick,pos,viewers=1);stimulus(tick+2,pos,viewers=0)
    else:
        stimulus(4,pos,inventory=[{'slot':5,'item':'book','count':1}]);stimulus(54,pos,inventory=[{'slot':5,'count':0}])

# Piston extension, contraction and wool destruction send their own contexts.
setBlock(0,(18,2,24),'piston',facing='east');setBlock(0,(19,2,24),'white_carpet');setBlock(0,(19,1,24),'stone')
sensor((21,2,24));watch.append([18,2,24])
setBlock(4,(17,2,24),'redstone_block');setBlock(54,(17,2,24),'air')

# Hook events are emitted from both ends at their original update positions.
setBlock(0,(17,2,8),'stone');setBlock(0,(23,2,8),'stone')
setBlock(0,(18,2,8),'tripwire_hook',facing='east');setBlock(0,(22,2,8),'tripwire_hook',facing='west')
for x in range(19,22):setBlock(0,(x,2,8),'tripwire')
sensor((20,2,12));watch.extend([[18,2,8],[22,2,8]])
stimulus(4,(20,2,8),entities=1);stimulus(5,(20,2,8),entities=0)
stimulus(54,(20,2,8),shear=True)
runCapture(commands,watch,154,'java26_2DeviceVibrations',forceLoadedNeighborhood=True,discardDrops=True)
