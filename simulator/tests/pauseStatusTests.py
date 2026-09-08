#!/usr/bin/env python3
"""Pause reasons reach healthy peers even while scene delivery awaits an ACK."""
import select
import sys
import time

from serverClient import ServerClient


if len(sys.argv) != 2 or int(sys.argv[1]) == 28765:
    raise SystemExit('Usage: pauseStatusTests.py <isolated server port; not 28765>')

port = int(sys.argv[1])


def collect(clients, duration):
    """Receive and ACK all available peers without sending application commands."""
    deadline = time.monotonic() + duration
    messages = []
    while time.monotonic() < deadline:
        ready = [client for client in clients if client.buffer]
        if not ready:
            readable, _, _ = select.select(
                [client.transport for client in clients], [], [],
                max(0, deadline - time.monotonic()))
            ready = [client for client in clients if client.transport in readable]
        for client in ready:
            messages.append((client, client.receive()))
    return messages


client = ServerClient(port)
peer = None
original = client.command('save', checkpoint=True)
try:
    client.command('new')
    client.command('speed', value=20)
    client.command('play')
    idle = collect([client], 3)
    statuses = [message for _, message in idle if message.get('type') == 'status']
    assert statuses and all(status['running'] for status in statuses), statuses
    assert statuses[-1]['tick'] >= 40 and statuses[-1]['pauseReason'] == '', statuses[-1]

    # A stale second tab holds its initial frame. The healthy peer continues
    # receiving and ACKing, but cannot acknowledge on behalf of that tab.
    peer = ServerClient(port)
    peer.autoAck = False
    held = peer.nextTrace()
    collect([client, peer], 0.2)
    framesBefore = (client.frames, peer.frames)
    blocked = collect([client, peer], 3)
    paused = [message for owner, message in blocked
              if owner is client and message.get('type') == 'status'
              and not message['running']]
    assert len(paused) == 1, ('healthy peer must receive one passive pause update', paused)
    assert '浏览器未确认数据' in paused[0]['pauseReason'], paused[0]
    assert (client.frames, peer.frames) == framesBefore, 'scene barrier was bypassed'
    quiet = collect([client, peer], 0.4)
    assert not [message for _, message in quiet if message.get('type') == 'status'], \
        'unchanged blocked status was repeatedly queued'
    assert (client.frames, peer.frames) == framesBefore, 'blocked scene was retransmitted'

    # Releasing the exact held frame restores delivery and explicit continuation.
    peer.autoAck = True
    peer.ackTrace(held)
    collect([client, peer], 0.2)
    pausedTick = paused[0]['tick']
    assert client.command('play')['type'] == 'reply'
    collect([client, peer], 0.6)
    for receiver in (client, peer):
        assert receiver.latestStatus['running'], receiver.latestStatus
        assert receiver.latestStatus['pauseReason'] == '', receiver.latestStatus
        assert receiver.latestStatus['tick'] > pausedTick, receiver.latestStatus
    assert client.frames > framesBefore[0] and peer.frames > framesBefore[1]

    # A malformed command already stops the run; both peers must learn why.
    error = client.command('speed', value=-1)
    assert error['type'] == 'error', error
    collect([client, peer], 0.4)
    for receiver in (client, peer):
        assert not receiver.latestStatus['running'], receiver.latestStatus
        assert receiver.latestStatus['pauseReason'] == error['message'], receiver.latestStatus
    print('PASS: idle continuation, passive ACK-stall reason, bounded status delivery, ACK recovery and command-error reason')
finally:
    if peer is not None:
        peer.autoAck = True
        if peer.latestTrace:
            peer.ackTrace(peer.latestTrace)
        peer.close()
    client.command('pause')
    client.command('speed', value=20)
    client.command('load', project=original)
    client.close()
