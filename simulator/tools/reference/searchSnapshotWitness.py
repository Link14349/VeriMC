#!/usr/bin/env python3
"""R13 反例搜索：找一个**可达**的原版场景，让 `FullNeighborUpdate` 携带的方块状态快照
与它真正执行时世界里的状态不同。

判据直接来自 26.2 自己的数据结构：`CollectingNeighborUpdater.FullNeighborUpdate` 记录
入队时的 `BlockState`，`SimpleNeighborUpdate` 执行时才 `level.getBlockState`。捕获端在每次
peek 到 Full 更新时比较这两者（`CaptureRedstone` 的 `snapshotWitness` 选项），不一致就是见证。

复用 `fuzzRedstone.buildCell` 的随机电路生成器（含铁轨、充能铁轨、活塞、红石粉、比较器等），
每轮把 GRID*GRID 个格子塞进一次 GameTest 捕获，所以一轮只付一次服务器启动开销。
命中时打印所在格子与那一格的命令历史，便于手工缩减。

    python3 tools/reference/searchSnapshotWitness.py --seed 1 --rounds 20 --output <目录>
"""
import argparse
import json
from pathlib import Path
import random
import sys

from captureRedstone import runCapture
import fuzzRedstone


def cellOf(position):
    x, _, z = position
    return ((x - fuzzRedstone.ORIGIN) // fuzzRedstone.CELL, (z - fuzzRedstone.ORIGIN) // fuzzRedstone.CELL)


def runRound(seed, roundIndex, ticks, outputPath):
    rng = random.Random((seed << 16) + roundIndex)
    commands, watch, cells = [], [], {}
    for cellX in range(fuzzRedstone.GRID):
        for cellZ in range(fuzzRedstone.GRID):
            cellCommands, cellWatch = fuzzRedstone.buildCell(rng, cellX, cellZ, ticks)
            cells[(cellX, cellZ)] = (cellCommands, cellWatch)
            commands += cellCommands
            watch += cellWatch
    runCapture(commands, watch, ticks, capturePath=str(outputPath),
               lenientInteract=True, discardDrops=True, updateTraceLimit=1, snapshotWitness=True)
    capture = json.loads(outputPath.read_text())
    return capture.get('fullSnapshotMismatch', []), cells


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seed', type=int, default=1)
    parser.add_argument('--rounds', type=int, default=10)
    parser.add_argument('--ticks', type=int, default=12)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    report = {'seed': args.seed, 'rounds': args.rounds, 'ticks': args.ticks, 'hits': []}
    for roundIndex in range(args.rounds):
        path = args.output / f'round{roundIndex}.json'
        rows, cells = runRound(args.seed, roundIndex, args.ticks, path)
        print(roundIndex, len(rows), flush=True)
        for row in rows:
            cell = cellOf(row['pos'])
            hit = dict(row)
            hit['round'] = roundIndex
            hit['cell'] = list(cell)
            hit['cellCommands'] = cells.get(cell, ([], []))[0]
            report['hits'].append(hit)
        (args.output / 'report.json').write_text(json.dumps(report, indent=2, ensure_ascii=False))
    print('total hits:', len(report['hits']))
    return 0


if __name__ == '__main__':
    sys.exit(main())
