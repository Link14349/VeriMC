#!/usr/bin/env python3
"""Slow receiver, ACK validation, reset epochs and bounded trace integration."""
import sys
from serverClient import ServerClient

client = ServerClient(int(sys.argv[1]) if len(sys.argv) > 1 else 28765)
command = client.command
original = command('save', checkpoint=True)
try:
    assert command('new')['type'] == 'reply'
    assert command('traceBudget', capacity=256)['type'] == 'reply'
    for x, facing in [(0,'east'), (1,'west'), (0,'south'), (0,'east')]:
        assert command('place', pos=[x,0,0], name='observer', properties={'facing':facing})['type'] == 'reply'
    for i in range(16): assert command('probe', pos=[0,0,0], name=f'clock{i}')['type'] == 'reply'
    # Drain initialization, then hold a frame so the simulator cannot send more.
    client.nextTrace()
    client.autoAck = False
    held = client.nextTrace()
    assert command('step', count=1000)['type'] == 'reply'
    paused = command('save', checkpoint=True)
    assert paused['tick'] < 1000 and not paused['faulted'], paused.get('tick')
    assert paused['traceDropped'] <= held['from'] and len(paused['trace']) >= 256
    assert paused['events'] or paused['blockTickState']['batch'], 'pause consumed future work'
    assert command('step', count=1)['type'] == 'error', 'full trace allowed another event'
    # Neither the old binary-only ACK nor an incorrect trace end releases data.
    client.send({'cmd':'ack', 'frameId':held['frameId']})
    client.send({'cmd':'ack', 'frameId':held['frameId'], 'epoch':held['epoch'], 'traceEnd':held['next'] + 1})
    assert command('stepEvent')['type'] == 'error', 'incorrect ACK released protected edges'
    assert command('save', checkpoint=True)['tick'] == paused['tick']
    client.ackTrace(held)
    pending = client.nextTrace()
    assert pending['from'] == held['next'] and pending['next'] == paused['traceDropped'] + len(paused['trace'])
    assert pending['edges'] == paused['trace'][pending['from'] - paused['traceDropped']:], 'edges missing during blocked send'
    client.ackTrace(pending)
    client.autoAck = True
    assert command('step', count=8)['type'] == 'reply'
    resumed = command('save', checkpoint=True)
    assert resumed['tick'] > paused['tick'] and not resumed['faulted']
    assert resumed['traceDropped'] > 0, 'ACK did not allow bounded history trimming'
    # Replace the world while a previous frame awaits ACK: full refresh must wait.
    client.nextTrace(); client.autoAck = False
    old = client.nextTrace()
    assert command('new')['type'] == 'reply'
    assert client.latestTrace['frameId'] == old['frameId'], 'full refresh bypassed outstanding ACK'
    client.ackTrace(old)
    reset = client.nextTrace()
    assert reset['reset'] and reset['epoch'] > old['epoch'] and reset['next'] == 0
    client.ackTrace(reset); client.autoAck = True
    # A new, fast window must not acknowledge on behalf of an older, slow one.
    command('load', project=paused)
    client.nextTrace(); client.autoAck = False
    slow = client.nextTrace()
    peer = ServerClient(int(sys.argv[1]) if len(sys.argv) > 1 else 28765)
    try:
        peer.nextTrace()
        assert peer.command('step', count=1000)['type'] == 'reply'
        blocked = peer.command('save', checkpoint=True)
        assert peer.command('stepEvent')['type'] == 'error'
        assert blocked['traceDropped'] <= slow['from'], 'fast peer released slow peer history'
        client.ackTrace(slow); client.autoAck = True
        client.nextTrace(); peer.nextTrace()
        assert peer.command('stepEvent')['type'] == 'reply'
    finally:
        peer.close()
    print('PASS: bounded trace pause, invalid ACK rejection, exact delivery, continuation, reset epochs and slowest-client retention')
finally:
    client.autoAck = True
    if client.latestTrace: client.ackTrace(client.latestTrace)
    command('traceBudget', capacity=500000)
    command('load', project=original)
    client.close()
