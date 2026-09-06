#!/usr/bin/env python3
"""Build original fixtures and run Mojang's GameTest entry point; no client or EULA server configuration needed."""
import gzip
import json
from pathlib import Path
import struct
import subprocess

rootDir = Path(__file__).resolve().parents[2]
cacheDir = rootDir / '.cache/reference'
packDir = cacheDir / 'fixturePacks/simulator'
for folder in ['test_instance', 'structure']:
    (packDir / 'data/simulator' / folder).mkdir(parents=True, exist_ok=True)
(packDir / 'pack.mcmeta').write_text(json.dumps({'pack': {'description': 'Simulator original redstone reference tests', 'min_format': [107, 1], 'max_format': [107, 1]}}))
(packDir / 'data/simulator/test_instance/capture.json').write_text(json.dumps({'type': 'minecraft:function', 'function': 'simulator:capture', 'environment': {'type': 'minecraft:function'}, 'structure': 'simulator:empty', 'max_ticks': 100}))
def utf(value):
    data = value.encode(); return struct.pack('>H', len(data)) + data
def integer(name, value): return b'\x03' + utf(name) + struct.pack('>i', value)
def intList(name, values): return b'\x09' + utf(name) + b'\x03' + struct.pack('>i', len(values)) + b''.join(struct.pack('>i', v) for v in values)
# Original empty structure NBT, not copied game assets.
nbt = b'\x0a\x00\x00' + integer('DataVersion', 4903) + intList('size', [48, 6, 48])
nbt += b'\x09' + utf('palette') + b'\x0a' + struct.pack('>i', 1) + b'\x08' + utf('Name') + utf('minecraft:air') + b'\x00'
for name in ['blocks', 'entities']: nbt += b'\x09' + utf(name) + b'\x0a\x00\x00\x00\x00'
(packDir / 'data/simulator/structure/empty.nbt').write_bytes(gzip.compress(nbt + b'\x00'))
registry = json.loads((rootDir / 'data/blockStates.json').read_text())
blocks = {b['name'].removeprefix('minecraft:'): b for b in registry['blocks']}
def state(name, **props):
    block = blocks[name]
    base = next(s for s in block['states'] if s['id'] == block['defaultState'])['properties'] | props
    return next(s['id'] for s in block['states'] if s['properties'] == base)
commands = []
watch = []
def setBlock(tick, p, name, **props): commands.append({'tick': tick, 'pos': list(p), 'stateId': state(name, **props)})
for z in [2, 6, 10, 14, 18, 22]:
    for x in range(1, 22): setBlock(0, (x, 1, z), 'stone')
# Decay and directional connections, including the unpowered tail.
for x in range(3, 21): setBlock(0, (x, 2, 2), 'redstone_wire'); watch.append([x, 2, 2])
setBlock(0, (2, 2, 2), 'redstone_block'); setBlock(10, (2, 2, 2), 'air')
# Repeater short pulse, four game tick delay; no player-placement shortcut.
setBlock(0, (3, 2, 6), 'repeater', facing='west', delay='2'); setBlock(0, (4, 2, 6), 'redstone_wire')
setBlock(0, (2, 2, 6), 'redstone_block'); setBlock(1, (2, 2, 6), 'air'); watch += [[3,2,6],[4,2,6]]
# Observer and bulb on each pulse.
setBlock(0, (3, 2, 10), 'observer', facing='west'); setBlock(0, (4, 2, 10), 'copper_bulb'); setBlock(0, (2, 2, 10), 'stone'); setBlock(12, (2, 2, 10), 'air'); watch += [[3,2,10],[4,2,10]]
# Torch inversion into delayed lamp.
setBlock(0, (3, 2, 14), 'redstone_torch'); setBlock(0, (3, 3, 14), 'redstone_lamp'); setBlock(0, (2, 1, 14), 'lever', face='wall', facing='west', powered='true'); watch += [[3,2,14],[3,3,14]]
setBlock(12, (2, 1, 14), 'lever', face='wall', facing='west', powered='false')
# Comparator read-through and subtract.
setBlock(0, (3, 2, 18), 'comparator', facing='west'); setBlock(0, (4, 2, 18), 'redstone_wire'); setBlock(0, (2, 2, 18), 'copper_bulb', lit='true'); watch += [[3,2,18],[4,2,18]]
setBlock(10, (3, 2, 18), 'comparator', facing='west', mode='subtract'); setBlock(10, (3, 2, 19), 'redstone_block')
# Lock transition while main input is high.
setBlock(0, (3, 1, 23), 'stone'); setBlock(0, (3, 2, 22), 'repeater', facing='west'); setBlock(0, (3, 2, 23), 'repeater', facing='south', powered='true'); setBlock(0, (2, 2, 22), 'redstone_block'); setBlock(10, (3, 2, 23), 'air'); watch.append([3,2,22])
# Normal push, sticky pull and short-pulse block dropping.
for z, piston, pulse in [(26, 'piston', 8), (30, 'sticky_piston', 8), (34, 'sticky_piston', 1)]:
    setBlock(0, (2,2,z), piston, facing='east'); setBlock(0, (3,2,z), 'stone')
    setBlock(0, (1,2,z), 'redstone_block'); setBlock(pulse, (1,2,z), 'air')
    watch += [[x,2,z] for x in range(2,5)]
# Quasi-connectivity requires an actual neighbor notification.
setBlock(0, (2,2,38), 'piston', facing='east'); setBlock(0, (2,4,38), 'redstone_block')
setBlock(4, (1,2,38), 'stone'); setBlock(10, (2,4,38), 'air'); setBlock(14, (1,2,38), 'air'); watch += [[2,2,38],[3,2,38]]
# Slime branches, with an adjacent honey block that must not stick.
setBlock(0, (2,2,42), 'sticky_piston', facing='east'); setBlock(0, (3,2,42), 'slime_block'); setBlock(0, (3,3,42), 'stone'); setBlock(0, (3,2,43), 'honey_block')
setBlock(0, (1,2,42), 'redstone_block'); setBlock(10, (1,2,42), 'air'); watch += [[2,2,42],[3,2,42],[4,2,42],[3,3,42],[4,3,42],[3,2,43]]
# A thirteenth block blocks the whole move.
setBlock(0, (2,2,46), 'piston', facing='east')
for x in range(3,16): setBlock(0, (x,2,46), 'stone')
setBlock(0, (1,2,46), 'redstone_block'); watch += [[2,2,46],[3,2,46],[15,2,46]]
scenarioPath = cacheDir / 'redstoneScenario.json'
scenarioPath.write_text(json.dumps({'endTick': 24, 'commands': commands, 'watch': watch}, indent=2))
outputPath = rootDir / 'tests/fixtures/java26_2Redstone.json'
outputPath.parent.mkdir(parents=True, exist_ok=True)
subprocess.run(['python3', str(Path(__file__).with_name('runReferenceTool.py')), 'CaptureRedstone', str(scenarioPath), str(outputPath), str(cacheDir / 'captureWorld'), str(packDir.parent), str(cacheDir / 'gameTestReport.xml')], check=True)
