#!/usr/bin/env python3
"""Random small circuits plus random input histories, differentially checked against vanilla.

Many independent circuits are packed into one GameTest capture so a run costs one server
start. On a difference the first differing position identifies the offending cell, and the
scenario is shrunk automatically: first to that cell alone, then by greedily dropping
commands while the difference survives. This is budgeted reduction, not a minimality proof.

Every step re-captures from the pinned reference; nothing is inferred. Coordinates, seed
and the surviving command history are written to the report so a failure is reproducible.

By default every capture of a run — the round, the cell isolation, each reduction step and the
final confirmation — uses **one fixed origin**, drawn once from the seed, through the strict
vanilla-only server (`CaptureRedstoneVanilla`). The reduced witness is therefore confirmed at the
same absolute coordinates as the original failure. `--gameTest` restores the old path, which goes
through GameTest and lets it draw a new origin for every capture; a witness reduced that way is
only confirmed at its own recorded origin.

    python3 tools/reference/fuzzRedstone.py --seed 1 --rounds 3 --output <new directory>
"""
import argparse
import hashlib
import json
import random
import subprocess
import sys
from pathlib import Path

from captureRedstone import runCapture, blocks, state
from captureVanillaScenario import runVanillaCapture

rootDir = Path(__file__).resolve().parents[2]

CELL = 8
GRID = 5
ORIGIN = 3

# Only blocks the kernel claims to support; anything else is refused on purpose and would
# make the generator test the refusal instead of the mechanics.
STRUCTURE = ['stone', 'glass', 'oak_planks', 'white_wool', 'stone_slab', 'oak_stairs', 'slime_block', 'honey_block']
DEVICES = ['redstone_wire', 'repeater', 'comparator', 'redstone_torch', 'lever', 'redstone_lamp',
           'copper_bulb', 'observer', 'piston', 'sticky_piston', 'redstone_block', 'rail',
           'powered_rail', 'target', 'note_block', 'oak_trapdoor', 'oak_fence_gate', 'tripwire_hook']
INTERACTIVE = ['lever', 'note_block', 'redstone_wire', 'oak_trapdoor', 'oak_fence_gate', 'comparator', 'repeater']
# Vanilla's useWithoutItem is reached reflectively for exactly these classes in the capture
# harness; blocks outside the list are only driven by setBlock commands.
FACINGS = ['north', 'east', 'south', 'west']


def randomState(rng, name):
    """A random valid state for this block, drawn from the exported registry."""
    block = blocks[name]
    options = {}
    for prop, values in propertyValues(block).items():
        if prop in ('waterlogged', 'lit', 'powered', 'extended', 'triggered', 'locked', 'has_record'):
            continue  # runtime-owned properties stay at their default
        options[prop] = rng.choice(values)
    return state(name, **options)


def propertyValues(block):
    values = {}
    for row in block['states']:
        for key, value in row['properties'].items():
            values.setdefault(key, [])
            if value not in values[key]:
                values[key].append(value)
    return values


