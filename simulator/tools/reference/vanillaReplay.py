#!/usr/bin/env python3
"""Replay committed fixtures on the strict vanilla-only reference server.

Each fixture already records the absolute origin GameTest happened to pick, and (once
re-captured) the game/day time at its first timeline tick. `CaptureRedstoneVanilla` boots its own
server whose enabled features are exactly `FeatureFlags.VANILLA_SET` with only the `vanilla`
datapack, prepares the same region GameTest prepares, and replays the identical timeline there.

Two things are checked per fixture:

1. the vanilla-only capture is compared field by field against the GameTest capture, which is the
   `trade_rebalance` on/off comparison at a *fixed* origin rather than two independent draws;
2. the vanilla-only capture is then handed to `checkReference`, so the C++ kernel is also
   validated against strict vanilla-only output and not only against the GameTest environment.

Usage: `vanillaReplay.py <fixture-name|all> <output directory>`
"""
import hashlib
import json
from pathlib import Path
import subprocess
import sys

rootDir = Path(__file__).resolve().parents[2]
cacheDir = rootDir / '.cache/reference'
fixtureDir = rootDir / 'tests/fixtures'
checker = rootDir / 'buildAudit/checkReference'

# The environment block intentionally differs between the two harnesses; everything else must not.
environmentOnly = {'reference', 'referenceEnvironment'}


def compare(expected, actual):
    """Returns the first differing path between two captures, ignoring the environment block."""
    for key in sorted(set(expected) | set(actual)):
        if key in environmentOnly:
            continue
        if key not in expected or key not in actual:
            return f'{key}: present in only one capture'
        if expected[key] == actual[key]:
            continue
        if key == 'frames':
            for frame in range(max(len(expected[key]), len(actual[key]))):
                if frame >= len(expected[key]) or frame >= len(actual[key]):
                    return f'frames: tick count differs ({len(expected[key])} vs {len(actual[key])})'
                a, b = expected[key][frame], actual[key][frame]
                if a == b:
                    continue
                for field in sorted(set(a) | set(b)):
                    if a.get(field) != b.get(field):
                        return f'frames[{frame}].{field}: {json.dumps(a.get(field))[:160]} vs {json.dumps(b.get(field))[:160]}'
        return f'{key}: differs'
    return None


def runOnce(name, fixture, outputDir, suffix, trace):
    origin = fixture['origin']
    environment = fixture.get('referenceEnvironment', {})
    scenario = {key: value for key, value in fixture.items()
                if key not in {'frames', 'origin', 'reference', 'referenceEnvironment', 'updateTrace',
                               'updateTraceLimit', 'updateTraceTruncated'}}
    if trace and 'updateTraceLimit' in fixture:
        scenario['updateTraceLimit'] = fixture['updateTraceLimit']
    scenarioPath = outputDir / (name + suffix + '.scenario.json')
    scenarioPath.write_text(json.dumps(scenario, indent=2), encoding='utf-8')
    capturePath = outputDir / (name + suffix + '.vanilla.json')
    arguments = [str(scenarioPath), str(capturePath), str(cacheDir / 'vanillaUniverse'),
                 str(origin[0]), str(origin[1]), str(origin[2])]
    if 'gameTime' in environment:
        arguments += [str(environment['gameTime']), str(environment['clockTicks'])]
    subprocess.run(['python3', str(Path(__file__).with_name('runReferenceTool.py')), 'CaptureRedstoneVanilla'] + arguments, check=True)
    return capturePath, json.loads(capturePath.read_text(encoding='utf-8'))


def replay(name, outputDir):
    fixture = json.loads((fixtureDir / (name + '.json')).read_text(encoding='utf-8'))
    origin = fixture['origin']
    environment = fixture.get('referenceEnvironment', {})
    capturePath, captured = runOnce(name, fixture, outputDir, '', True)
    difference = compare(fixture, captured)
    result = {
        'fixture': name,
        'origin': origin,
        'gameTestEnvironment': environment,
        'vanillaEnvironment': captured.get('referenceEnvironment', {}),
        'clockAligned': 'gameTime' in environment,
        'firstDifferenceAgainstGameTest': difference,
        'sha256': hashlib.sha256(capturePath.read_bytes()).hexdigest(),
    }
    if checker.exists():
        check = subprocess.run([str(checker), str(capturePath)], capture_output=True, text=True)
        result['checkerExitCode'] = check.returncode
        try:
            report = json.loads(check.stdout)
        except json.JSONDecodeError:
            report = {'status': 'unparsable', 'stdout': check.stdout[-400:], 'stderr': check.stderr[-400:]}
        # A non-zero exit never counts as agreement, whatever the report says.
        result['checkerStatus'] = report.get('status') if check.returncode == 0 else 'failed'
        for key in ('firstDifference', 'firstTraceDifference'):
            if report.get(key):
                result[key] = report[key]
    if 'updateTraceLimit' in fixture:
        # The tracing on/off control the previous round could not run: same origin, same clock,
        # same feature set, only the debug listener differs. Anything but an identical timeline
        # would mean the trace itself perturbs execution.
        _, untraced = runOnce(name, fixture, outputDir, '.noTrace', False)
        result['traceFreeRerunDifference'] = compare(
            {k: v for k, v in captured.items() if not k.startswith('updateTrace')}, untraced)
        result['traceFreeRerunSameOrigin'] = captured['origin'] == untraced['origin']
    return result


def main():
    name, outputDir = sys.argv[1], Path(sys.argv[2])
    outputDir.mkdir(parents=True, exist_ok=True)
    if name == 'all':
        # Only the timeline fixtures can be replayed; the rest are exported tables (sine, registry
        # capabilities, composting rules) with no origin and no per-tick observations.
        names = sorted(p.stem for p in fixtureDir.glob('*.json')
                       if {'commands', 'watch', 'frames', 'origin'} <= set(json.loads(p.read_text(encoding='utf-8'))))
    else:
        names = [name]
    results = []
    for fixtureName in names:
        results.append(replay(fixtureName, outputDir))
        last = results[-1]
        control = ''
        if 'traceFreeRerunDifference' in last:
            control = ' | traceOff ' + ('same' if last['traceFreeRerunDifference'] is None else last['traceFreeRerunDifference'])
        print(fixtureName, last['vanillaEnvironment'].get('featureFlags'),
              'sameAsGameTest' if last['firstDifferenceAgainstGameTest'] is None else last['firstDifferenceAgainstGameTest'],
              '| checker', last.get('checkerStatus'), control, flush=True)
    (outputDir / 'vanillaReplayResults.json').write_text(json.dumps(results, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    identical = sum(1 for r in results if r['firstDifferenceAgainstGameTest'] is None)
    matched = sum(1 for r in results if r.get('checkerStatus') == 'match')
    controls = [r for r in results if 'traceFreeRerunDifference' in r]
    controlled = sum(1 for r in controls if r['traceFreeRerunDifference'] is None and r['traceFreeRerunSameOrigin'])
    print(f'{identical}/{len(results)} identical to the GameTest capture, {matched}/{len(results)} match the C++ kernel, '
          f'{controlled}/{len(controls)} unchanged with tracing off at the same origin')
    return 0 if identical == len(results) and matched == len(results) and controlled == len(controls) else 1


if __name__ == '__main__':
    sys.exit(main())
