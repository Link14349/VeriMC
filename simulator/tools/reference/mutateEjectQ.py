#!/usr/bin/env python3
"""对 eject fixture 做定向改动，确认这条闭环确实在断言而不是恰好通过。

用法：mutateEjectQ.py <fixture> <输出目录>；随后对目录里每个 json 跑 checkReference，
除 `baseline` 外**都必须失败**。
"""
import copy
import json
from pathlib import Path
import sys

source = Path(sys.argv[1])
target = Path(sys.argv[2])
target.mkdir(parents=True, exist_ok=False)
base = json.loads(source.read_text(encoding='utf-8'))


def groundCommand(fixture):
    for command in fixture['commands']:
        if 'stimulus' in command and 'groundItems' in command['stimulus']:
            return command
    raise SystemExit('fixture 里没有 groundItems 命令')


def mutant(name, edit):
    fixture = copy.deepcopy(base)
    edit(fixture)
    (target / (name + '.json')).write_text(json.dumps(fixture), encoding='utf-8')


mutant('baseline', lambda f: None)
mutant('noExpectedActions', lambda f: f.pop('expectedActions'))
mutant('emptyExpectedActions', lambda f: f.__setitem__('expectedActions', []))
mutant('wrongTick', lambda f: f['expectedActions'][0].__setitem__('tick', 7))
mutant('wrongSource', lambda f: f['expectedActions'][0].__setitem__('source', [10, 6, 11]))
mutant('wrongCount', lambda f: f['expectedActions'][0].__setitem__('count', 2))
mutant('wrongVelocityBit', lambda f: f['expectedActions'][0]['velocityBits'].__setitem__(1, '3fce1663bec018a4'))
mutant('wrongPosition', lambda f: f['expectedActions'][0]['position'].__setitem__(1, -52.0))
mutant('noLanding', lambda f: f.__setitem__('commands', [c for c in f['commands'] if c is not groundCommand(f)]))
mutant('lateLanding', lambda f: groundCommand(f).__setitem__('tick', 19))
mutant('landingOutOfRange', lambda f: groundCommand(f)['stimulus']['groundItems'][0].__setitem__('y', 2.5))
print('\n'.join(sorted(p.name for p in target.glob('*.json'))))
