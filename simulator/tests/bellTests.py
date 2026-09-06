#!/usr/bin/env python3
"""Bell interaction, hit filtering, support and checkpoint protocol checks."""
import sys
from serverClient import ServerClient
client=ServerClient(int(sys.argv[1]) if len(sys.argv)>1 else 28765)
command=client.command;original=command('save',checkpoint=True)
try:
    command('new');command('place',pos=[0,0,0],name='stone');command('place',pos=[0,1,0],name='bell')
    command('place',pos=[4,1,0],name='sculk_sensor');command('probe',pos=[4,1,0],mode='analog')
    command('stimulate',pos=[0,1,0],stimulus={'face':'up','height':.5})
    command('stimulate',pos=[0,1,0],stimulus={'face':'east','height':.5})
    command('stimulate',pos=[0,1,0],stimulus={'face':'north','height':.9})
    assert not command('inspect',pos=[0,1,0])['runtime'].get('ringing',False)
    assert command('stimulate',pos=[0,1,0],stimulus={'height':2,'face':'north'})['type']=='error'
    command('interact',pos=[0,1,0]);checkpoint=command('save',checkpoint=True)
    assert command('save',checkpoint=False)['type']=='error'
    command('step',count=4);assert command('inspect',pos=[4,1,0])['analog']==11
    command('load',project=checkpoint);command('step',count=4);assert command('inspect',pos=[4,1,0])['analog']==11
    command('step',count=46);assert command('inspect',pos=[0,1,0])['runtime']['ringing']==False
    assert command('save',checkpoint=False)['type']=='reply'
    command('place',pos=[0,1,-1],name='stone');command('place',pos=[0,1,1],name='stone')
    command('place',pos=[0,1,0],name='bell',properties={'attachment':'double_wall','facing':'north'})
    command('remove',pos=[0,1,-1]);bell=command('inspect',pos=[0,1,0]);assert bell['properties']['attachment']=='single_wall' and bell['properties']['facing']=='south'
    command('remove',pos=[0,1,1]);assert command('inspect',pos=[0,1,0])['name']=='minecraft:air'
    print('PASS: bell hit rejection, interaction, frequency 11, checkpoint, completion and wall support')
finally:
    command('load',project=original);client.close()
