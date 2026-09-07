#!/usr/bin/env python3
"""Compare canonical replay streams without keeping their snapshots in memory."""
import argparse
import gzip
import hashlib
import itertools
import json
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('before', type=Path)
parser.add_argument('after', type=Path)
parser.add_argument('--before-commit', required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
if args.output.exists():
    parser.error('output already exists; choose another result file')

digest = hashlib.sha256()
scenarios = {}
size = 0
def openStream(path):
    return gzip.open(path, 'rb') if path.suffix == '.gz' else path.open('rb')

with openStream(args.before) as before, openStream(args.after) as after:
    for index, (old, new) in enumerate(itertools.zip_longest(before, after), 1):
        if old != new:
            oldRecord = json.loads(old) if old else {}
            newRecord = json.loads(new) if new else {}
            fields = [key for key in oldRecord.keys() | newRecord.keys() if oldRecord.get(key) != newRecord.get(key)]
            raise SystemExit(f"First difference: record {index}, {oldRecord.get('scenario', newRecord.get('scenario'))}, "
                             f"point {oldRecord.get('point', newRecord.get('point'))}, fields {fields}")
        digest.update(old)
        size += len(old)
        record = json.loads(old)
        summary = scenarios.setdefault(record['scenario'], {'records': 0})
        summary['records'] += 1
        summary['finalTick'] = record['checkpoint']['tick']
        summary['events'] = record['events']
        summary['neighborUpdates'] = record['updates']
        summary['traceEdges'] = len(record['checkpoint']['trace'])
if not scenarios:
    raise SystemExit('Empty replay streams are not verification evidence')
result = {
    'baselineCommit': args.before_commit,
    'identicalBytes': size,
    'sha256': digest.hexdigest(),
    'scenarios': scenarios,
    'notes': 'Byte-identical full checkpoints, queues, RNG state, inventories, probe edges, display changes and counters after each fixture input and tick. Includes negative-coordinate chains with 64 switching probes. This compares the two core builds; original-game correctness is checked separately by simulatorTests.'
}
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps(result, indent=2, ensure_ascii=False) + '\n')
print(f'{len(scenarios)} scenarios, {sum(row["records"] for row in scenarios.values())} records, {size} identical bytes')
print(args.output.resolve())
