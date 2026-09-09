#!/usr/bin/env python3
"""Negative controls for `java26_2BoatContainers`: mutate the **expected** frames and check that
`checkReference` refuses them. A fixture that only ever reports `match` proves nothing unless the
comparison is shown to be able to fail on exactly the facts the scenario is about.

Four mutants, one per claim:
  droppedIdleDraw   ticks 20.. rewound by one LCG step -> the idle raft's per-tick redraw is real
  swappedOrderD     D's two entity rows swapped at tick 53 -> candidate order is the spawn order
  boatKeepsItem     A's boat keeps its stone at tick 5 -> the single-candidate pull really happened
  emptyRaftPush     E's raft stays empty at tick 77 -> the push into the raft really happened
"""
import copy
import json
from pathlib import Path

MULT, ADD, MASK = 0x5DEECE66D, 0xB, (1 << 48) - 1
INVERSE = pow(MULT, -1, 1 << 48)

root = Path(__file__).resolve().parents[2]
source = root / 'tests/fixtures/java26_2BoatContainers.json'
outputDir = root / 'testResults/boatMutantsN'
capture = json.loads(source.read_text(encoding='utf-8'))
watch = [tuple(p) for p in capture['watch']]


def write(name, mutate):
    mutant = copy.deepcopy(capture)
    mutate(mutant)
    path = outputDir / (name + '.json')
    path.write_text(json.dumps(mutant, indent=2), encoding='utf-8')
    return path


def droppedIdleDraw(mutant):
    for frame in mutant['frames']:
        if frame['tick'] >= 20:
            frame['randomState'] = ((frame['randomState'] - ADD) * INVERSE) & MASK


def swappedOrderD(mutant):
    index = watch.index((25, 2, 4))
    for frame in mutant['frames']:
        if frame['tick'] >= 53:
            frame['containerEntities'][index] = list(reversed(frame['containerEntities'][index]))


def boatKeepsItem(mutant):
    index = watch.index((4, 2, 4))
    for frame in mutant['frames']:
        if frame['tick'] == 5:
            for row in frame['containerEntities'][index]:
                for stack in row['inventory']:
                    if stack['slot'] == 0:
                        stack['count'] += 1


def emptyRaftPush(mutant):
    index = watch.index((32, 1, 4))
    for frame in mutant['frames']:
        if frame['tick'] == 77:
            for row in frame['containerEntities'][index]:
                row['inventory'] = []


if __name__ == '__main__':
    outputDir.mkdir(parents=True, exist_ok=True)
    for name, mutate in [('droppedIdleDraw', droppedIdleDraw), ('swappedOrderD', swappedOrderD),
                         ('boatKeepsItem', boatKeepsItem), ('emptyRaftPush', emptyRaftPush)]:
        print(write(name, mutate))
