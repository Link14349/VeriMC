#!/usr/bin/env python3
"""从 --probe 捕获里读出掉落物相对漏斗方块角点的**精确**落点（不做四舍五入）。"""
import json
import sys

data = json.loads(open(sys.argv[1], encoding='utf-8').read())
hopper = data['watch'][1]
last = None
for frame in data['frames']:
    for item in frame.get('itemEntities', []):
        p = item['position']
        rel = [p[0] - hopper[0], p[1] - hopper[1], p[2] - hopper[2]]
        if last is None or rel != last[1]:
            print('tick', frame['tick'], 'rel-hopper', repr(rel), 'onGround', item['onGround'])
        last = (frame['tick'], rel)
print('final tick', last[0], 'REST =', {'x': last[1][0], 'y': last[1][1], 'z': last[1][2]})
