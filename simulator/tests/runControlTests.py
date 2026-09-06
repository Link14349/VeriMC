#!/usr/bin/env python3
"""Unlimited runs stop at their last event; displayed work excludes idle jumps."""
import sys
import time
from serverClient import ServerClient

client = ServerClient(int(sys.argv[1]) if len(sys.argv) > 1 else 28765)
command = client.command
original = command('save', checkpoint=True)

def waitStatus(predicate):
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        message = client.receive()
        if message.get('type') == 'status' and predicate(message): return message
    raise AssertionError('expected run status did not arrive')

try:
    command('new'); command('speed', value=0); command('play')
    empty = waitStatus(lambda s: s['blocks'] == 0 and '已稳定' in s['pauseReason'])
    assert empty['tick'] == 0 and not empty['running'] and empty['eventsPerSecond'] == 0
    command('place', pos=[0,0,0], name='stone')
    command('place', pos=[0,1,0], name='stone_button', properties={'face':'floor'})
    command('interact', pos=[0,1,0]); command('play')
    stable = waitStatus(lambda s: s['blocks'] == 2 and '已稳定' in s['pauseReason'])
    assert stable['tick'] == 20 and not stable['running'], stable
    assert stable['eventsPerSecond'] == 0
    command('step', count=10000)
    idle = waitStatus(lambda s: s['tick'] == 10020)
    assert idle['eventsPerSecond'] == 0, 'manual idle jump counted as throughput'
    command('new')
    for x, facing in [(0,'east'), (1,'west'), (0,'south'), (0,'east')]:
        command('place', pos=[x,0,0], name='observer', properties={'facing':facing})
    command('play')
    active = waitStatus(lambda s: s['running'] and s['eventsPerSecond'] > 0)
    assert active['events'] > 0 and active['pending'] > 0
    command('pause')
    paused = waitStatus(lambda s: not s['running'])
    assert paused['eventsPerSecond'] == 0
    print('PASS: unlimited empty/stable stop, exact final tick, idle/manual exclusion and measured active events')
finally:
    command('pause'); command('speed', value=20); command('load', project=original); client.close()
