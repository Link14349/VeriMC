#!/usr/bin/env python3
"""Repeat the release workload and retain raw observations and build identity."""
import argparse
import hashlib
import json
import platform
import statistics
import subprocess
from datetime import datetime, timezone
from pathlib import Path

rootDir = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--circuits', type=int, default=10000)
parser.add_argument('--runs', type=int, default=3)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
if not 1 <= args.circuits <= 100000 or not 1 <= args.runs <= 100:
    parser.error('circuits must be 1–100000 and runs must be 1–100')
if args.output.exists():
    parser.error('output already exists; choose a new result file')
binary = rootDir / 'build/simulatorCli'
if not binary.is_file():
    parser.error('build the release simulatorCli first')

def git(*arguments):
    return subprocess.check_output(['git', '-C', str(rootDir), *arguments], text=True).strip()

result = {
    'dateUtc': datetime.now(timezone.utc).isoformat(),
    'platform': platform.platform(),
    'machine': platform.machine(),
    'sourceCommit': git('rev-parse', 'HEAD'),
    'sourceDirty': bool(git('status', '--porcelain', '--', str(rootDir))),
    'binarySha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
    'build': 'Release (build/simulatorCli)',
    'command': ['simulatorCli', '--benchmark', str(args.circuits)],
    'samples': [],
    'notes': 'Timed work includes 40 input transitions per circuit, execution and change collection; excludes construction. No browser rendering. No idle-only TPS claim.'
}
for index in range(args.runs):
    sample = json.loads(subprocess.check_output([str(binary), '--benchmark', str(args.circuits)], text=True))
    result['samples'].append(sample)
    print(f"Run {index + 1}: {sample['seconds']:.6f} s", flush=True)
result['medianSeconds'] = statistics.median(sample['seconds'] for sample in result['samples'])
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
print(args.output.resolve())
