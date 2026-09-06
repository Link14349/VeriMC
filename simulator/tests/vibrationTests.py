#!/usr/bin/env python3
"""Vibration commands, distinct signal/frequency probes and exact continuation."""
import sys
from serverClient import ServerClient

client=ServerClient(int(sys.argv[1]) if len(sys.argv)>1 else 28765)
command=client.command;original=command('save',checkpoint=True)
try:
    command('new');command('place',pos=[0,0,0],name='sculk_sensor')
    command('probe',pos=[0,0,0],name='strength');command('probe',pos=[0,0,0],name='frequency',mode='analog')
    assert command('stimulate',pos=[8,0,0],stimulus={'gameEvent':'block_change','offset':[.999,.5,.5]})['type']=='reply'
    assert command('inspect',pos=[0,0,0])['vibration']['state']=='selecting'
    assert command('save',checkpoint=False)['type']=='error'
    command('step',count=3);flying=command('save',checkpoint=True)
    assert command('inspect',pos=[0,0,0])['vibration']['remaining']==5
    assert command('stimulate',pos=[1,0,0],stimulus={'gameEvent':'explode','offset':[2,.5,.5]})['type']=='error'
    command('step',count=5);arrived=command('inspect',pos=[0,0,0])
    assert arrived['value']==1 and arrived['analog']==11
    trace=command('vcd')['text'];assert 'b0001' in trace and 'b1011' in trace
    command('load',project=flying);command('step',count=5)
    assert command('vcd')['text']==trace
    command('step',count=40);assert command('inspect',pos=[0,0,0])['properties']['sculk_sensor_phase']=='inactive'
    assert command('save',checkpoint=False)['type']=='reply'
    command('demo',kind='vibrations');command('interact',pos=[0,1,0]);command('step',count=4)
    assert command('inspect',pos=[4,1,0])['analog']==10
    assert command('inspect',pos=[13,1,0])['value']==0
    command('step',count=8);assert command('inspect',pos=[13,1,0])['analog']==10
    command('step',count=40);assert command('inspect',pos=[4,1,20])['value']==0
    command('interact',pos=[0,1,20]);command('step',count=20)
    assert command('inspect',pos=[4,1,20])['value']==0
    command('remove',pos=[2,1,20]);command('interact',pos=[0,1,20]);command('step',count=4)
    assert command('inspect',pos=[4,1,20])['analog']==9
    print('PASS: candidate/travel inspection, strength/frequency probes, checkpoint, internal button, resonance and wool isolation')
finally:
    command('load',project=original);client.close()
