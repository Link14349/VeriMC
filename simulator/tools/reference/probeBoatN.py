#!/usr/bin/env python3
"""Probe driver: run captureBoatContainers' timeline into a scratch path instead of the fixture.

Kept out of the fixture directory so an exploratory run never overwrites checked-in evidence.
"""
from pathlib import Path
import captureBoatContainers as scenario
from captureVanillaScenario import runVanillaCapture

outputDir = Path(__file__).resolve().parents[2] / 'testResults/claudeIssueClosure/layered-20260909-183541-agentN'

if __name__ == '__main__':
    outputDir.mkdir(parents=True, exist_ok=True)
    runVanillaCapture(scenario.commands, scenario.watch, scenario.END, 'probeBoatContainers',
                      scenario.ORIGIN, capturePath=str(outputDir / 'probeBoatContainers.json'),
                      randomSeed=scenario.SEED, watchContainerEntities=True)
