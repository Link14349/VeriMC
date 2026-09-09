#!/usr/bin/env python3
"""A hopper in ticket level 32 still ticks; entity-ticking is not its gate.

Only the first chunk loses its forced ticket. Adjacent chunks keep theirs, so
vanilla reports blockTicking=true and entityTicking=false for ticks 3..12.
The explicit kernel input is checked against those observed chunk predicates.
This does not test entity-data loading latency or vibration delivery at an edge.
"""
from captureRedstone import state
from captureVanillaScenario import runVanillaCapture


def main():
    hopper, chest = [4, 1, 4], [5, 1, 4]
    commands = [
        {'tick': 0, 'pos': hopper, 'stateId': state('hopper', facing='east')},
        {'tick': 0, 'pos': chest, 'stateId': state('chest')},
        {'tick': 0, 'pos': hopper, 'stimulus': {'inventory': [
            {'slot': 0, 'item': 'minecraft:stone', 'count': 30}]}},
        {'tick': 2, 'pos': hopper, 'chunkForced': False},
        {'tick': 2, 'pos': hopper, 'chunkState': 'blockTicking'},
        {'tick': 12, 'pos': hopper, 'chunkForced': True},
        {'tick': 12, 'pos': hopper, 'chunkState': 'entityTicking'},
    ]
    runVanillaCapture(commands, [hopper, chest], 20, 'java26_2BlockTicking',
                      [16, -59, 32], watchChunkState=True, requiresAlignedOrigin=True)


if __name__ == '__main__':
    main()
