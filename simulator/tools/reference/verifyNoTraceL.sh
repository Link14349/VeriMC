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

target = Path(sys.argv[1]) / 'fullUpdateSnapshotUntraced.json'
runVanillaCapture(module.commands, module.watch, 12, None, [64, -59, 64],
                  capturePath=str(target), discardDrops=True)
untraced = json.loads(target.read_text())
traced = json.loads(Path('../../tests/fixtures/java26_2FullUpdateSnapshot.json').read_text())
print('frames equal:', untraced['frames'] == traced['frames'])
print('origin equal:', untraced['origin'] == traced['origin'])
PY
