#!/usr/bin/env python3
"""Intra-tick trace coverage for the two update kinds the coordinate-only trace could not tell apart.

26.2 queues neighbour notifications in two shapes. `SimpleNeighborUpdate` reads the target's block
state when it finally runs; `FullNeighborUpdate` carries a state snapshot taken when it was queued,
so a later replacement of the target does not change what `handleNeighborChanged` sees. The old
trace recorded only coordinates, so the two were indistinguishable and the kernel modelled every
notification as the simple form.

This scenario drives the reachable full-update sources:

- `BlockEntity.setChanged` -> `Level.updateNeighbourForOutputSignal`, both directly adjacent and
  through a redstone conductor, for a chest, a hopper and a decorated pot;
- `BaseRailBlock.updateState`, which only issues the full form for the straight rails
  (`powered_rail`, `detector_rail`, `activator_rail`); plain `rail` is the control that must not;
- `DetectorRailBlock`, which notifies its connected rail when a cart arrives.

The trace is on, so the update kind, the source block and the snapshot state are all compared.
"""
from captureRedstone import runCapture, state

commands = []
watch = []


def put(tick, pos, name, **props):
    commands.append({'tick': tick, 'pos': list(pos), 'stateId': state(name, **props)})


def clear(tick, pos):
    commands.append({'tick': tick, 'pos': list(pos), 'stateId': 0})


def stimulate(tick, pos, stimulus):
    commands.append({'tick': tick, 'pos': list(pos), 'stimulus': stimulus})


def floor(x, z, radius=3):
    for dx in range(-radius, radius + 1):
        for dz in range(-radius, radius + 1):
            put(0, (x + dx, 0, z + dz), 'stone')


def container(x, z, name, gap, **props):
    """A container whose comparator sits either next to it or one conductor further away."""
    global watch
    floor(x, z, 4)
    put(0, (x, 1, z), name, **props)
    comparator = (x + 1 + gap, 1, z)
    if gap:
        put(0, (x + 1, 1, z), 'stone')
    put(0, comparator, 'comparator', facing='east')
    watch += [[x, 1, z], list(comparator)]
    stimulate(2, (x, 1, z), {'inventory': [{'slot': 0, 'item': 'minecraft:stone', 'count': 1}]})
    stimulate(6, (x, 1, z), {'inventory': [{'slot': 0, 'item': 'minecraft:stone', 'count': 0}]})


container(5, 5, 'chest', 0, facing='north')
container(13, 5, 'chest', 1, facing='north')
container(21, 5, 'hopper', 0, facing='down')
container(29, 5, 'decorated_pot', 0, facing='north')


def rail(x, z, name, control=False):
    """Placing a straight rail issues a full neighbour update on itself; plain rail must not."""
    global watch
    floor(x, z)
    put(2, (x, 1, z), name, shape='east_west')
    # A neighbouring rail so the shape logic actually has something to connect to.
    put(0, (x + 1, 1, z), 'rail', shape='east_west')
    watch += [[x, 1, z], [x + 1, 1, z]]
    clear(8, (x, 1, z))
    if control:
        # The curvable rail takes the same updateState path but skips the full notification.
        put(10, (x, 1, z), 'rail', shape='east_west')


rail(5, 13, 'powered_rail')
rail(11, 13, 'detector_rail')
rail(17, 13, 'activator_rail')
rail(23, 13, 'rail', control=True)

# A cart arriving on a detector rail makes it notify its connected rail with a state snapshot.
floor(29, 13)
put(0, (29, 1, 13), 'detector_rail', shape='east_west')
put(0, (30, 1, 13), 'rail', shape='east_west')
put(0, (28, 1, 13), 'rail', shape='east_west')
watch += [[29, 1, 13], [30, 1, 13], [28, 1, 13]]
stimulate(3, (29, 1, 13), {'carts': [{'type': 'minecart'}]})
stimulate(9, (29, 1, 13), {'carts': []})

if __name__ == '__main__':
    runCapture(commands, watch, 14, 'java26_2UpdateSource', discardDrops=True, updateTraceLimit=200000)
