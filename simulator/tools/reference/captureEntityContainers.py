#!/usr/bin/env python3
"""Hoppers talking to container minecarts, with the level's random source compared every tick.

`HopperBlockEntity.getContainerAt` looks for a block container first and only then for an entity
container. `getEntityContainer` collects every `Container && isAlive` entity whose bounding box
meets a 1x1x1 box centred on the cell centre and then picks one with
`level.getRandom().nextInt(list.size())` — **a draw happens even when there is a single candidate**,
which is why this scenario turns on `randomSeed` and compares the raw 48 bit state every frame.
This protocol currently covers the chest minecart (27 slots) and the hopper minecart (5 slots).
Vanilla also selects chest boats/rafts; these remain outside this fixture's coverage.

The carts are spawned at the cell centre with zero velocity and gravity switched off, the same
convention the dropped-item, pressure plate and tripwire contact inputs already use: this models
the block-side logic, not minecart motion. Their positions are an input and are never asserted;
the observation is `{type, inventory}` per cell, listed in the vanilla `level.getEntities` order,
because that is the list `nextInt` indexes into.

Timing. `tryMoveItems` sets a cooldown only when something actually moved, so a hopper that keeps
*failing* against a present cart runs again on the very next tick and draws again — for as long as
the cart stands there. Sub-scenario G holds exactly that steady state on both the push and the pull
side for seventeen ticks, with a cooldown window in the middle where the draws must stop.

Draw ticks are kept disjoint between sub-scenarios: B 1/9/17, D 3, A 5/13/21/29/37, F 6/14/22/30/38,
then G alone from tick 40 to the end. A and F are switched off with a redstone block at tick 39 so
that nothing else can draw inside G's window. Disjointness matters because the state comparison only
pins down the *number* of draws per tick; the one draw whose *value* is observable is F's
`nextInt(2)` cart choice, and letting a second device draw in the same tick would make that value
depend on the block entity order instead of on the device under test.
"""
from captureRedstone import runCapture, state

commands = []
watch = []

SEED = 20260909


def put(tick, pos, name, **props):
    commands.append({'tick': tick, 'pos': list(pos), 'stateId': state(name, **props)})


def floor(x, z, radius=1):
    for dx in range(-radius, radius + 1):
        for dz in range(-radius, radius + 1):
            put(0, (x + dx, 0, z + dz), 'stone')


def carts(tick, pos, entities):
    commands.append({'tick': tick, 'pos': list(pos), 'stimulus': {'containerEntities': entities}})


def cart(kind, *rows):
    return {'type': kind, 'inventory': [{'slot': slot, 'item': item, 'count': count} for slot, item, count in rows]}


def inventory(tick, pos, rows):
    commands.append({'tick': tick, 'pos': list(pos), 'stimulus': {'inventory': rows}})


def drops(tick, pos, items):
    commands.append({'tick': tick, 'pos': list(pos), 'stimulus': {'groundItems': items}})


# A. Pulling out of a chest minecart. The hopper faces down into the stone floor, so its eject
#    side never finds a container and never draws; the only draws are the source lookups.
#    Slot 0 keeps more items than the run can consume, so no tick ever fails against the cart.
#    Slot 26 stays untouched, which is the 27 slot flat order `getSlots` hands to the hopper.
floor(4, 4)
put(0, (4, 1, 4), 'hopper', facing='down')
carts(4, (4, 2, 4), [cart('chest_minecart', (0, 'minecraft:stone', 6), (26, 'minecraft:dirt', 1))])
watch += [[4, 1, 4], [4, 2, 4]]

# B. Pushing into a hopper minecart standing in the cell the hopper faces. Three items means
#    three ejections; afterwards the hopper is empty, `ejectItems` is not reached at all and the
#    draws stop on both sides. The cart's own `suckInItems` looks at the cell two above it, which
#    is left empty, so the minecart's unmodelled hopper behaviour never fires.
floor(11, 4)
put(0, (12, 0, 4), 'stone')
put(0, (11, 1, 4), 'hopper', facing='east')
inventory(0, (11, 1, 4), [{'slot': 0, 'item': 'minecraft:stone', 'count': 3}])
carts(0, (12, 1, 4), [cart('hopper_minecart')])
watch += [[11, 1, 4], [12, 1, 4]]

