#!/usr/bin/env python3
"""Quasi-connectivity machines repeated across positions, orientations and placement orders.

The same compact machine (lever -> wire -> conductor -> climbing wire -> quasi-connected
piston) is built at sixteen different offsets so the position-dependent seven-entry set
ordering of `DefaultRedstoneWireEvaluator` is exercised with different hash buckets, and at
all six piston facings so `PistonBaseBlock`'s `direction != facing` exclusion is covered.

The intra-tick update trace is enabled, so the comparison is on the update order itself,
not only on per-tick end states.
"""
from captureRedstone import runCapture, state

commands = []
watch = []


def put(tick, pos, name, **props):
    commands.append({'tick': tick, 'pos': list(pos), 'stateId': state(name, **props)})


def interact(tick, pos):
    commands.append({'tick': tick, 'pos': list(pos), 'interact': True})


def machine(x, z, facing, pistonFirst):
    """lever at x, climbing wire two blocks along +x, quasi-connected piston at x+3."""
    for dx in range(-1, 6):
        for dz in range(-1, 2):
            put(0, (x + dx, 1, z + dz), 'stone')
    steps = [
        ('piston', (x + 3, 2, z), {'facing': facing}),
        ('lever', (x, 2, z), {'face': 'floor', 'facing': 'north'}),
        ('redstone_wire', (x + 1, 2, z), {}),
        ('stone', (x + 2, 2, z), {}),
        ('redstone_wire', (x + 2, 3, z), {}),
    ]
    if not pistonFirst:
        steps.append(steps.pop(0))
    for name, pos, props in steps:
        put(0, pos, name, **props)
    watch.extend([[x, 2, z], [x + 1, 2, z], [x + 2, 3, z], [x + 3, 2, z]])
    interact(2, (x, 2, z))
    interact(8, (x, 2, z))


# Sixteen positions: four x offsets and four z offsets, so the low bits of the absolute
# coordinates differ and the wire update set is ordered differently in each copy.
for i in range(4):
    for j in range(4):
        machine(4 + 10 * i, 4 + 3 * j, 'east', pistonFirst=(i + j) % 2 == 0)

# All six piston facings at otherwise identical geometry.
for index, facing in enumerate(['down', 'up', 'north', 'south', 'west', 'east']):
    machine(4 + 7 * index, 22, facing, pistonFirst=index % 2 == 0)

if __name__ == '__main__':
    runCapture(commands, watch, 14, 'java26_2MachineMatrix', updateTraceLimit=400000)
