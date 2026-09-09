#!/usr/bin/env python3
"""R13 探针：把已有捕获场景重跑一遍，只为找出「Full 更新的快照 != 执行时世界状态」的实例。

`CaptureRedstone` 的 `snapshotWitness` 选项在每次 peek 到 `FullNeighborUpdate` 时比较
`update.state` 与 `level.getBlockState(update.pos)`，不一致就记一行。输出写到临时目录，
不进 `tests/fixtures`，所以这个脚本只是搜索工具，不产生受检夹具。

用法：`probeSnapshotWitness.py 输出目录 模块名[:结束刻] [...]`
模块名是同目录下的 capture 脚本（不带 .py），例如 `captureUpdateSource:14`。
"""
import importlib
import json
from pathlib import Path
import sys

import captureRedstone


def probe(moduleName, outputDir, endTick):
    module = importlib.import_module(moduleName)
    outputPath = Path(outputDir) / (moduleName + '.json')
    options = dict(getattr(module, 'probeOptions', {}))
    options.setdefault('discardDrops', True)
    options['updateTraceLimit'] = max(options.get('updateTraceLimit', 0), 400000)
    options['snapshotWitness'] = True
    captureRedstone.runCapture(module.commands, module.watch, endTick, capturePath=str(outputPath), **options)
    rows = json.loads(outputPath.read_text()).get('fullSnapshotMismatch', [])
    print(f'{moduleName}: {len(rows)} mismatch(es) -> {outputPath}')
    for row in rows[:40]:
        print('   ', json.dumps(row, ensure_ascii=False))
    return rows


if __name__ == '__main__':
    target = Path(sys.argv[1])
    target.mkdir(parents=True, exist_ok=True)
    total = 0
    for spec in sys.argv[2:]:
        name, _, ticks = spec.partition(':')
        total += len(probe(name, target, int(ticks) if ticks else 24))
    print('total mismatches:', total)
