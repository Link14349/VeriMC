#!/usr/bin/env python3
"""Repeat the release workload and retain raw observations and build identity."""
import argparse
import hashlib
import json
import math
import platform
import statistics
import subprocess
from datetime import datetime, timezone
from pathlib import Path

rootDir = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--circuits', type=int, default=10000)
parser.add_argument('--runs', type=int, default=5)
parser.add_argument('--warmups', type=int, default=1)
parser.add_argument('--probes', type=int, default=0)
parser.add_argument('--baseline', type=Path, help='Optional earlier binary; alternate before/after runs to reduce time drift')
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
if not 1 <= args.circuits <= 100000 or not 1 <= args.runs <= 100 or not 0 <= args.warmups <= 10:
    parser.error('circuits must be 1–100000, runs 1–100, warmups 0–10')
if not 0 <= args.probes <= min(64, args.circuits):
    parser.error('probes must be 0–64 and not exceed the circuit count')
if args.output.exists():
    parser.error('output already exists; choose a new result file')
binary = rootDir / 'build/simulatorCli'
if not binary.is_file():
    parser.error('build the release simulatorCli first')
if args.baseline and not args.baseline.is_file():
    parser.error('baseline binary does not exist')
cacheFile = rootDir / 'build/CMakeCache.txt'
if not cacheFile.is_file() or 'CMAKE_BUILD_TYPE:STRING=Release' not in cacheFile.read_text():
    parser.error('build/ must be configured as Release')

def git(*arguments):
    return subprocess.check_output(['git', '-C', str(rootDir), *arguments], text=True).strip()

sourceFiles = [rootDir / 'CMakeLists.txt', *sorted((rootDir / 'src').glob('*.cpp')),
               *sorted((rootDir / 'include/simulator').glob('*.hpp')), rootDir / 'apps/cli/main.cpp']
flagsFile = rootDir / 'build/CMakeFiles/simulatorCore.dir/flags.make'
result = {
    'dateUtc': datetime.now(timezone.utc).isoformat(),
    'platform': platform.platform(),
    'machine': platform.machine(),
    'sourceCommit': git('rev-parse', 'HEAD'),
    'sourceDirty': bool(git('status', '--porcelain', '--', str(rootDir))),
    'binarySha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
    'sourceFilesSha256': {str(path.relative_to(rootDir)): hashlib.sha256(path.read_bytes()).hexdigest() for path in sourceFiles},
    'build': 'Release (build/simulatorCli)',
    'coreCompileFlags': flagsFile.read_text() if flagsFile.exists() else None,
    'command': ['simulatorCli', '--benchmark', str(args.circuits), '--probes', str(args.probes)],
    'samples': [],
    'warmups': [],
    'percentileMethod': 'nearest rank: sorted[ceil(0.95 * n) - 1]; with five runs p95 is the maximum',
    'notes': 'Timed work includes 40 input transitions per circuit, execution and change collection; excludes construction. No browser rendering. No idle-only TPS claim.'
}
variants = [('current', binary, result)]
if args.baseline:
    result['baseline'] = {'binarySha256': hashlib.sha256(args.baseline.read_bytes()).hexdigest(), 'samples': [], 'warmups': []}
    result['runOrder'] = 'Each pair alternates baseline/current and current/baseline; both have separate warmups.'
    variants.insert(0, ('baseline', args.baseline.resolve(), result['baseline']))
expectedWork = None
expectedCounters = {}
for phase, count in [('warmups', args.warmups), ('samples', args.runs)]:
    for index in range(count):
        for name, executable, observations in (variants if index % 2 == 0 else variants[::-1]):
            sample = json.loads(subprocess.check_output([str(executable), *result['command'][1:]], text=True))
            work = {key: sample[key] for key in ['benchmark', 'version', 'blocks', 'circuits', 'ticks', 'events', 'neighborUpdates']}
            counters = {key: sample.get(key) for key in ['probes', 'traceEdges', 'stateChanges']}
            if sample.get('probes', 0) != args.probes:
                raise RuntimeError(f'{name} did not honor the requested probe count')
            if (expectedWork is not None and work != expectedWork) or (name in expectedCounters and counters != expectedCounters[name]):
                raise RuntimeError('workload counters changed between runs; refusing a misleading timing result')
            expectedWork = work
            expectedCounters[name] = counters
            observations[phase].append(sample)
            print(f"{name} {phase} {index + 1}: {sample['seconds']:.6f} s", flush=True)
for _, _, observations in variants:
    observations['medianSeconds'] = statistics.median(sample['seconds'] for sample in observations['samples'])
    observations['p95Seconds'] = sorted(sample['seconds'] for sample in observations['samples'])[math.ceil(0.95 * args.runs) - 1]
    observations['medianGameTicksPerSecond'] = expectedWork['ticks'] / observations['medianSeconds']
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
print(args.output.resolve())
