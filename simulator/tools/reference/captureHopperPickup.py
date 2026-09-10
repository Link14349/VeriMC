#!/usr/bin/env python3
"""Hoppers picking up item entities: suck range, blocking, cooldown and partial absorption.

`HopperBlockEntity.suckInItems` only reaches its item-entity branch when there is no container
above. The suck volume is `Hopper.SUCK_AABB = Block.column(16, 11, 32)` moved onto the block, i.e.
`x in [px, px+1]`, `y in [py+0.6875, py+2]`, `z in [pz, pz+1]`, intersected with the item's
0.25x0.25 bounding box (`AABB.intersects` is strict, so touching does not count).

`addItem(Container, ItemEntity)` reports a change **only when the whole stack fits**. A partial
absorption still moves items but returns false, so `tryMoveItems` does not set the 8 gt cooldown
for it — that quirk is asserted here on purpose.

The dropped items are spawned with zero velocity and no gravity, the same convention the pressure
plate and tripwire contact inputs already use: this models the block-side logic, not item motion.
Each group uses distinct item types so vanilla's `mergeWithNeighbours` never merges two of them.
"""
from captureRedstone import runCapture, state

commands = []
watch = []


def put(tick, pos, name, **props):
    commands.append({'tick': tick, 'pos': list(pos), 'stateId': state(name, **props)})


def clear(tick, pos):
    commands.append({'tick': tick, 'pos': list(pos), 'stateId': 0})


def drops(tick, pos, items):
    commands.append({'tick': tick, 'pos': list(pos), 'stimulus': {'groundItems': items}})


def inventory(tick, pos, rows):
    commands.append({'tick': tick, 'pos': list(pos), 'stimulus': {'inventory': rows}})


def base(x, z, facing='down', above=None):
    """A hopper with a floor under it; `above` optionally fills the block on top."""
    for dx in range(-1, 2):
        for dz in range(-1, 2):
            put(0, (x + dx, 0, z + dz), 'stone')
    put(0, (x, 1, z), 'hopper', facing=facing)
    if above:
        put(0, (x, 2, z), above)
    watch.append([x, 1, z])


# 1. The vertical extent of the suck volume, from the hopper's own hollow up to two blocks.
#    Positions sit clear of both ends on purpose: vanilla runs `Entity.move` every tick and
#    reconstructs the position from the bounding box, which drifts by about 1e-15, so exact
#    boundary behaviour is deliberately not asserted. The volume's lower end (11/16) is exactly
#    where the hopper's hollow starts, so anything below it is inside the hopper's collision shape;
#    vanilla then applies `moveTowardsClosestSpace` and lifts the item into range. That is item
#    motion, which this protocol does not model, so no case is placed there.
base(4, 4)
drops(2, (4, 1, 4), [
    {'item': 'minecraft:stone', 'count': 1, 'y': 2.05},            # above the volume
    {'item': 'minecraft:dirt', 'count': 1, 'y': 0.70},             # in the hopper's hollow
    {'item': 'minecraft:gravel', 'count': 1, 'y': 1.99},           # just inside the top
    {'item': 'minecraft:sand', 'count': 1, 'y': 2.30},             # well above the volume
])

# 2. The horizontal extent, same 0.25 wide box on x and z.
base(9, 4)
drops(2, (9, 1, 4), [
    {'item': 'minecraft:stone', 'count': 1, 'x': -0.2, 'y': 1.0},     # -0.325 .. -0.075, outside
    {'item': 'minecraft:dirt', 'count': 1, 'x': -0.1, 'y': 1.0},      # -0.225 .. 0.025, just inside
    {'item': 'minecraft:gravel', 'count': 1, 'z': 1.2, 'y': 1.0},     # 1.075 .. 1.325, outside
    {'item': 'minecraft:sand', 'count': 1, 'z': 1.1, 'y': 1.0},       # 0.975 .. 1.225, just inside
])

# 3. A full collision block above blocks the pickup; a bee nest is in DOES_NOT_BLOCK_HOPPERS.
#    These items sit in the hopper's own hollow (0.6875 .. 1.0) so that a solid block above
#    never overlaps them: an item inside a collision shape would be moved by vanilla instead.
base(14, 4, above='stone')
drops(2, (14, 1, 4), [{'item': 'minecraft:stone', 'count': 1, 'y': 0.72}])
base(19, 4, above='bee_nest')
drops(2, (19, 1, 4), [{'item': 'minecraft:dirt', 'count': 1, 'y': 0.72}])
# A non-full block does not block either.
base(24, 4, above='oak_slab')
drops(2, (24, 1, 4), [{'item': 'minecraft:gravel', 'count': 1, 'y': 0.72}])

# 4. Cooldown: one entity per tick is taken and the hopper then waits 8 gt.
base(29, 4)
drops(2, (29, 1, 4), [
    {'item': 'minecraft:stone', 'count': 1, 'y': 1.0},
    {'item': 'minecraft:dirt', 'count': 1, 'y': 1.2},
    {'item': 'minecraft:gravel', 'count': 1, 'y': 1.4},
])

# 5. Partial absorption. The hopper is filled so only part of the incoming stack fits;
#    vanilla writes the remainder back on the entity and reports no change.
base(34, 4)
inventory(0, (34, 1, 4), [{'slot': slot, 'item': 'minecraft:stone', 'count': 64} for slot in range(4)]
          + [{'slot': 4, 'item': 'minecraft:stone', 'count': 60}])
drops(2, (34, 1, 4), [{'item': 'minecraft:stone', 'count': 20, 'y': 1.0}])

# 6. A container above wins: the entity branch never runs even when the chest is empty.
base(39, 4, above='chest')
drops(2, (39, 1, 4), [{'item': 'minecraft:stone', 'count': 1, 'y': 0.72}])
watch.append([39, 2, 4])

# 7. A powered hopper is disabled and takes nothing; unpowering lets it resume.
base(44, 4)
put(0, (44, 2, 4), 'redstone_block')
drops(2, (44, 1, 4), [{'item': 'minecraft:stone', 'count': 1, 'y': 0.72}])
clear(10, (44, 2, 4))

# 8. Replacing the declared set mid-run removes the old entities and adds new ones.
base(4, 10)
drops(2, (4, 1, 10), [{'item': 'minecraft:stone', 'count': 1, 'y': 1.0}])
drops(3, (4, 1, 10), [{'item': 'minecraft:dirt', 'count': 2, 'y': 1.0}])

if __name__ == '__main__':
    runCapture(commands, watch, 30, 'java26_2HopperPickup', watchGroundItems=True)
