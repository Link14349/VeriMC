#!/usr/bin/env python3
"""Original ascending-wire scenarios: four orientations and four support types."""
from captureRedstone import runCapture, state

commands = []
watch = []
supports = [('stone', {}), ('glass', {}), ('stone_slab', {'type': 'top'}),
            ('oak_trapdoor', {'half': 'top', 'open': 'false', 'facing': 'north'})]
directions = [(1, 0), (0, 1), (-1, 0), (0, -1)]
for supportIndex, (support, properties) in enumerate(supports):
    for directionIndex, (dx, dz) in enumerate(directions):
        centerX = 5 + directionIndex * 10
        centerZ = 5 + supportIndex * 10
        def position(distance, y):
            return [centerX + dx * distance, y, centerZ + dz * distance]
        def put(tick, pos, name, **props):
            commands.append({'tick': tick, 'pos': pos, 'stateId': state(name, **props)})
        for distance in range(-1, 4):
            put(0, position(distance, 1), 'stone')
        put(0, position(1, 2), support, **properties)
        put(0, position(2, 2), 'stone')
        put(0, position(3, 2), 'stone')
        put(0, position(0, 2), 'redstone_wire')
        put(0, position(1, 3), 'redstone_wire')
        put(0, position(2, 3), 'redstone_wire')
        put(0, position(3, 3), 'redstone_lamp')
        for tick, name in [(0, 'redstone_block'), (10, 'air'), (20, 'redstone_block'), (30, 'air')]:
            put(tick, position(-1, 2), name)
        watch.extend([position(0, 2), position(1, 2), position(1, 3), position(2, 3), position(3, 3)])

if __name__ == '__main__':
    runCapture(commands, watch, endTick=36, fixtureName='java26_2WireGeometry')
