#!/usr/bin/env python3
"""All target faces, repeated hits, stale power placement and pulse timing."""
from captureRedstone import commands,watch,setBlock,runCapture
commands.clear();watch.clear()

def hit(tick,pos,face,point,arrow=True):
    commands.append({'tick':tick,'pos':list(pos),'stimulus':{'face':face,'hit':point,'arrow':arrow}})

for i,face in enumerate(['down','up','north','south','west','east']):
    pos=(4+i*6,2,4);setBlock(0,pos,'target');watch.append(list(pos))
    normal=1 if face in ['down','up'] else 2 if face in ['north','south'] else 0
    point=[.5,.5,.5];point[normal]=1 if face in ['up','south','east'] else 0
    hit(1,pos,face,point,True)
    point=point.copy();point[(normal+1)%3]=.07;hit(5,pos,face,point,False)
    hit(22,pos,face,point,False)
    point=point.copy();point[(normal+1)%3]=0;hit(31,pos,face,point,True)
    for x in range(pos[0]+1,pos[0]+4):
        setBlock(0,(x,1,4),'stone');setBlock(0,(x,2,4),'redstone_wire');watch.append([x,2,4])

# Newly placed powered states reset unless an older matching tick is pending.
setBlock(0,(4,2,12),'target',power='15');watch.append([4,2,12])
setBlock(0,(12,2,12),'target');watch.append([12,2,12]);hit(1,(12,2,12),'up',[.5,1,.5])
setBlock(5,(12,2,12),'air');setBlock(6,(12,2,12),'target',power='7')
setBlock(22,(12,2,12),'air');setBlock(23,(12,2,12),'target',power='11')
runCapture(commands,watch,52,'java26_2Targets')
