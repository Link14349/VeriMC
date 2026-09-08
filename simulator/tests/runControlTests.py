#!/usr/bin/env python3
"""Live editing preserves run intent; idle time is not simulated event work."""
import sys
import time
from serverClient import ServerClient

client = ServerClient(int(sys.argv[1]) if len(sys.argv) > 1 else 28765)


def command(cmd, **values):
    reply = client.command(cmd, **values)
    assert reply['type'] == 'reply', reply
    return reply


def waitStatus(predicate=lambda status: True, running=None):
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        message = client.receive()
        if message.get('type') != 'status':
            continue
        if running is not None:
            assert message['running'] == running, message
        if predicate(message):
            return message
    raise AssertionError(f'expected run status did not arrive: {client.latestStatus}')


def control(cmd, running, **values):
    command(cmd, **values)
    return waitStatus(running=running)


def observe(duration, check):
    # Keep reading/acknowledging frames while observing, so a slow test client
    # cannot itself trigger the server's recording backpressure protection.
    deadline = time.monotonic() + duration
    while time.monotonic() < deadline:
        check(waitStatus())


def checkpoint():
    return {key: value for key, value in command('save', checkpoint=True).items()
            if key not in ('type', 'cmd', 'requestId')}


def assertIdle(status, tick, running):
    assert status['running'] == running and status['tick'] == tick, status
    assert status['pending'] == 0 and status['eventsPerSecond'] == 0, status
    assert not status['pauseReason'], status


