#!/usr/bin/env python3
"""Real game-event dispatch, filtering, travel, wool rays and sensor resonance."""
from captureRedstone import commands,watch,setBlock,runCapture
commands.clear();watch.clear()
def event(tick,pos,name='step',**options):
    commands.append({'tick':tick,'pos':list(pos),'stimulus':{'gameEvent':'minecraft:'+name,**options}})
def sensor(pos,kind='sculk_sensor',**props):
    setBlock(0,pos,kind,**props);watch.append(list(pos))
    x,y,z=pos;setBlock(0,(x+1,y-1,z),'stone');setBlock(0,(x+1,y,z),'comparator',facing='west');watch.append([x+1,y,z])

sensor((5,2,5))
event(4,(13,2,5),offset=[.999,.5,.5]) # Float travel distance; integer range/strength.
event(5,(6,2,5),'explode') # Already travelling: ignored.
event(54,(14,2,5),'explode',offset=[0,.5,.5]) # Outside integer radius.
event(55,(6,2,5),'step');event(55,(4,2,5),'explode') # Equal distance -> higher frequency.
event(56,(5,2,6),'eat') # Earlier candidate must not be replaced in another tick.
event(100,(5,2,5),'block_place');event(101,(5,2,5),'block_destroy') # Self exclusions.
event(102,(5,2,5),'step') # Zero-distance delivery on following tick.

sensor((5,2,24),'calibrated_sculk_sensor',facing='north')
setBlock(0,(5,2,25),'target');setBlock(1,(5,2,25),'target',power='11');watch.append([5,2,25])
event(4,(5,2,30),'explode');event(5,(5,2,30),'block_change')
setBlock(7,(5,2,25),'target',power='15') # Filtering at candidate receipt, not arrival.
event(34,(5,2,40),'explode') # Exact calibrated 16-block radius.
event(70,(5,2,25),'explode',source={'spectator':True})
event(71,(5,2,25),'explode',source={'dampensVibrations':True})
setBlock(72,(5,2,25),'target',power='0')
event(73,(5,2,25),'step',source={'sneaking':True})
event(74,(5,2,25),'block_change',source={'sneaking':True})
event(104,(5,2,25),'block_change',affectedBlock={'name':'minecraft:white_carpet'})
event(105,(5,2,25),'block_change',affectedBlock={'name':'minecraft:white_wool'})
event(106,(5,2,25),'block_change',affectedBlock={'name':'minecraft:stone'})

sensor((25,2,5));setBlock(0,(27,2,5),'white_wool');setBlock(0,(27,1,5),'stone')
event(4,(29,2,5),'eat')
setBlock(20,(27,2,5),'white_carpet');event(21,(29,2,5),'eat')
setBlock(65,(27,2,5),'stone');event(66,(29,2,5),'eat')
event(115,(29,2,5),'sculk_sensor_tendrils_clicking') # Not in vibration tag.

# Independent corner rays: a single edge-touching wool cell cannot occlude all six.
sensor((35,2,35),'calibrated_sculk_sensor',waterlogged='true')
setBlock(0,(37,2,36),'white_wool');event(4,(39,2,39),'equip')
setBlock(30,(37,2,37),'white_wool');event(31,(39,2,39),'equip')
setBlock(60,(37,2,37),'air');event(61,(39,2,39),'equip')

sensor((22,2,24));setBlock(0,(23,2,24),'amethyst_block');sensor((31,2,24))
event(4,(14,2,24),'drink');event(60,(14,2,24),'entity_die')
# Remove a receiver during travel, then re-place without retaining its listener.
setBlock(72,(31,2,24),'air');setBlock(73,(31,2,24),'sculk_sensor')
event(111,(14,2,24),'block_change')
runCapture(commands,watch,160,'java26_2Vibrations',forceLoadedNeighborhood=True)
