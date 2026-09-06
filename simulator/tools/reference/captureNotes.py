#!/usr/bin/env python3
"""Notes, dynamic instrument choice and actual note game events in Java 26.2."""
from captureRedstone import commands,watch,setBlock,runCapture
commands.clear();watch.clear()
def play(tick,pos):commands.append({'tick':tick,'pos':list(pos),'stimulus':{'playNote':True}})
def tune(tick,pos):commands.append({'tick':tick,'pos':list(pos),'interact':True})
def sensor(x,z):
    setBlock(0,(x,2,z),'sculk_sensor');setBlock(0,(x+1,1,z),'stone');setBlock(0,(x+1,2,z),'comparator',facing='west')
    watch.extend([[x,2,z],[x+1,2,z]])

# Every base instrument type that the editor currently admits, including new copper trumpets.
for i,base in enumerate(['air','stone','glass','oak_planks','clay','gold_block','white_wool','packed_ice','bone_block','iron_block','soul_sand','pumpkin','emerald_block','hay_block','glowstone','copper_block','exposed_copper','weathered_copper','oxidized_copper']):
    x=2+(i%7)*6;z=2+(i//7)*5
    setBlock(0,(x,2,z),'note_block');setBlock(1,(x,1,z),base if base!='air' else 'stone');setBlock(2,(x,1,z),base)
    watch.append([x,2,z]);tune(3,(x,2,z));play(4,(x,2,z))
    setBlock(5,(x+1,2,z),'redstone_block');setBlock(6,(x+1,2,z),'stone');setBlock(7,(x+1,2,z),'redstone_block')

# Base instruments are blocked even by transparent blocks above; removing the
# obstruction while still powered does not manufacture another rising edge.
setBlock(0,(3,2,20),'note_block');watch.append([3,2,20]);sensor(7,20)
setBlock(0,(3,3,20),'glass');setBlock(4,(2,2,20),'redstone_block');play(5,(3,2,20))
setBlock(6,(3,3,20),'air');setBlock(8,(2,2,20),'air');setBlock(9,(2,2,20),'redstone_block')
for _ in range(25):tune(55,(3,2,20))

# Heads override the base and allow playing with an occupied upper cell.
heads=['zombie_head','skeleton_skull','creeper_head','dragon_head','wither_skeleton_skull','piglin_head','player_head']
for i,head in enumerate(heads):
    x=3+i*6;z=30;setBlock(0,(x,1,z),'gold_block');setBlock(0,(x,2,z),'note_block');setBlock(1,(x,3,z),head)
    watch.append([x,2,z]);play(4,(x,2,z));setBlock(6,(x,3,z),'air');setBlock(7,(x,1,z),head)
# Exact event timing through ordinary and head instruments, including missing custom sound.
setBlock(0,(3,2,42),'note_block');watch.append([3,2,42]);setBlock(1,(3,3,42),'zombie_head');sensor(7,42);play(4,(3,2,42))
setBlock(53,(3,3,42),'player_head');play(54,(3,2,42))
runCapture(commands,watch,99,'java26_2Notes',forceLoadedNeighborhood=True,discardDrops=True)
