"""Small synchronous protocol client for local simulator integration tests."""
import base64
import http.client
import json
import os
import socket
import struct


class ServerClient:
    def __init__(self, port=28765):
        self.host = f'127.0.0.1:{port}'
        httpConnection = http.client.HTTPConnection('127.0.0.1', port, timeout=5)
        httpConnection.request('GET', '/api/bootstrap')
        bootstrap = json.loads(httpConnection.getresponse().read())
        assert bootstrap['version'] == '26.2'
        httpConnection.close()
        httpConnection = http.client.HTTPConnection('127.0.0.1', port, timeout=5)
        httpConnection.request('GET', '/api/bootstrap', headers={'Host': 'untrusted.example'})
        assert httpConnection.getresponse().status == 403, 'Host validation failed'
        httpConnection.close()
        self.transport = socket.create_connection(('127.0.0.1', port), timeout=5)
        key = base64.b64encode(os.urandom(16)).decode()
        self.transport.sendall((f'GET /socket?token={bootstrap["token"]} HTTP/1.1\r\nHost: {self.host}\r\nOrigin: http://{self.host}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n').encode())
        self.buffer = b''
        while b'\r\n\r\n' not in self.buffer: self.buffer += self.transport.recv(4096)
        header, self.buffer = self.buffer.split(b'\r\n\r\n', 1)
        assert header.startswith(b'HTTP/1.1 101'), header
        self.frames = 0
        self.latestStatus = {}
        self.latestTrace = None
        self.pendingFrame = None
        self.requestId = 0
        self.autoAck = True
        while self.receive().get('type') != 'ready': pass

    def close(self): self.transport.close()

    def exact(self, count):
        while len(self.buffer) < count:
            part = self.transport.recv(max(4096, count-len(self.buffer)))
            if not part: raise RuntimeError('connection closed')
            self.buffer += part
        value, self.buffer = self.buffer[:count], self.buffer[count:]
        return value

    def sendFrame(self, payload, opcode=1):
        mask = os.urandom(4)
        head = bytes([0x80 | opcode])
        length = len(payload)
        if length < 126: head += bytes([0x80 | length])
        elif length < 65536: head += bytes([0x80 | 126]) + struct.pack('>H', length)
        else: head += bytes([0x80 | 127]) + struct.pack('>Q', length)
        self.transport.sendall(head + mask + bytes(byte ^ mask[i % 4] for i, byte in enumerate(payload)))

    def send(self, message): self.sendFrame(json.dumps(message).encode())

    def ackTrace(self, message):
        self.send({'cmd': 'ack', 'frameId': message['frameId'], 'epoch': message['epoch'], 'traceEnd': message['next']})

    def receive(self):
        parts = []
        messageOpcode = None
        while True:
            first, second = self.exact(2); length = second & 127
            if length == 126: length = struct.unpack('>H', self.exact(2))[0]
            elif length == 127: length = struct.unpack('>Q', self.exact(8))[0]
            payload = self.exact(length); opcode = first & 15
            if opcode == 9: self.sendFrame(payload, 10); continue
            if opcode == 8: raise RuntimeError('websocket closed')
            if opcode != 0: messageOpcode = opcode
            parts.append(payload)
            if first & 128: break
        payload = b''.join(parts)
        if messageOpcode == 2:
            magic, kind, frameId, count = struct.unpack_from('<IIII', payload)
            assert magic == 0x32434d56 and kind in (1, 2)
            assert len(payload) == 32 + count * 28 and self.pendingFrame is None
            self.pendingFrame = (frameId, kind == 1)
            self.frames += 1
            return {}
        result = json.loads(payload)
        if result.get('type') == 'ready': assert result['protocolVersion'] == 3
        if result.get('type') == 'status': self.latestStatus = result
        if result.get('type') == 'trace':
            assert self.pendingFrame == (result['frameId'], result['reset']), 'trace has no matching scene'
            assert result['next'] == result['from'] + len(result['edges']), 'trace cursor mismatch'
            if not result['reset']:
                assert self.latestTrace is not None and result['epoch'] == self.latestTrace['epoch']
                assert result['from'] == self.latestTrace['next'], 'unreported trace loss'
            self.pendingFrame = None
            self.latestTrace = result
            if self.autoAck: self.ackTrace(result)
        return result

    def command(self, cmd, **values):
        self.requestId += 1
        self.send({'cmd': cmd, 'requestId': self.requestId, **values})
        while True:
            reply = self.receive()
            if reply.get('requestId') == self.requestId and reply.get('type') in ('reply', 'error'): return reply

    def nextTrace(self):
        while True:
            message = self.receive()
            if message.get('type') == 'trace': return message
