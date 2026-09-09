#!/usr/bin/env python3
"""把 --probe 捕获里的掉落物逐刻轨迹、抛出初值与漏斗观测整理成可读的一张表。"""
import json
import sys

data = json.loads(open(sys.argv[1], encoding='utf-8').read())
origin = data['origin']
hopper = data['watch'][1]
print('origin', origin, 'endTick', data['endTick'])
print('expectedActions:')
print(json.dumps(data.get('expectedActions', []), indent=1))
print()
print('tick |            item position (rel origin)            |        velocity        | onGr | ground | hopperInv')
for frame in data['frames']:
    items = frame.get('itemEntities', [])
    ground = frame['groundItems'][1]
    inv = frame['inventories'][1]
    dropper = frame['inventories'][0]
    if not items and not ground and not inv and frame['tick'] % 10:
        continue
    for item in items or [None]:
        if item is None:
            print(f"{frame['tick']:4d} | {'-':48s} | {'-':22s} | -    | {ground} | {inv} | dropper={dropper}")
            continue
        p = item['position']
        v = item['velocity']
        rel = [round(p[i] - hopper[i], 6) for i in range(3)]
        print(f"{frame['tick']:4d} | abs {p[0]:.5f} {p[1]:.5f} {p[2]:.5f} rel-hopper {rel} "
              f"| {v[0]:+.5f} {v[1]:+.5f} {v[2]:+.5f} | {str(item['onGround'])[0]}    | {ground} | {inv} | dropper={dropper}")
