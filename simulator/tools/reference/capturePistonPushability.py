#!/usr/bin/env python3
"""Original piston pushability scenarios: ordinary blocks, indestructible blocks and glazed terracotta.

Covers the vanilla `getDestroySpeed() == -1` rejection (bedrock, end portal frame)
and the PUSH_ONLY branch that only allows movement along the connection direction.
"""
from captureRedstone import runCapture, state

commands = []
watch = []


def put(tick, pos, name, **props):
    commands.append({'tick': tick, 'pos': list(pos), 'stateId': state(name, **props)})


def row(z, piston, target, targetProps, retract, above=None, aboveProps=None, swapAtTick=None, swapTo=None, swapProps=None):
    for x in range(2, 8):
        put(0, (x, 1, z), 'stone')
    put(0, (3, 2, z), piston, facing='east')
    put(0, (4, 2, z), target, **targetProps)
    if above is not None:
        put(0, (4, 3, z), above, **(aboveProps or {}))
    put(2, (2, 2, z), 'redstone_block')
    if swapAtTick is not None:
        put(swapAtTick, (5, 2, z), swapTo, **(swapProps or {}))
    if retract is not None:
        put(retract, (2, 2, z), 'air')
    watch.extend([[3, 2, z], [4, 2, z], [5, 2, z], [6, 2, z]])
    if above is not None:
        watch.extend([[4, 3, z], [5, 3, z]])


# Ordinary pushable block: the reference behaviour the indestructible check must not break.
row(3, 'piston', 'stone', {}, retract=12)
# getDestroySpeed() == -1 with NORMAL push reaction; no block entity, so only hardness rejects it.
row(7, 'piston', 'bedrock', {}, retract=12)
row(11, 'piston', 'end_portal_frame', {'facing': 'north', 'eye': 'false'}, retract=12)
# PUSH_ONLY along the connection direction is allowed; the block's own facing is irrelevant.
row(15, 'piston', 'white_glazed_terracotta', {'facing': 'north'}, retract=12)
# Sticky retraction asks with connection = piston facing, so PUSH_ONLY refuses to be pulled.
row(19, 'sticky_piston', 'stone', {}, retract=12, swapAtTick=8, swapTo='white_glazed_terracotta', swapProps={'facing': 'east'})
# Same timeline with an ordinary block, to show the pull itself works.
row(23, 'sticky_piston', 'stone', {}, retract=12, swapAtTick=8, swapTo='stone')
# Slime branch: the perpendicular connection direction makes PUSH_ONLY refuse and stay behind.
row(27, 'piston', 'slime_block', {}, retract=None, above='white_glazed_terracotta', aboveProps={'facing': 'north'})
row(31, 'piston', 'slime_block', {}, retract=None, above='stone')

if __name__ == '__main__':
    runCapture(commands, watch, endTick=24, fixtureName='java26_2PistonPushability')
