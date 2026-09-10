#!/usr/bin/env python3
"""Generate the per-block support inventory from the registry and the vanilla capability export.

Inputs:
  1. the JSON written by `exportSupportInventory` (support level per block type, from the kernel)
  2. `tests/fixtures/java26_2BlockCapabilities.json` (which redstone callbacks 26.2 actually overrides)

The document is generated, never hand-maintained, so a palette change shows up as a diff.
"""
import collections
import json
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
inventory = json.loads(Path(sys.argv[1]).read_text(encoding='utf-8'))
capabilities = json.loads((root / 'tests/fixtures/java26_2BlockCapabilities.json').read_text(encoding='utf-8'))
output = Path(sys.argv[2]) if len(sys.argv) > 2 else root / 'docs/redstoneAudit/supportInventory.md'

relevant = {row['name']: row for row in capabilities['blocks']}
types = inventory['types']
levels = collections.Counter(t['supportLevel'] for t in types)
relevantTypes = [t for t in types if t['name'] in relevant]
relevantLevels = collections.Counter(t['supportLevel'] for t in relevantTypes)

supported = collections.defaultdict(list)
refused = collections.defaultdict(list)
for t in relevantTypes:
    target = refused if t['supportLevel'] == 'unimplemented' else supported
    target[(t['className'], t['supportLevel'])].append(t['name'])

lines = []
add = lines.append
add('# 固定版本逐方块支持清单')
add('')
add('本文件由 `tools/buildSupportInventory.py` 生成，输入是 `exportSupportInventory` 导出的内核支持等级')
add('与 `tests/fixtures/java26_2BlockCapabilities.json`（用反射记录 26.2 每个方块**实际重写**了哪些红石相关回调）。')
add('不要手工编辑；调色板或 `supportLevel` 变化时重新生成并复核。')
add('')
add('“红石相关”的判据是：方块状态是信号源、有模拟量输出，或方块类重写了 `neighborChanged`、`tick`、')
add('`triggerEvent`、`affectNeighborsAfterRemoval`、`getSignal`、`getDirectSignal`、`ownSignal` 之一。')
add('纯形状/碰撞重写（栅栏、墙等）不计入，因为它们不带信号行为。')
add('')
add(f'- 注册表方块总数：**{len(types)}**')
add(f'- 其中红石相关：**{len(relevantTypes)}**')
add('- 全部方块按支持等级：' + '、'.join(f'{k} {v}' for k, v in sorted(levels.items())))
add('- 红石相关方块按支持等级：' + '、'.join(f'{k} {v}' for k, v in sorted(relevantLevels.items())))
add('')
add('支持等级的含义：`implemented` 有器件实现并有原版差分；`partial` 有实现但存在明确缺口；')
add('`externalStimulus` 需要显式环境输入才能驱动，不自动产生世界事件；`unimplemented` 一律拒绝放置。')
add('')
add('## 已开放的红石相关方块')
add('')
add('| 方块类 | 支持等级 | 数量 | 示例 |')
add('|---|---|---:|---|')
for (className, level), names in sorted(supported.items()):
    add(f'| `{className}` | {level} | {len(names)} | {", ".join(names[:3])}{" …" if len(names) > 3 else ""} |')
add('')
add('## 尚未实现、一律拒绝放置的红石相关方块')
add('')
add('这些方块在固定版注册表里确实带红石回调或模拟量输出，但内核没有实现其行为，')
add('`Simulator::place` 会直接拒绝。补支持时逐器件分单，不得只放开 `supportLevel`。')
add('')
add('| 方块类 | 数量 | 方块 |')
add('|---|---:|---|')
for (className, _), names in sorted(refused.items()):
    listed = ', '.join(f'`{n.removeprefix("minecraft:")}`' for n in sorted(names))
    add(f'| `{className}` | {len(names)} | {listed} |')
add('')
add('## 占位耦合门禁')
add('')
add('`Device::dispenser` / `crafter` / `furnace` 在枚举和容量表里有占位值。')
add('`BlockRegistry` 现在会在这些器件的 `supportLevel` 不是 `unimplemented` 时直接抛出，')
add('核心测试 “26.2 redstone capability coverage gate” 也断言这一点，')
add('避免有人只放开支持等级就让它们退化成普通容器。')
add('')
add('## 已确定不在实施范围')
add('')
add('TNT 复制机、流体农场、矿车计算机、依赖完整生物 AI 的机器属于用户已确定的排除项，')
add('不计入待实现功能；实验红石与基岩版规则同样不属于兼容目标。')
add('')
output.write_text('\n'.join(lines) + '\n', encoding='utf-8')
print(f'Wrote {output} ({len(lines)} lines)')
