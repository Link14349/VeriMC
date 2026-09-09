#!/usr/bin/env python3
"""Probe (not a fixture): how many block cells does one declared container entity occupy?

`ProbeEntityBoxN` computes the answer from the registered `EntityDimensions`. This probe measures
it on a running vanilla server through the very query the hopper uses: `CaptureRedstone`'s
`watchContainerEntities` observation calls `level.getEntities(null, AABB(c..c+1 on every axis),
CONTAINER_ENTITY_SELECTOR)` for each watched cell, which is `getEntityContainer`'s box verbatim.
One chest boat and one chest minecart are declared at a cell centre and a 5x3x5 neighbourhood of
each is watched; the reported cells are the entity's real reach.

Output goes to testResults, never to tests/fixtures.
"""
from pathlib import Path
from captureRedstone import state
from captureVanillaScenario import runVanillaCapture

ORIGIN = [64, -59, 64]
BOAT = (10, 2, 10)
CART = (20, 2, 10)
END = 4

commands = []
watch = []

for x, z in [(BOAT[0], BOAT[2]), (CART[0], CART[2])]:
    for dx in range(-2, 3):
        for dz in range(-2, 3):
            commands.append({'tick': 0, 'pos': [x + dx, 0, z + dz], 'stateId': state('stone')})

for centre, kind in [(BOAT, 'oak_chest_boat'), (CART, 'chest_minecart')]:
    commands.append({'tick': 0, 'pos': list(centre),
                     'stimulus': {'containerEntities': [{'type': kind, 'inventory': [
                         {'slot': 0, 'item': 'minecraft:stone', 'count': 1}]}]}})
    for dx in range(-2, 3):
        for dy in range(-2, 3):
            for dz in range(-2, 3):
                cell = [centre[0] + dx, centre[1] + dy, centre[2] + dz]
                if 0 <= cell[1] <= 5:
                    watch.append(cell)

outputDir = Path(__file__).resolve().parents[2] / 'testResults/boatBoxProbeN'

if __name__ == '__main__':
    outputDir.mkdir(parents=True, exist_ok=True)
    runVanillaCapture(commands, watch, END, 'probeBoatBox', ORIGIN,
                      capturePath=str(outputDir / 'probeBoatBox.json'), watchContainerEntities=True)
