#!/usr/bin/env python3
"""Reduce the boat-box probe capture to the set of cells that report each declared entity."""
import json
from pathlib import Path

path = Path(__file__).resolve().parents[2] / 'testResults/boatBoxProbeN/probeBoatBox.json'
capture = json.loads(path.read_text(encoding='utf-8'))
watch = [tuple(p) for p in capture['watch']]
centres = {'oak_chest_boat': (10, 2, 10), 'chest_minecart': (20, 2, 10)}

for tick, frame in enumerate(capture['frames']):
    if 'containerEntities' not in frame:
        continue
    hits = {}
    for pos, entities in zip(watch, frame['containerEntities']):
        for row in entities:
            hits.setdefault(row['type'], []).append(pos)
    print('tick', tick)
    for kind, cells in sorted(hits.items()):
        cx, cy, cz = centres[kind]
        rel = sorted((p[0] - cx, p[1] - cy, p[2] - cz) for p in cells)
        spanX = sorted({r[0] for r in rel})
        spanY = sorted({r[1] for r in rel})
        spanZ = sorted({r[2] for r in rel})
        print('  %-16s cells=%d x=%s y=%s z=%s' % (kind, len(rel), spanX, spanY, spanZ))
        print('   ', rel)