def buildCell(rng, cellX, cellZ, endTick):
    """One random circuit inside its own cell, plus its own command history."""
    baseX = ORIGIN + cellX * CELL
    baseZ = ORIGIN + cellZ * CELL
    commands, watch, occupied = [], [], {}
    for dx in range(CELL - 2):
        for dz in range(CELL - 2):
            commands.append({'tick': 0, 'pos': [baseX + dx, 1, baseZ + dz], 'stateId': state('stone')})
    for _ in range(rng.randint(4, 9)):
        dx, dz = rng.randrange(CELL - 2), rng.randrange(CELL - 2)
        y = rng.choice([2, 2, 2, 3])
        pos = (baseX + dx, y, baseZ + dz)
        if pos in occupied:
            continue
        name = rng.choice(DEVICES if rng.random() < 0.75 else STRUCTURE)
        try:
            stateId = randomState(rng, name)
        except StopIteration:
            continue
        occupied[pos] = name
        commands.append({'tick': 0, 'pos': list(pos), 'stateId': stateId})
        watch.append(list(pos))
    # Interact targets keep the block they were placed with, so the reference harness always
    # sees an interactable block; every other history entry works on untouched positions.
    interactable = [pos for pos, name in occupied.items() if name in INTERACTIVE]
    rng.shuffle(interactable)
    reserved = set(interactable[:rng.randint(0, min(2, len(interactable)))])
    for pos in reserved:
        for tick in sorted(rng.sample(range(1, max(2, endTick - 1)), rng.randint(1, 2))):
            # Fence gates read the player's look direction, so it is always explicit.
            commands.append({'tick': tick, 'pos': list(pos), 'interact': True, 'playerFacing': rng.choice(FACINGS)})
    editable = [pos for pos in occupied if pos not in reserved]
    for _ in range(rng.randint(2, 5)):
        tick = rng.randint(1, max(1, endTick - 2))
        if editable and rng.random() < 0.5:
            pos = rng.choice(editable)
            commands.append({'tick': tick, 'pos': list(pos), 'stateId': 0})
        else:
            dx, dz = rng.randrange(CELL - 2), rng.randrange(CELL - 2)
            pos = (baseX + dx, rng.choice([2, 3]), baseZ + dz)
            if pos in reserved:
                continue
            name = rng.choice(DEVICES)
            commands.append({'tick': tick, 'pos': list(pos), 'stateId': randomState(rng, name)})
            if list(pos) not in watch:
                watch.append(list(pos))
    commands.sort(key=lambda row: row['tick'])
    return commands, watch