original = checkpoint()
try:
    # An unlimited run waits at the last real event and accepts more work,
    # including when it was started with an entirely empty world.
    command('new'); command('speed', value=0)
    empty = control('play', True)
    assertIdle(empty, 0, True)
    observe(0.15, lambda status: assertIdle(status, 0, True))
    control('place', True, pos=[0, 0, 0], name='stone')
    control('place', True, pos=[0, 1, 0], name='stone_button', properties={'face': 'floor'})
    control('interact', True, pos=[0, 1, 0])
    settledAfter = time.monotonic() + 0.6
    stable = waitStatus(lambda status: status['tick'] == 20 and status['pending'] == 0
                        and status['eventsPerSecond'] == 0 and time.monotonic() >= settledAfter, running=True)
    assert stable['events'] > 0, stable
    assert command('inspect', pos=[0, 1, 0])['properties']['powered'] == 'false'
    observe(0.15, lambda status: assertIdle(status, 20, True))
    control('interact', True, pos=[0, 1, 0])
    resumed = waitStatus(lambda status: status['tick'] == 40 and status['pending'] == 0, running=True)
    assert resumed['events'] > stable['events'], 'new input did not resume event processing'
    control('step', False, count=10000)
    manual = waitStatus(lambda status: status['tick'] == 10040, running=False)
    assert manual['eventsPerSecond'] == 0, 'manual idle jump counted as throughput'

    # At ordinary speed, edit each supported way while a real circuit keeps
    # advancing. Reset must still refer to the snapshot at the original play.
    command('new'); command('speed', value=20)
    blocks = [{'pos': [x, 0, 0], 'name': 'stone'} for x in range(5)]
    blocks += [
        {'pos': [0, 1, 0], 'name': 'stone_button', 'properties': {'face': 'floor'}},
        {'pos': [1, 1, 0], 'name': 'redstone_wire'},
        {'pos': [2, 1, 0], 'name': 'repeater', 'properties': {'facing': 'west', 'delay': '2'}},
        {'pos': [3, 1, 0], 'name': 'redstone_wire'},
        {'pos': [4, 1, 0], 'name': 'redstone_lamp'},
        {'pos': [20, 0, 0], 'name': 'lightning_rod', 'properties': {'facing': 'up'}},
    ]
    command('edit', blocks=blocks)
    started = checkpoint()
    control('play', True)
    control('place', True, pos=[8, 0, 0], name='stone')
    assert command('inspect', pos=[8, 0, 0])['name'] == 'minecraft:stone'
    control('remove', True, pos=[8, 0, 0])
    assert command('inspect', pos=[8, 0, 0])['name'] == 'minecraft:air'
    control('edit', True, blocks=[{'pos': [8, 0, 0], 'name': 'stone'},
                                  {'pos': [9, 0, 0], 'name': 'stone'}])
    pressed = control('interact', True, pos=[0, 1, 0])
    assert pressed['pending'] > 0, 'button and repeater did not schedule circuit work'
    control('place', True, pos=[12, 0, 0], name='stone')
    control('remove', True, pos=[12, 0, 0])
    control('edit', True, blocks=[{'pos': [13, 0, 0], 'name': 'stone'},
                                  {'pos': [14, 0, 0], 'name': 'stone'}])
    waitStatus(lambda status: status['tick'] >= pressed['tick'] + 4, running=True)
    assert command('inspect', pos=[4, 1, 0])['properties']['lit'] == 'true'
    struck = control('stimulate', True, pos=[20, 0, 0], stimulus={})
    assert command('inspect', pos=[20, 0, 0])['value'] == 15
    progressed = waitStatus(lambda status: status['tick'] >= max(pressed['tick'] + 30, struck['tick'] + 8), running=True)
    assert progressed['events'] > pressed['events'], 'live edits left scheduled circuit work unprocessed'
    assert command('inspect', pos=[0, 1, 0])['properties']['powered'] == 'false'
    assert command('inspect', pos=[4, 1, 0])['properties']['lit'] == 'false'
    assert command('inspect', pos=[20, 0, 0])['value'] == 0
    control('place', True, pos=[10, 0, 0], name='stone')
    control('undo', True)
    assert command('inspect', pos=[10, 0, 0])['name'] == 'minecraft:air'
    redone = control('redo', True)
    assert command('inspect', pos=[10, 0, 0])['name'] == 'minecraft:stone'
    waitStatus(lambda status: status['tick'] > redone['tick'], running=True)
    control('reset', False)
    assert checkpoint() == started, 'live editing or undo/redo replaced the original run-start snapshot'

    # Pausing is also persistent intent: editing, interaction and history must
    # not restart the clock or consume their newly scheduled device events.
    control('play', True)
    paused = control('pause', False)
    control('place', False, pos=[8, 0, 0], name='stone')
    control('remove', False, pos=[8, 0, 0])
    control('edit', False, blocks=[{'pos': [9, 0, 0], 'name': 'stone'}])
    control('interact', False, pos=[0, 1, 0])
    control('stimulate', False, pos=[20, 0, 0], stimulus={})
    control('place', False, pos=[10, 0, 0], name='stone')
    control('undo', False)
    assert command('inspect', pos=[10, 0, 0])['name'] == 'minecraft:air'
    pending = control('redo', False)
    assert command('inspect', pos=[10, 0, 0])['name'] == 'minecraft:stone'
    assert pending['tick'] == paused['tick'] and pending['pending'] > 0, pending
    def assertPaused(status):
        assert not status['running'] and status['tick'] == paused['tick'], status
        assert status['pending'] == pending['pending'] and status['eventsPerSecond'] == 0, status
    observe(0.15, assertPaused)
    assert command('inspect', pos=[0, 1, 0])['properties']['powered'] == 'true'
    assert command('inspect', pos=[20, 0, 0])['value'] == 15

    # Live edits must respect an explicitly configured probe breakpoint.
    command('new')
    command('edit', blocks=[{'pos': [0, 0, 0], 'name': 'stone'},
                            {'pos': [0, 1, 0], 'name': 'redstone_wire'}])
    probe = command('probe', pos=[0, 1, 0], name='edit breakpoint')['id']
    command('configureProbe', id=probe, trigger='rising')
    control('play', True)
    stopped = control('place', False, pos=[1, 1, 0], name='redstone_block')
    assert '触发断点' in stopped['pauseReason'], stopped
    assert command('inspect', pos=[0, 1, 0])['value'] == 15

    command('new'); command('speed', value=0)
    for x, facing in [(0, 'east'), (1, 'west'), (0, 'south'), (0, 'east')]:
        command('place', pos=[x, 0, 0], name='observer', properties={'facing': facing})
    control('play', True)
    active = waitStatus(lambda status: status['eventsPerSecond'] > 0, running=True)
    assert active['events'] > 0 and active['pending'] > 0, active
    assert control('pause', False)['eventsPerSecond'] == 0
    print('PASS: live edits, undo/redo, original reset snapshot, persistent manual pause, probe breakpoint, unlimited idle/resume and real event throughput')
finally:
    command('pause'); command('speed', value=20); command('load', project=original); client.close()
