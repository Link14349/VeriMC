#!/usr/bin/env python3
"""Target hit coordinates drive strength; pulse duration survives repeated hits."""
import sys
from serverClient import ServerClient
client=ServerClient(int(sys.argv[1]) if len(sys.argv)>1 else 28765)
command=client.command
original=command('save',checkpoint=True)

def power(): return int(command('inspect',pos=[0,1,0])['properties']['power'])

try:
    command('new');command('place',pos=[0,1,0],name='target',properties={'power':'15'})
    assert power()==0
    assert command('stimulate',pos=[0,1,0],stimulus={'value':15})['type']=='error'
    assert command('stimulate',pos=[0,1,0],stimulus={'face':'up','hit':[.5,1,.5],'arrow':True})['type']=='reply'
    assert power()==15
    command('step',count=3)
    command('stimulate',pos=[0,1,0],stimulus={'face':'east','hit':[1,.5,.05],'arrow':False})
    assert power()==15
    snapshot=command('save',checkpoint=True);command('step',count=17);assert power()==0
    command('load',project=snapshot);command('step',count=16);assert power()==15
    command('step');assert power()==0
    command('stimulate',pos=[0,1,0],stimulus={'face':'east','hit':[1,.5,.05],'arrow':False});assert power()==2
    command('step',count=7);assert power()==2
    command('step');assert power()==0
    print('PASS: coordinate-derived strength, direct-value rejection, placement reset, pulse timing and checkpoint')
finally:
    command('pause');command('load',project=original);client.close()
