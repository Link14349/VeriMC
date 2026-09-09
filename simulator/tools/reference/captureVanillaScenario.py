#!/usr/bin/env python3
"""Produce a fixture with the strict vanilla-only server at a **chosen** origin.

`captureRedstone.runCapture` goes through GameTest, which picks its own random origin. Some
scenarios need a known origin — chunk lifecycle tests have to know where the chunk borders fall
relative to their own coordinates. `CaptureRedstoneVanilla` takes the origin as an argument, so
this driver is used instead. The world, region preparation and timeline are the same code path;
only the origin and the enabled feature set differ, and the feature set is the stricter one.
"""
import json
from pathlib import Path
import subprocess

rootDir = Path(__file__).resolve().parents[2]
cacheDir = rootDir / '.cache/reference'


def runVanillaCapture(commands, watch, endTick, fixtureName, origin, capturePath=None, **scenarioOptions):
    scenario = {'endTick': endTick, 'commands': commands, 'watch': watch, **scenarioOptions}
    scenarioPath = cacheDir / 'vanillaScenario.json'
    scenarioPath.write_text(json.dumps(scenario, indent=2), encoding='utf-8')
    # Fuzzing writes outside the checked-in fixture directory.
    outputPath = Path(capturePath) if capturePath else rootDir / ('tests/fixtures/' + fixtureName + '.json')
    outputPath.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(['python3', str(Path(__file__).with_name('runReferenceTool.py')), 'CaptureRedstoneVanilla',
                    str(scenarioPath), str(outputPath), str(cacheDir / 'vanillaUniverse'),
                    str(origin[0]), str(origin[1]), str(origin[2])], check=True)
    return outputPath
