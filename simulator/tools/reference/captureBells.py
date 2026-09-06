#!/usr/bin/env python3
"""Bell support, power, explicit hits and sensor events in the fixed server."""
from captureRedstone import commands,watch,setBlock,runCapture
commands.clear();watch.clear()
vectors={'north':(0,0,-1),'south':(0,0,1),'east':(1,0,0),'west':(-1,0,0)}
def stimulus(tick,pos,**values):commands.append({'tick':tick,'pos':list(pos),'stimulus':values})
for row,attachment in enumerate(['floor','ceiling','single_wall','double_wall']):
    for col,facing in enumerate(vectors):
        x=3+col*12;z=3+row*12;pos=(x,2,z);dx,_,dz=vectors[facing]
        setBlock(0,(x,1,z),'stone')
        if attachment=='ceiling':setBlock(0,(x,3,z),'stone')
        if 'wall' in attachment:setBlock(0,(x+dx,2,z+dz),'stone')
        if attachment=='double_wall':setBlock(0,(x-dx,2,z-dz),'stone')
        setBlock(0,pos,'bell',facing=facing,attachment=attachment);watch.append(list(pos))
        setBlock(0,(x+4,2,z),'sculk_sensor');watch.append([x+4,2,z])
        setBlock(4,(x,1,z),'redstone_block');setBlock(5,(x,1,z),'stone')
        stimulus(12,pos,face='up',height=.5)
        valid=facing if attachment in ['floor','ceiling'] else ('east' if dz else 'north')
        stimulus(54,pos,face=valid,height=.9);stimulus(56,pos,face=valid,height=.5)
        stimulus(60,pos,ring=True)
        if attachment=='double_wall':setBlock(115,(x+dx,2,z+dz),'air');setBlock(117,(x+dx,2,z+dz),'stone');setBlock(119,(x-dx,2,z-dz),'air')
        elif attachment=='single_wall':setBlock(115,(x-dx,2,z-dz),'stone');setBlock(117,(x+dx,2,z+dz),'air');setBlock(119,(x-dx,2,z-dz),'air')
        else:setBlock(119,(x,3 if attachment=='ceiling' else 1,z),'air')
# Placement beside existing power does not itself call neighborChanged.
for x,name in [(9,'note_block'),(21,'bell')]:
    setBlock(0,(x,1,9),'stone');setBlock(0,(x-1,2,9),'redstone_block');setBlock(0,(x,2,9),name)
    watch.append([x,2,9]);setBlock(10,(x,2,10),'stone')
runCapture(commands,watch,122,'java26_2Bells',forceLoadedNeighborhood=True,discardDrops=True,watchBells=True)
