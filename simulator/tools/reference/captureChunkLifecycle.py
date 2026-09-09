#!/usr/bin/env python3
"""Chunk lifecycle: a chunk that stops ticking keeps its pending work and resumes with it.

Run with the strict vanilla-only server at a **chunk-aligned** origin so relative coordinates map
to known chunks: with origin x/z multiples of 16, relative `[0,16)` is the first chunk of the
region, `[16,32)` the second and `[32,48)` the third.

A forced chunk ticks entities; the ring one chunk around it still ticks blocks. To get a chunk
that is loaded but **not** block ticking, its ticket and those of its in-region neighbours are all
removed, leaving it two chunks away from the nearest forced chunk.

Each chunk holds the three kinds of pending work the acceptance asks for:

- a repeater chain (block scheduled ticks),
- a piston (block events),
- a hopper feeding a chest (block entities).

The kernel does not model ticket propagation, so the scenario declares the chunk state as explicit
input. The declaration is not assumed: the capture records vanilla's own `shouldTickBlocksAt`,
`isPositionEntityTicking` and `hasChunkAt` per frame, and the checker compares them against the
kernel's state with a one-frame offset (a command applied at frame T governs the advance into
frame T+1, which is the tick vanilla reports the change in).
"""
import sys

from captureRedstone import state
from captureVanillaScenario import runVanillaCapture

commands = []
watch = []


def put(tick, pos, name, **props):
    commands.append({'tick': tick, 'pos': list(pos), 'stateId': state(name, **props)})


def clear(tick, pos):
    commands.append({'tick': tick, 'pos': list(pos), 'stateId': 0})


def forced(tick, pos, value):
    commands.append({'tick': tick, 'pos': list(pos), 'chunkForced': value})


def chunkState(tick, pos, value):
    commands.append({'tick': tick, 'pos': list(pos), 'chunkState': value})


def group(bx, bz):
    """Scheduled ticks, a block event and a block entity, all inside one chunk."""
    for dx in range(1, 15):
        for dz in range(1, 15):
            put(0, (bx + dx, 0, bz + dz), 'stone')
    # Scheduled ticks. The repeater FACING property points at its input side, so a chain running
    # east uses west.
    put(0, (bx + 4, 1, bz + 4), 'lever', face='floor', facing='east', powered='false')
    put(0, (bx + 5, 1, bz + 4), 'repeater', facing='west', delay='4')
    put(0, (bx + 6, 1, bz + 4), 'repeater', facing='west', delay='4')
    put(0, (bx + 7, 1, bz + 4), 'redstone_lamp')
    watch.extend([[bx + 4, 1, bz + 4], [bx + 5, 1, bz + 4], [bx + 6, 1, bz + 4], [bx + 7, 1, bz + 4]])
    # Block event: a piston extending and retracting.
    put(0, (bx + 4, 1, bz + 8), 'piston', facing='east')
    put(0, (bx + 5, 1, bz + 8), 'stone')
    watch.extend([[bx + 4, 1, bz + 8], [bx + 5, 1, bz + 8], [bx + 6, 1, bz + 8]])
    # Block entity: a hopper feeding a chest.
    put(0, (bx + 4, 1, bz + 12), 'hopper', facing='east')
    put(0, (bx + 5, 1, bz + 12), 'chest', facing='north')
    commands.append({'tick': 0, 'pos': [bx + 4, 1, bz + 12],
                     'stimulus': {'inventory': [{'slot': 0, 'item': 'minecraft:stone', 'count': 5}]}})
    watch.extend([[bx + 4, 1, bz + 12], [bx + 5, 1, bz + 12]])
    # Two more block entity timers that must freeze with the chunk: a playing jukebox counts
    # elapsed ticks, and a sculk sensor counts down its vibration delay.
    put(0, (bx + 8, 1, bz + 12), 'jukebox')
    commands.append({'tick': 1, 'pos': [bx + 8, 1, bz + 12],
                     'stimulus': {'inventory': [{'slot': 0, 'item': 'minecraft:music_disc_cat', 'count': 1}]}})
    watch.append([bx + 8, 1, bz + 12])
    put(0, (bx + 8, 1, bz + 8), 'sculk_sensor')
    put(0, (bx + 9, 1, bz + 8), 'redstone_lamp')
    commands.append({'tick': 4, 'pos': [bx + 12, 1, bz + 8],
                     'stimulus': {'gameEvent': 'minecraft:block_place'}})
    watch.extend([[bx + 8, 1, bz + 8], [bx + 9, 1, bz + 8]])


def drive(bx, bz, tick, on):
    """Toggle both the repeater chain and the piston in a group."""
    put(tick, (bx + 4, 1, bz + 4), 'lever', face='floor', facing='east', powered='true' if on else 'false')
    if on:
        put(tick, (bx + 3, 1, bz + 8), 'redstone_block')
    else:
        clear(tick, (bx + 3, 1, bz + 8))


def build(origin, fixtureName):
    commands.clear()
    watch.clear()
    # Stalling chunk: the first chunk of the region. Control chunk: the third, which keeps its
    # ticket the whole time.
    group(0, 0)
    group(32, 32)
    drive(0, 0, 2, True)
    drive(32, 32, 2, True)
    # 6 gt: drop the tickets of the first chunk and its in-region neighbours, so it ends up two
    # chunks away from the nearest forced chunk: loaded, but neither block nor entity ticking.
    for pos in [(4, 1, 4), (20, 1, 4), (4, 1, 20), (20, 1, 20)]:
        forced(6, pos, False)
    chunkState(6, (4, 1, 4), 'loaded')
    # 14 gt: toggle everything again. The control chunk reacts; the stalled chunk must keep the
    # scheduled ticks and the block event pending without running them.
    drive(0, 0, 14, False)
    drive(32, 32, 14, False)
    # 40 gt: give the ticket back. The overdue work all runs once the chunk is ticking again.
    forced(40, (4, 1, 4), True)
    chunkState(40, (4, 1, 4), 'entityTicking')
    # The chunk borders have to fall at known relative coordinates, so this fixture cannot be
    # re-captured through GameTest, which picks its own random origin.
    return runVanillaCapture(commands, watch, 60, fixtureName, origin, watchChunkState=True,
                             watchJukeboxes=True, discardDrops=True, requiresAlignedOrigin=True)


if __name__ == '__main__':
    which = sys.argv[1] if len(sys.argv) > 1 else 'both'
    if which in ('both', 'positive'):
        build([16, -59, 32], 'java26_2ChunkLifecycle')
    if which in ('both', 'negative'):
        # Negative chunk indices exercise the floored division in BlockTicks::chunkAt.
        build([-64, -59, -80], 'java26_2ChunkLifecycleNegative')
