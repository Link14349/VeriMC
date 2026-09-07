#!/usr/bin/env python3
"""Compost input, maturation, snapshots and virtual output via the live protocol."""
import sys
from serverClient import ServerClient

client=ServerClient(int(sys.argv[1]) if len(sys.argv)>1 else 28765)
command=client.command
original=command('save',checkpoint=True)
pos=[0,1,0]
try:
    command('new');command('place',pos=pos,name='composter')
    command('probe',pos=pos,mode='analog')
    command('stimulate',pos=pos,stimulus={'compostItem':'stone'})
    assert command('inspect',pos=pos)['analog']==0
    assert command('stimulate',pos=pos,stimulus={'compostItem':'unknown_item'})['type']=='error'
    command('place',pos=pos,name='composter',properties={'level':'1'})
    before=command('save',checkpoint=True)
    command('stimulate',pos=pos,stimulus={'compostItem':'wheat_seeds'})
    after=command('save',checkpoint=True)
    assert after['randomSource']['state']!=before['randomSource']['state'] and command('inspect',pos=pos)['analog']==1
    command('undo');assert command('save',checkpoint=True)['randomSource']==before['randomSource']
    command('redo');assert command('save',checkpoint=True)['randomSource']==after['randomSource']
    command('place',pos=pos,name='composter',properties={'level':'0'})
    for _ in range(7):command('stimulate',pos=pos,stimulus={'compostItem':'pumpkin_pie'})
    command('step',count=19);assert command('inspect',pos=pos)['analog']==7
    checkpoint=command('save',checkpoint=True)
    command('step',count=1);assert command('inspect',pos=pos)['analog']==8
    command('load',project=checkpoint);command('step',count=1)
    assert command('inspect',pos=pos)['analog']==8
    command('place',pos=[0,0,0],name='hopper',properties={'facing':'east'})
    command('step',count=1)
    assert command('inspect',pos=pos)['analog']==0
    assert command('inspect',pos=[0,0,0])['inventory'][0]['item']=='minecraft:bone_meal'

    for count,expected in [(63,0),(64,8)]:
        command('new');command('place',pos=pos,name='composter',properties={'level':'8'})
        command('place',pos=[0,0,0],name='hopper',properties={'facing':'east'})
        command('stimulate',pos=[0,0,0],stimulus={'inventory':[{'slot':slot,'item':'stone','count':count} for slot in range(5)]})
        command('step',count=1)
        assert command('inspect',pos=pos)['analog']==expected
        assert all(slot['item']=='minecraft:stone' for slot in command('inspect',pos=[0,0,0])['inventory'])
    print('PASS: compost input, maturity, checkpoint, bone meal extraction and original failed-transfer behavior')
finally:
    command('load',project=original);client.close()
