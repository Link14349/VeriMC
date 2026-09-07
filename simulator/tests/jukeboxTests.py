#!/usr/bin/env python3
"""Jukebox playback, retained comparator, checkpoint and hopper protocol checks."""
import copy
import json
import subprocess
import sys
from serverClient import ServerClient

client = ServerClient(int(sys.argv[1]) if len(sys.argv) > 1 else 28765)
command = client.command
original = command('save', checkpoint=True)
pos = [0, 1, 0]

def disc(item):
    return command('stimulate', pos=pos, stimulus={'inventory': [{'slot': 0, 'item': item, 'count': 1}]})

try:
    command('new')
    command('place', pos=pos, name='jukebox')
    command('probe', pos=pos, mode='analog')
    disc('music_disc_11')
    state = command('inspect', pos=pos)
    assert state['value'] == 15 and state['analog'] == 11
    assert state['jukebox']['elapsed'] == 0
    duration = state['jukebox']['durationTicks']
    command('step', count=7)
    checkpoint = command('save', checkpoint=True)
    assert command('inspect', pos=pos)['jukebox']['elapsed'] == 7
    invalid = copy.deepcopy(checkpoint)
    invalid['jukeboxes'][0]['song'] = 'minecraft:cat'
    assert command('load', project=invalid)['type'] == 'error'
    assert command('inspect', pos=pos)['jukebox']['elapsed'] == 7
    assert disc('stone')['type'] == 'error'
    command('step', count=duration - 7)
    assert command('inspect', pos=pos)['jukebox']['playing']
    command('step', count=1)
    finished = command('inspect', pos=pos)
    assert not finished['jukebox']['playing'] and finished['value'] == 0
    assert finished['analog'] == 11 and finished['inventory'][0]['count'] == 1
    command('load', project=checkpoint)
    command('step', count=duration - 6)
    assert command('inspect', pos=pos)['jukebox'] == finished['jukebox']

    # A playing disc locks the hopper beneath it; completion unlocks extraction.
    command('new')
    command('place', pos=[0, 0, 0], name='hopper', properties={'facing': 'east'})
    command('place', pos=pos, name='jukebox')
    disc('music_disc_11')
    command('step', count=1)
    assert command('inspect', pos=[0, 0, 0])['properties']['enabled'] == 'false'
    assert command('inspect', pos=pos)['jukebox']['playing']
    command('step', count=duration + 1)
    assert not command('inspect', pos=pos)['inventory']
    assert command('inspect', pos=[0, 0, 0])['inventory'][0]['item'] == 'minecraft:music_disc_11'

    # Idle UINT64_MAX sentinels must survive JSON round trips and reject negatives.
    command('place', pos=[100, 1, 0], name='sculk_sensor')
    idle = command('save', checkpoint=True)
    assert idle['jukeboxes'][0]['wakeAt'] is None and idle['sensors'][0]['wakeAt'] is None
    browserJson = subprocess.check_output(['node', '-e', 'let s="";process.stdin.on("data",d=>s+=d);process.stdin.on("end",()=>process.stdout.write(JSON.stringify(JSON.parse(s))))'], input=json.dumps(idle).encode())
    assert command('load', project=json.loads(browserJson))['type'] == 'reply'
    invalid = copy.deepcopy(idle)
    invalid['jukeboxes'][0]['wakeAt'] = -1
    assert command('load', project=invalid)['type'] == 'error'
    print('PASS: jukebox playback, end padding, comparator retention, checkpoint, filtering and hopper release')
finally:
    command('load', project=original)
    client.close()
