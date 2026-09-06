#!/usr/bin/env python3
"""Dropper output pauses, versioned feedback, snapshot and undo through WebSocket."""
import sys
from serverClient import ServerClient

client = ServerClient(int(sys.argv[1]) if len(sys.argv)>1 else 28765)
command = client.command
original = command('save',checkpoint=True)

def status():
    while True:
        message=client.receive()
        if message.get('type')=='status': return message

try:
    assert command('demo',kind='droppers')['type']=='reply'
    command('interact',pos=[-1,1,0]);command('step',count=4)
    assert sum(s['count'] for s in command('inspect',pos=[1,1,0])['inventory'])==1
    command('interact',pos=[-1,1,3]);command('step',count=4)
    pending=status()
    while not pending['pendingActions']: pending=status()
    action=pending['pendingActions'][0]
    assert action['source']==[0,1,3] and action['tick']==8 and action['count']==1
    snapshot=command('save',checkpoint=True)
    for cmd in ['play','step','stepEvent']: assert command(cmd)['type']=='error'
    assert command('save',checkpoint=False)['type']=='error'
    assert command('resolveAction',id=action['id'],revision=pending['revision']-1)['type']=='error'
    assert command('resolveAction',id=action['id'],revision=pending['revision'])['type']=='reply'
    resolved=status()
    while resolved['pendingActions']: resolved=status()
    assert command('resolveAction',id=action['id'],revision=resolved['revision'])['type']=='error'
    log=command('actionLog');assert log['actions'][0]['resolved'] and log['dropped']==0
    command('undo')
    restored=status()
    while not restored['pendingActions']: restored=status()
    assert command('resolveAction',id=action['id'],revision=pending['revision'])['type']=='error', 'stale pre-undo revision reused'
    command('load',project=snapshot)
    loaded=status()
    while loaded['revision']<=restored['revision']: loaded=status()
    assert loaded['pendingActions'][0]==action
    # A user can supply feedback while execution waits, without an implicit resume.
    assert command('stimulate',pos=[1,1,0],stimulus={'inventory':[{'slot':8,'item':'stone','count':1}]})['type']=='reply'
    feedback=status()
    while feedback['revision']<=loaded['revision']: feedback=status()
    assert not feedback['running'] and feedback['pendingActions']
    assert command('resolveAction',id=action['id'],revision=feedback['revision'])['type']=='reply'
    command('step',count=30)
    end=command('save',checkpoint=True)
    assert end['tick']==38 and all(a['resolved'] for a in end['environmentActions'])
    print('PASS: container transfer, external output, pending pause, stale revision, feedback, checkpoint and undo')
finally:
    command('pause');command('load',project=original);client.close()
