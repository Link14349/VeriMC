set -eu
cd '/mnt/f/学习和研究/VeriMC-agents/pistonStall'
export PATH="$PWD/simulator/.cache/jdk25/bin:$PATH"
out="${1:?output dir}"
mkdir -p "$out"
cd simulator/tools/reference
python3 - "$out" <<'PY'
import json, sys
from pathlib import Path
import captureFullUpdateSnapshot as module
from captureVanillaScenario import runVanillaCapture

target = Path(sys.argv[1]) / 'fullUpdateSnapshotRepeat.json'
runVanillaCapture(module.commands, module.watch, 12, None, [64, -59, 64],
                  capturePath=str(target), discardDrops=True, updateTraceLimit=200000)
repeat = json.loads(target.read_text())
committed = json.loads(Path('../../tests/fixtures/java26_2FullUpdateSnapshot.json').read_text())
for key in ('frames', 'updateTrace', 'origin', 'updateTraceTruncated'):
    print(key, 'equal' if repeat.get(key) == committed.get(key) else 'DIFFERENT')
a = {k: v for k, v in repeat['referenceEnvironment'].items() if k not in ('gameTime', 'clockTicks')}
b = {k: v for k, v in committed['referenceEnvironment'].items() if k not in ('gameTime', 'clockTicks')}
print('referenceEnvironment', 'equal' if a == b else 'DIFFERENT')
PY
