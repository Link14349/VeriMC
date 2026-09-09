#!/usr/bin/env python3
"""逐键比较签入 fixture 与 auditReference 重新捕获出来的 actual.json，列出哪些键不同。

预期只有与捕获原点、游戏时钟相关的键不同：`origin`、`referenceEnvironment`
以及 `expectedActions` 里那两个**绝对**坐标（position/positionBits）。
frames 必须逐字节相同，否则说明观测本身不可复现。
"""
import json
from pathlib import Path
import sys

left = json.loads(Path(sys.argv[1]).read_text(encoding='utf-8'))
right = json.loads(Path(sys.argv[2]).read_text(encoding='utf-8'))
for key in sorted(set(left) | set(right)):
    same = left.get(key) == right.get(key)
    print(('same     ' if same else 'DIFFERENT'), key)
if left['expectedActions'] != right['expectedActions']:
    for a, b in zip(left['expectedActions'], right['expectedActions']):
        for field in sorted(set(a) | set(b)):
            if a.get(field) != b.get(field):
                print('  expectedActions', field, a.get(field), '->', b.get(field))
