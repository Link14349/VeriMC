#!/usr/bin/env python3
"""Recapture checked-in scenarios without replacing their expected observations.

Requires the pinned reference cache and JDK 25 on PATH. Results/worlds stay in
an explicit, new ignored output directory. A successful comparison establishes
only the observations in those scenarios, not complete Minecraft equivalence.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import zipfile


def firstDifference(expected, actual, path='$'):
    if type(expected) is not type(actual):
        return {'path': path, 'expected': expected, 'actual': actual}
    if isinstance(expected, dict):
        if expected.keys() != actual.keys():
            return {'path': path, 'expectedKeys': sorted(expected), 'actualKeys': sorted(actual)}
        for key in expected:
            difference = firstDifference(expected[key], actual[key], path + '.' + key)
            if difference:
                return difference
    elif isinstance(expected, list):
        if len(expected) != len(actual):
            return {'path': path, 'expectedLength': len(expected), 'actualLength': len(actual)}
        for index, (left, right) in enumerate(zip(expected, actual)):
            difference = firstDifference(left, right, f'{path}[{index}]')
            if difference:
                return difference
    elif expected != actual:
        return {'path': path, 'expected': expected, 'actual': actual}
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--checker', type=Path, help='Built checkReference executable; compare C++ at the captured origin')
    parser.add_argument('fixtures', nargs='*', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    cache = root / '.cache/reference'
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    info = json.loads((root / 'data/referenceVersion.json').read_text())
    if hashlib.sha1((cache / 'server.jar').read_bytes()).hexdigest() != info['serverSha1']:
        raise RuntimeError('Pinned server SHA-1 mismatch')
    with zipfile.ZipFile(cache / 'server.jar') as bundle:
        versionRows = bundle.read('META-INF/versions.list').decode().strip().splitlines()
        if len(versionRows) != 1:
            raise RuntimeError('Unexpected version manifest')
        expectedGameHash, version, _ = versionRows[0].split('\t')
        if version != info['version'] or hashlib.sha256((cache / 'game.jar').read_bytes()).hexdigest() != expectedGameHash:
            raise RuntimeError('Pinned inner JAR mismatch')
        libraryPaths = []
        for row in bundle.read('META-INF/libraries.list').decode().strip().splitlines():
            expectedHash, _, relative = row.split('\t')
            target = (cache / 'libraries' / relative).resolve()
            if not target.is_relative_to((cache / 'libraries').resolve()):
                raise RuntimeError('Invalid library path')
            content = bundle.read('META-INF/libraries/' + relative)
            if hashlib.sha256(content).hexdigest() != expectedHash:
                raise RuntimeError('Bundled library checksum mismatch')
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(content)
            libraryPaths.append(target)
    classDir = output / 'classes'
    classDir.mkdir()
    classPath = os.pathsep.join(map(str, [cache / 'game.jar', *libraryPaths]))
    javaVersion = subprocess.run(['java', '-version'], capture_output=True, text=True, check=True).stderr
    if not re.search(r'version "25[.\"]', javaVersion):
        raise RuntimeError('This audit requires JDK 25 on PATH')
    subprocess.run(['javac', '-cp', classPath, '-d', str(classDir), str(Path(__file__).with_name('CaptureRedstone.java'))], check=True)
    # This import creates the original GameTest pack only after validating inputs.
    import captureRedstone
    fixtures = args.fixtures or sorted((root / 'tests/fixtures').glob('*.json'))
    report = {'referenceVersion': info['version'], 'serverSha1': info['serverSha1'],
              'gameSha256': expectedGameHash, 'javaVersion': javaVersion,
              'comparison': 'baseline comparison includes origin; only nativeComparison establishes C++ agreement at the new origin', 'scenarios': []}
    for source in fixtures:
        baseline = json.loads(source.read_text())
        if not {'commands', 'watch', 'frames', 'endTick'} <= baseline.keys():
            continue
        scenarioDir = output / source.stem
        scenarioDir.mkdir()
        scenario = {key: value for key, value in baseline.items()
                    if key not in ('frames', 'origin', 'reference', 'referenceEnvironment', 'updateTrace', 'updateTraceTruncated')}
        scenarioPath = scenarioDir / 'scenario.json'
        scenarioPath.write_text(json.dumps(scenario))
        instancePath = captureRedstone.packDir / 'data/simulator/test_instance/capture.json'
        instance = json.loads(instancePath.read_text())
        instance['max_ticks'] = max(100, baseline['endTick'] + 20)
        instancePath.write_text(json.dumps(instance))
        actualPath = scenarioDir / 'actual.json'
        with (scenarioDir / 'java.log').open('w') as log:
            process = subprocess.run(['java', '-cp', str(classDir) + os.pathsep + classPath,
                                     'CaptureRedstone', str(scenarioPath), str(actualPath),
                                     str(scenarioDir / 'world'), str(captureRedstone.packDir.parent),
                                     str(scenarioDir / 'gameTestReport.xml')], cwd=cache, stdout=log, stderr=subprocess.STDOUT, timeout=180)
        result = {'fixture': source.name, 'baselineSha256': hashlib.sha256(source.read_bytes()).hexdigest(), 'exitCode': process.returncode}
        if process.returncode == 0 and actualPath.exists():
            actual = json.loads(actualPath.read_text())
            result.update(frames=len(actual['frames']), watch=len(actual['watch']),
                          baselineFirstDifference=firstDifference(baseline, actual))
            result['status'] = 'captured'
            if args.checker:
                checked = subprocess.run([str(args.checker.resolve()), str(actualPath)], capture_output=True, text=True, timeout=180)
                result['nativeComparison'] = json.loads(checked.stdout)
                result['status'] = 'match' if checked.returncode == 0 and result['nativeComparison']['status'] == 'match' else 'difference'
        else:
            result['status'] = 'captureFailed'
        report['scenarios'].append(result)
        (output / 'report.json').write_text(json.dumps(report, indent=2))
        print(source.name, result['status'], flush=True)
    if not report['scenarios'] or any(row['status'] not in ('match', 'captured') for row in report['scenarios']):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
