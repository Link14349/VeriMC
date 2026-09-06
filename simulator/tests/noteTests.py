#!/usr/bin/env python3
"""Note controls, scheduled sound diagnostics and vibration integration."""
import sys
from serverClient import ServerClient
client=ServerClient(int(sys.argv[1]) if len(sys.argv)>1 else 28765)
command=client.command;original=command('save',checkpoint=True)
try:
    command('demo',kind='notes')
    assert command('inspect',pos=[0,1,0])['properties']['instrument']=='trumpet'
    command('interact',pos=[0,1,0]);command('stimulate',pos=[0,1,0],stimulus={'playNote':True})
    queued=command('save',checkpoint=True)
    assert command('save',checkpoint=False)['type']=='error'
    command('step',count=1);note=command('inspect',pos=[0,1,0])
    assert note['runtime']['playCount']==1 and note['runtime']['lastPlayed']['note']==1
    command('load',project=queued);command('step',count=1)
    assert command('inspect',pos=[0,1,0])['runtime']==note['runtime']
    command('step',count=3);sensor=command('inspect',pos=[4,1,0]);assert sensor['analog']==10 and sensor['value']==8
    command('place',pos=[0,2,0],name='glass');command('stimulate',pos=[0,1,0],stimulus={'playNote':True});command('step',count=1)
    assert command('inspect',pos=[0,1,0])['runtime']['playCount']==1
    command('place',pos=[0,2,0],name='player_head');command('stimulate',pos=[0,2,0],stimulus={'customSound':'simulator:test/bell'})
    command('stimulate',pos=[0,1,0],stimulus={'playNote':True});command('step',count=1)
    assert command('inspect',pos=[0,1,0])['runtime']['lastPlayed']['sound']=='simulator:test/bell'
    assert command('stimulate',pos=[0,2,0],stimulus={'customSound':'BAD SOUND'})['type']=='error'
    command('stimulate',pos=[0,2,0],stimulus={'customSound':None});command('stimulate',pos=[0,1,0],stimulus={'playNote':True});command('step',count=1)
    assert command('inspect',pos=[0,1,0])['runtime']['playCount']==2
    print('PASS: note tuning, deduplicated sound, checkpoint, vibration, obstruction and custom heads')
finally:
    command('load',project=original);client.close()
