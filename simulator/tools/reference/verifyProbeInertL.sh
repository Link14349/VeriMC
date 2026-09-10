set -eu
cd '/mnt/f/学习和研究/VeriMC-agents/pistonStall'
export PATH="$PWD/simulator/.cache/jdk25/bin:$PATH"
out="${1:?output dir}"
mkdir -p "$out"
cd simulator/tools/reference
python3 - "$out" <<'PY'
import json, sys
from pathlib import Path
import captureUpdateSource as module
from captureRedstone import runCapture

target = Path(sys.argv[1]) / 'updateSourceReRun.json'
runCapture(module.commands, module.watch, 14, capturePath=str(target),
           discardDrops=True, updateTraceLimit=200000)
fresh = json.loads(target.read_text())
committed = json.loads(Path('../../tests/fixtures/java26_2UpdateSource.json').read_text())
volatile = {'origin'}
for key in sorted(set(fresh) | set(committed)):
    if key in volatile:
        continue
    if key == 'referenceEnvironment':
        a = {k: v for k, v in fresh[key].items() if k not in ('gameTime', 'clockTicks')}
        b = {k: v for k, v in committed[key].items() if k not in ('gameTime', 'clockTicks')}
        print(key, 'equal' if a == b else 'DIFFERENT')
        continue
    print(key, 'equal' if fresh.get(key) == committed.get(key) else 'DIFFERENT')
PY
