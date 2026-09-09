#!/usr/bin/env python3
"""列出 eject fixture 的关键刻：抛出声明、掉落物观测、漏斗与投掷器库存。"""
import json
import sys

data = json.loads(open(sys.argv[1], encoding='utf-8').read())
print('origin', data['origin'], 'endTick', data['endTick'], 'watch', data['watch'])
print('expectedActions', json.dumps(data.get('expectedActions'), indent=1))
previous = None
for frame in data['frames']:
    row = (frame['groundItems'], frame['inventories'], frame['states'])
    if row != previous:
        print(frame['tick'], 'ground', frame['groundItems'], 'inv', frame['inventories'],
              'states', frame['states'], 'rnd', frame.get('randomState'))
    previous = row
