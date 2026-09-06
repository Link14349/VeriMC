#!/usr/bin/env python3
"""End-to-end local HTTP/WebSocket tests, using only the Python standard library."""
import base64
import http.client
import json
import os
import socket
import struct
import sys

port = int(sys.argv[1]) if len(sys.argv) > 1 else 28765
host = f'127.0.0.1:{port}'
httpConnection = http.client.HTTPConnection('127.0.0.1', port, timeout=5)
httpConnection.request('GET', '/api/bootstrap')
bootstrap = json.loads(httpConnection.getresponse().read())
assert bootstrap['version'] == '26.2'
httpConnection.close()
httpConnection = http.client.HTTPConnection('127.0.0.1', port, timeout=5)
httpConnection.request('GET', '/api/bootstrap', headers={'Host': 'untrusted.example'})
assert httpConnection.getresponse().status == 403, 'Host validation failed'
httpConnection.close()
transport = socket.create_connection(('127.0.0.1', port), timeout=5)
key = base64.b64encode(os.urandom(16)).decode()
transport.sendall((f'GET /socket?token={bootstrap["token"]} HTTP/1.1\r\nHost: {host}\r\nOrigin: http://{host}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n').encode())
buffer = b''
while b'\r\n\r\n' not in buffer: buffer += transport.recv(4096)
header, buffer = buffer.split(b'\r\n\r\n', 1)
assert header.startswith(b'HTTP/1.1 101'), header
def exact(count):
    global buffer
    while len(buffer) < count:
        part = transport.recv(max(4096, count-len(buffer)))
        if not part: raise RuntimeError('connection closed')
        buffer += part
    value, buffer = buffer[:count], buffer[count:]
    return value
def sendFrame(payload, opcode=1):
    mask = os.urandom(4)
    head = bytes([0x80 | opcode])
    length = len(payload)
    if length < 126: head += bytes([0x80 | length])
    elif length < 65536: head += bytes([0x80 | 126]) + struct.pack('>H', length)
    else: head += bytes([0x80 | 127]) + struct.pack('>Q', length)
    transport.sendall(head + mask + bytes(byte ^ mask[i % 4] for i, byte in enumerate(payload)))
def send(message): sendFrame(json.dumps(message).encode())
frames = 0
latestStatus = {}
def receive():
    global frames, latestStatus
    parts = []
    messageOpcode = None
    while True:
        first, second = exact(2); length = second & 127
        if length == 126: length = struct.unpack('>H', exact(2))[0]
        elif length == 127: length = struct.unpack('>Q', exact(8))[0]
        payload = exact(length); opcode = first & 15
        if opcode == 9: sendFrame(payload, 10); continue
        if opcode == 8: raise RuntimeError('websocket closed')
        if opcode != 0: messageOpcode = opcode
        parts.append(payload)
        if first & 128: break
    payload = b''.join(parts)
    if messageOpcode == 2:
        magic, kind, frameId, count = struct.unpack_from('<IIII', payload)
        assert magic == 0x31434d56 and kind in (1, 2)
        assert len(payload) == 32 + count * 20
        send({'cmd': 'ack', 'frameId': frameId}); frames += 1; return {}
    result = json.loads(payload)
    if result.get('type') == 'status': latestStatus = result
    return result
requestId = 0
def command(cmd, **values):
    global requestId
    requestId += 1; send({'cmd': cmd, 'requestId': requestId, **values})
    while True:
        reply = receive()
        if reply.get('requestId') == requestId and reply.get('type') in ('reply', 'error'): return reply
while receive().get('type') != 'ready': pass
original = command('save', checkpoint=True)
try:
    command('new')
    blocks = [{'pos': [x,0,0], 'name': 'stone'} for x in range(5)]
    blocks += [{'pos': [0,1,0], 'name': 'lever', 'properties': {'face': 'floor'}}, {'pos': [1,1,0], 'name': 'redstone_wire'}, {'pos': [2,1,0], 'name': 'repeater', 'properties': {'facing': 'west', 'delay': '2'}}, {'pos': [3,1,0], 'name': 'redstone_wire'}, {'pos': [4,1,0], 'name': 'redstone_lamp'}]
    assert command('edit', blocks=blocks)['type'] == 'reply'
    command('probe', pos=[3,1,0], name='output')
    command('interact', pos=[0,1,0]); command('step', count=3)
    assert command('inspect', pos=[4,1,0])['properties']['lit'] == 'false'
    command('step', count=1)
    assert command('inspect', pos=[4,1,0])['properties']['lit'] == 'true'
    checkpoint = command('save', checkpoint=True)
    command('undo')
    assert command('inspect', pos=[0,1,0])['properties']['powered'] == 'false'
    command('redo')
    assert command('inspect', pos=[4,1,0])['properties']['lit'] == 'true'
    assert command('place', pos=[0,2,0], name='tnt')['type'] == 'error'
    assert command('inspect', pos=[0,2,0])['name'] == 'minecraft:air'
    command('interact', pos=[0,1,0]); command('step', count=12)
    assert command('inspect', pos=[4,1,0])['properties']['lit'] == 'false'
    assert '#200' in command('vcd')['text']
    command('load', project=checkpoint)
    assert command('inspect', pos=[4,1,0])['properties']['lit'] == 'true'
    assert frames > 0
    print(f'PASS: HTTP host validation, binary frames ({frames}), circuit editing, delay, probes, VCD, atomic errors, native undo/redo, checkpoint import')
finally:
    command('load', project=original)
    transport.close()
