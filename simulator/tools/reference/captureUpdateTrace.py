#!/usr/bin/env python3
"""Intra-tick neighbour/shape update trace, compared position by position against vanilla.

Vanilla exposes the hook itself: `CollectingNeighborUpdater.setDebugListener` is called once
per peek of the update stack with that update's affected positions. The kernel records the
same points. The trace has a fixed capacity and a truncation flag; a truncated trace fails.

Set `trace=False` to capture the identical timeline without the listener, which is how the
"tracing does not change execution" check is done.
"""
import sys

from captureRedstone import runCapture, state

commands = []
watch = []


def put(tick, pos, name, **props):
    commands.append({'tick': tick, 'pos': list(pos), 'stateId': state(name, **props)})


def clear(tick, pos):
    commands.append({'tick': tick, 'pos': list(pos), 'stateId': 0})


# A small circuit that exercises wire strength updates, a diode, a quasi-connected piston and
# a comparator, so the trace covers multi, simple and shape updates in the same tick.
for dx in range(0, 12):
    for dz in range(0, 7):
        put(0, (4 + dx, 1, 4 + dz), 'stone')
put(0, (5, 2, 6), 'lever', face='floor', facing='north')
for x in range(6, 10):
    put(0, (x, 2, 6), 'redstone_wire')
put(0, (10, 2, 6), 'repeater', facing='west')
put(0, (11, 2, 6), 'redstone_wire')
put(0, (12, 2, 6), 'redstone_lamp')
put(0, (11, 2, 7), 'comparator', facing='north')
put(0, (11, 2, 5), 'barrel')
put(0, (9, 3, 6), 'piston', facing='up')
watch.extend([[x, 2, 6] for x in range(6, 13)] + [[11, 2, 7], [9, 3, 6]])

commands.append({'tick': 2, 'pos': [5, 2, 6], 'interact': True})
commands.append({'tick': 8, 'pos': [5, 2, 6], 'interact': True})
commands.append({'tick': 4, 'pos': [11, 2, 5], 'stimulus': {'inventory': [{'slot': 0, 'item': 'stone', 'count': 64}]}})
clear(10, (11, 2, 5))

if __name__ == '__main__':
    trace = '--no-trace' not in sys.argv
    options = {'updateTraceLimit': 40000} if trace else {}
    runCapture(commands, watch, 14, 'java26_2UpdateTrace' if trace else 'java26_2UpdateTraceUntraced', **options)