def cellOf(position):
    x, _, z = position
    return ((x - ORIGIN) // CELL, (z - ORIGIN) // CELL)


def capture(commands, watch, endTick, path, origin=None):
    if origin is None:
        runCapture(commands, watch, endTick, capturePath=str(path), lenientInteract=True)
    else:
        runVanillaCapture(commands, watch, endTick, None, origin, capturePath=str(path), lenientInteract=True)
    return json.loads(Path(path).read_text())


def check(checker, path):
    finished = subprocess.run([str(checker), str(path)], capture_output=True, text=True, timeout=300)
    report = json.loads(finished.stdout)
    status = report.get('status')
    if status not in ('match', 'difference', 'error'):
        raise RuntimeError(f'Unknown checker status: {status}')
    expectedCode = 0 if status == 'match' else 1
    if finished.returncode != expectedCode:
        raise RuntimeError(f'Checker status {status} contradicts exit code {finished.returncode}')
    return report


def differenceKey(report):
    difference = report.get('firstDifference')
    if difference:
        return ('state', tuple(difference['relativePos']), difference['tick'], difference['field'],
                json.dumps(difference.get('expected'), sort_keys=True),
                json.dumps(difference.get('actual'), sort_keys=True))
    return None


def captureEvidence(path):
    content = path.read_bytes()
    capture = json.loads(content)
    return {'captureSha256': hashlib.sha256(content).hexdigest(),
            'origin': capture.get('origin'), 'referenceEnvironment': capture.get('referenceEnvironment')}


def shrink(checker, commands, watch, endTick, key, workDir, budget, origin):
    """Budgeted command reduction. With a fixed origin every step keeps the original coordinates."""
    step = 0
    changed = True
    while changed and step < budget:
        changed = False
        for index in range(len(commands) - 1, -1, -1):
            if step >= budget:
                break
            candidate = commands[:index] + commands[index + 1:]
            step += 1
            path = workDir / f'shrink{step}.json'
            try:
                capture(candidate, watch, endTick, path, origin)
            except subprocess.CalledProcessError:
                continue
            if differenceKey(check(checker, path)) == key:
                commands = candidate
                changed = True
    return commands, step, step >= budget


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seed', type=int, default=1)
    parser.add_argument('--rounds', type=int, default=1)
    parser.add_argument('--ticks', type=int, default=12)
    parser.add_argument('--checker', type=Path, default=rootDir / 'buildAudit/checkReference')
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--shrinkBudget', type=int, default=60)
    parser.add_argument('--gameTest', action='store_true',
                        help='capture through GameTest, which draws a new origin for every capture')
    args = parser.parse_args()
    if args.rounds < 1 or args.ticks < 4 or args.shrinkBudget < 0:
        parser.error('rounds must be positive, ticks >= 4, and shrinkBudget >= 0')
    args.output.mkdir(parents=True, exist_ok=False)
    # One origin for the whole run, drawn from the seed, so a reduced witness keeps the exact
    # coordinates of the original failure. Chunk aligned and far from the other test worlds.
    originRng = random.Random(args.seed)
    origin = None if args.gameTest else [16 * originRng.randrange(-800000, 800000), -59,
                                         16 * originRng.randrange(-800000, 800000)]
    report = {'seed': args.seed, 'rounds': args.rounds, 'ticks': args.ticks, 'origin': origin,
              'reductionScope': ('budgeted greedy command removal; no minimality proof. '
                                 + ('every capture uses the fixed origin above, so the reduced witness is confirmed '
                                    'at the original coordinates' if origin else
                                    'GameTest draws a new origin per capture, so a reduced witness is only confirmed '
                                    'at its own recorded origin')),
              'results': []}
    failures = 0
    for round_ in range(args.rounds):
        rng = random.Random((args.seed << 16) + round_)
        commands, watch = [], []
        cells = {}
        for cellX in range(GRID):
            for cellZ in range(GRID):
                cellCommands, cellWatch = buildCell(rng, cellX, cellZ, args.ticks)
                cells[(cellX, cellZ)] = (cellCommands, cellWatch)
                commands += cellCommands
                watch += cellWatch
        path = args.output / f'round{round_}.json'
        capture(commands, watch, args.ticks, path, origin)
        result = check(args.checker, path)
        row = {'round': round_, 'cells': GRID * GRID, 'commands': len(commands), 'watch': len(watch),
               'status': result['status'], 'origin': result.get('origin')}
        row.update(captureEvidence(path))
        key = differenceKey(result)
        if key:
            failures += 1
            cell = cellOf(result['firstDifference']['relativePos'])
            row['firstDifference'] = result['firstDifference']
            row['cell'] = list(cell)
            cellCommands, cellWatch = cells.get(cell, (commands, watch))
            reduced = args.output / f'round{round_}Cell.json'
            capture(cellCommands, cellWatch, args.ticks, reduced, origin)
            cellKey = differenceKey(check(args.checker, reduced))
            if cellKey == key:
                reducedDir = args.output / f'round{round_}Reduction'
                reducedDir.mkdir()
                shrunk, steps, budgetReached = shrink(args.checker, cellCommands, cellWatch, args.ticks, key, reducedDir, args.shrinkBudget, origin)
                candidate = args.output / f'round{round_}Reduced.json'
                capture(shrunk, cellWatch, args.ticks, candidate, origin)
                confirmation = check(args.checker, candidate)
                row['reducedCommands'] = len(shrunk)
                row['shrinkSteps'] = steps
                row['shrinkBudgetReached'] = budgetReached
                row['reducedScenario'] = str(candidate)
                row['reducedEvidence'] = captureEvidence(candidate)
                row['reducedComparison'] = confirmation
                row['reducedWitnessConfirmed'] = differenceKey(confirmation) == key
                # 固定原点时，缩减候选与原始失败在**同一绝对坐标**上确认。
                row['reducedAtOriginalCoordinates'] = origin is not None and row['reducedEvidence']['origin'] == row['origin']
            else:
                row['note'] = 'difference does not survive isolation to a single cell'
        elif result['status'] != 'match':
            failures += 1
            row['error'] = result.get('error')
        report['results'].append(row)
        (args.output / 'report.json').write_text(json.dumps(report, indent=2, ensure_ascii=False))
        print(round_, result['status'], flush=True)
    (args.output / 'report.json').write_text(json.dumps(report, indent=2, ensure_ascii=False))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
