#!/usr/bin/env python3
"""打印 auditReference 报告里每个场景的状态与基线首个差异，并列出所有用了
groundItems 刺激的签入 fixture（用来确认捕获器改动的影响面）。"""
import json
from pathlib import Path
import sys

report = json.loads(Path(sys.argv[1]).read_text(encoding='utf-8'))
for row in report['scenarios']:
    print(row['fixture'], row['status'], 'native=', row.get('nativeComparison', {}).get('status'))
    print('   baselineFirstDifference:', json.dumps(row.get('baselineFirstDifference'))[:200])
print()
print('用 groundItems 刺激的 fixture：')
for path in sorted(Path(sys.argv[2]).glob('*.json')):
    fixture = json.loads(path.read_text(encoding='utf-8'))
    if not isinstance(fixture, dict):
        continue
    hits = [c for c in fixture.get('commands', []) if 'groundItems' in c.get('stimulus', {})]
    if hits:
        print(' ', path.name, len(hits), '条')