# C. A block container wins outright: `getBlockContainer` returns the chest, `getEntityContainer`
#    is never called and no draw happens even though a loaded minecart shares the cell.
floor(18, 4)
put(0, (18, 1, 4), 'hopper', facing='down')
put(0, (18, 2, 4), 'chest')
inventory(0, (18, 2, 4), [{'slot': 0, 'item': 'minecraft:stone', 'count': 3}])
carts(0, (18, 2, 4), [cart('chest_minecart', (0, 'minecraft:dirt', 2))])
watch += [[18, 1, 4], [18, 2, 4]]

# D. An entity container shadows dropped items. At tick 3 an empty chest minecart sits above the
#    hopper together with a dropped item inside the suck volume: vanilla draws, takes the cart
#    branch, finds nothing and returns without ever looking at the item. The cart is removed in
#    that same tick, and at tick 4 the very same item is picked up, which proves it was reachable
#    all along and only the cart was in the way. Withdrawing the cart uses the `CHANGED_DIMENSION`
#    removal reason instead of `discard()`: destroying a container minecart runs
#    `Containers.dropContents`, whose `dropItemStack` draws three `nextDouble()`s per slot before it
#    tests emptiness — 162 level-random draws for an empty chest minecart. That is capture
#    machinery, not modelled behaviour, and the first capture of this scenario tripped over it.
floor(25, 4)
put(0, (25, 1, 4), 'hopper', facing='down')
carts(2, (25, 2, 4), [cart('chest_minecart')])
drops(2, (25, 1, 4), [{'item': 'minecraft:dirt', 'count': 1, 'y': 1.05}])
carts(3, (25, 2, 4), [])
watch += [[25, 1, 4], [25, 2, 4]]

# F. Two chest minecarts in one cell, both stocked, spawned at the same point so vanilla's own
#    `Entity.push` is a no-op (it needs a horizontal separation of at least 0.01). `nextInt(2)`
#    then decides which one loses an item; distinct item types make the choice visible.
floor(32, 4)
put(0, (32, 1, 4), 'hopper', facing='down')
carts(5, (32, 2, 4), [cart('chest_minecart', (0, 'minecraft:stone', 8)),
                      cart('chest_minecart', (0, 'minecraft:dirt', 8))])
watch += [[32, 1, 4], [32, 2, 4]]

# G. The steady state: a hopper that can see a cart but can never move anything into or out of it.
#    Vanilla leaves the cooldown untouched on a failed `tryMoveItems`, so `pushItemsTick` walks
#    straight back into `getContainerAt` on the next tick and draws again, every tick, forever.
#    Both directions are held at once from tick 40 to the end of the run:
#      * pull side (39): an empty chest minecart above the hopper. Its first slot holds one item,
#        so tick 40 is a *successful* pull that sets the 8 gt cooldown — ticks 41..47 must draw
#        nothing at all, because `isOnCooldown` returns before `getEntityContainer` is reached.
#        From tick 48 the cart is empty and every tick draws again.
#      * push side (44): the hopper holds one item and faces a hopper minecart whose five slots
#        are all full stacks. `getAttachedContainer` draws first and `isFullContainer` refuses
#        afterwards, so every tick from 40 on draws exactly once and moves nothing.
#    Expected draw counts: tick 40 -> 2, ticks 41..47 -> 1, ticks 48..56 -> 2.
#    A and F are disabled one tick earlier so their next scheduled transfers (45 and 46) cannot
#    land inside this window; a powered hopper leaves `tryMoveItems` before any container lookup.
put(39, (3, 1, 4), 'redstone_block')
put(39, (31, 1, 4), 'redstone_block')

floor(39, 4)
put(0, (39, 1, 4), 'hopper', facing='down')
carts(39, (39, 2, 4), [cart('chest_minecart', (0, 'minecraft:stone', 1))])
watch += [[39, 1, 4], [39, 2, 4]]

floor(44, 4)
put(0, (44, 1, 4), 'hopper', facing='east')
inventory(39, (44, 1, 4), [{'slot': 0, 'item': 'minecraft:stone', 'count': 1}])
carts(39, (45, 1, 4), [cart('hopper_minecart', *((slot, 'minecraft:stone', 64) for slot in range(5)))])
watch += [[44, 1, 4], [45, 1, 4]]

if __name__ == '__main__':
    runCapture(commands, watch, 56, 'java26_2EntityContainers',
               randomSeed=SEED, watchContainerEntities=True, watchGroundItems=True)
