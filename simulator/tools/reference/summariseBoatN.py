#!/usr/bin/env python3
"""Reduce a boat-container capture to a per-tick ledger: level-random advances plus what moved.

The number of `nextInt` calls in a tick is recovered by stepping java.util.Random's LCG forward
from the previous frame's 48 bit state until it matches the current one; `nextInt(bound)` with a
non power of two bound can retry, so the count is "seed advances", not "calls", and is reported
as such. Every scenario in this fixture uses bound 1 or 2, both powers of two, so one call is
exactly one advance.
"""
import json
import sys
from pathlib import Path

MULT, ADD, MASK = 0x5DEECE66D, 0xB, (1 << 48) - 1


def advances(previous, current, limit=64):
    seed = previous
    for step in range(limit + 1):
        if seed == current:
            return step
        seed = (seed * MULT + ADD) & MASK
    return None


def main(path):
    capture = json.loads(Path(path).read_text(encoding='utf-8'))
    watch = [tuple(p) for p in capture['watch']]
    frames = capture['frames']
    print('version:', capture.get('version'))
    print('watch:', watch)
    previousState = None
    previousView = None
    for tick, frame in enumerate(frames):
        state = frame.get('randomState')
        steps = None if previousState is None or state is None else advances(previousState, state)
        view = {}
        for index, pos in enumerate(watch):
            entities = frame.get('containerEntities', [[]] * len(watch))[index]
            inventory = frame.get('inventories', [[]] * len(watch))[index] if 'inventories' in frame else []
            view[pos] = (json.dumps(entities, sort_keys=True), json.dumps(inventory, sort_keys=True))
        changes = []
        if previousView is not None:
            for pos in watch:
                if view[pos] != previousView[pos]:
                    changes.append((pos, previousView[pos], view[pos]))
        if steps or changes:
            print('tick %d advances=%s' % (tick, steps))
            for pos, old, new in changes:
                if old[0] != new[0]:
                    print('    %s entities %s -> %s' % (list(pos), old[0], new[0]))
                if old[1] != new[1]:
                    print('    %s hopper   %s -> %s' % (list(pos), old[1], new[1]))
        previousState, previousView = state, view


if __name__ == '__main__':
    main(sys.argv[1])
