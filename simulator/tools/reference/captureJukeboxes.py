#!/usr/bin/env python3
"""All fixed-version discs, repeated inventory writes and hopper filtering."""
import json
from captureRedstone import commands,watch,setBlock,runCapture,rootDir
commands.clear();watch.clear()
def inventory(tick,pos,item,count=1,slot=0):commands.append({'tick':tick,'pos':list(pos),'stimulus':{'inventory':[{'slot':slot,'item':'minecraft:'+item,'count':count}]}})
items=json.loads((rootDir/'data/jukeboxRules.json').read_text())['items']
for i,item in enumerate(items):
    x=2+(i%6)*7;z=2+(i//6)*7;pos=(x,2,z)
    setBlock(0,pos,'jukebox');setBlock(0,(x+1,1,z),'stone');setBlock(0,(x+1,2,z),'redstone_wire')
    setBlock(0,(x,1,z+1),'stone');setBlock(0,(x,2,z+1),'comparator',facing='north');watch.extend([list(pos),[x+1,2,z],[x,2,z+1]])
    inventory(1,pos,item.removeprefix('minecraft:'));inventory(42,pos,item.removeprefix('minecraft:'));inventory(83,pos,'air',0)
    inventory(85,pos,item.removeprefix('minecraft:'));inventory(126,pos,'air',0);inventory(127,pos,'air',0)

# A playing event itself must not trigger a sculk sensor after it has cooled.
setBlock(0,(3,2,34),'jukebox');setBlock(0,(7,2,34),'sculk_sensor');watch.extend([[3,2,34],[7,2,34]])
inventory(1,(3,2,34),'music_disc_bounce')
runCapture(commands,watch,150,'java26_2Jukeboxes',forceLoadedNeighborhood=True,discardDrops=True,watchJukeboxes=True)

# Full slot occupancy, despite spare stack capacity, prevents disc extraction.
commands.clear();watch.clear()
for x in [20,30,40]:
    setBlock(0,(x,2,35),'jukebox');setBlock(0,(x,1,35),'hopper',facing='east');setBlock(0,(x+1,1,35),'stone')
    for slot in range(5):inventory(0,(x,1,35),'stone',63,slot)
    inventory(1,(x,2,35),'music_disc_11');inventory(1447 if x==40 else 21,(x,1,35),'air',0,4)
    watch.extend([[x,2,35],[x,1,35],[x+1,1,35]])
    if x==30:
        # Explicit has_record edits gate the ticker, not the song/player state.
        setBlock(6,(x,2,35),'jukebox',has_record='false');setBlock(12,(x,2,35),'jukebox',has_record='true')
# Inserting during the block-entity phase registers playback for the next tick.
setBlock(0,(3,2,3),'jukebox');setBlock(0,(3,3,3),'hopper');inventory(0,(3,3,3),'music_disc_11')
watch.extend([[3,2,3],[3,3,3]])
runCapture(commands,watch,1455,'java26_2JukeboxHoppers',forceLoadedNeighborhood=True,discardDrops=True,watchJukeboxes=True)
