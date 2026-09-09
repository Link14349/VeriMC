#!/usr/bin/env python3
"""Hoppers talking to **chest boats and chest rafts**, with the level random compared every tick.

`HopperBlockEntity.getEntityContainer` collects every entity that satisfies
`EntitySelector.CONTAINER_ENTITY_SELECTOR` (`entity instanceof Container && entity.isAlive()`) in a
1x1x1 box centred on the cell centre and picks one with `level.getRandom().nextInt(list.size())` —
**a draw happens even when there is a single candidate**. In 26.2 exactly two class hierarchies
satisfy that selector: `AbstractMinecartContainer` (`MinecartChest` 27 slots, `MinecartHopper` 5)
and `AbstractChestBoat` (`ChestBoat` and `ChestRaft`, both 27 slots, `getContainerSize` on
`AbstractChestBoat`). Twelve registered ids in total: nine `*_chest_boat`, `bamboo_chest_raft`,
`chest_minecart`, `hopper_minecart`.

`java26_2EntityContainers` covers the minecart side only. This fixture is the boat side:
  A  a single chest boat is the only candidate — `nextInt(1)` is still drawn;
  B  a chest raft whose 27 slots are all full stacks blocks a push and the hopper idles,
     redrawing **every tick** because a failed `tryMoveItems` never sets the cooldown;
  C  a chest minecart and a chest boat sharing one cell, minecart declared first;
  D  the same pair with the boat declared first — the reported candidate order is the spawn order;
  E  pushing into an empty chest raft, which succeeds and does set the 8 gt cooldown.

Wood choice. All nine `*_chest_boat` ids share one Java class (`ChestBoat`); they differ only in
`EntityType` registration and in the item they drop when destroyed, neither of which the hopper
path reads, so `oak_chest_boat` stands for all nine. `bamboo_chest_raft` is the *only* registered
`ChestRaft`, so it has to appear on its own. `AbstractChestBoat.setChanged()` is empty and the class
is not a `WorldlyContainer`, so `getSlots` hands the hopper the flat 0..26 order — B's full raft and
E's empty raft both exercise that order.

Conventions carried over from `captureEntityContainers.py`: the entities are spawned at the **cell
centre** with zero velocity and gravity switched off; their position is an input and is never
asserted. Withdrawing them uses `CHANGED_DIMENSION`, not `discard()`: `AbstractChestBoat.remove`
runs `Containers.dropContents` for any reason with `shouldDestroy()`, and `dropItemStack` draws
three `nextDouble()`s per slot **before** it tests emptiness, so discarding one empty chest boat
would burn 27x6 = 162 level-random draws — capture machinery, not modelled behaviour.

Geometry note (important, and the reason the push cases watch only the boat's cell).
A chest boat's bounding box is 1.375 x 0.5625 x 1.375 and `setPos` puts its **feet** at the cell
centre, so a boat declared in cell C really overlaps the 3x2x3 block of cells around it
(`x-1..x+1`, `y..y+1`, `z-1..z+1`). Every cell of that block reports the boat to
`getEntityContainer`. That is a property of the real entity, not of this protocol, and the kernel
models a container entity as belonging to exactly one declared cell. To keep this fixture about the
container rules rather than about box extents, every watched cell here is either the boat's own
cell or far enough away that the box cannot reach it:
  * pull cases put the boat one cell **above** the hopper — the boat spans y..y+1 upward, so the
    hopper's own cell stays clear and the hopper's source cell is the boat's cell;
  * push cases put the boat one cell **below** the hopper, where the boat's box does poke 0.0625
    into the hopper's cell, so the hopper's cell is deliberately **not** watched. B instead proves
    that nothing moved by draining the hopper into a chest placed in the boat's old cell afterwards.
The stand-alone consequence of the box extent is left to a separate probe; see agentN notes.

Draw ledger (all draw ticks disjoint, so every `randomState` step is attributable):
  A ticks 5, 13            nextInt(1), value unobservable, the pull moves one item each time
  B ticks 17..27           nextInt(1) every tick, nothing moves at all
  C ticks 29, 37, 45       nextInt(2), the choice is visible in which inventory loses an item
  D ticks 53, 61, 69       nextInt(2), reversed spawn order
  E ticks 77, 85           nextInt(1), the push succeeds
Everything else is silent: an empty hopper never reaches `ejectItems`, and `getEntityContainer`
returns null **without drawing** when the box holds no candidate.
"""
from captureRedstone import state
from captureVanillaScenario import runVanillaCapture

commands = []
watch = []

SEED = 20260910
ORIGIN = [64, -59, 64]
END = 88


def put(tick, pos, name, **props):
    commands.append({'tick': tick, 'pos': list(pos), 'stateId': state(name, **props)})


def floor(x, z, y=0, radius=1):
    for dx in range(-radius, radius + 1):
        for dz in range(-radius, radius + 1):
            put(0, (x + dx, y, z + dz), 'stone')


def entities(tick, pos, rows):
    commands.append({'tick': tick, 'pos': list(pos), 'stimulus': {'containerEntities': rows}})


def vessel(kind, *rows):
    return {'type': kind, 'inventory': [{'slot': slot, 'item': item, 'count': count} for slot, item, count in rows]}


def inventory(tick, pos, rows):
    commands.append({'tick': tick, 'pos': list(pos), 'stimulus': {'inventory': rows}})


# A. One chest boat above a hopper. The hopper faces down into the stone floor, so its eject side
#    finds neither a block container nor an entity and never draws; the only draws are the source
#    lookups at ticks 5 and 13. Slot 26 holds an item the run never reaches, which is the flat
#    27 slot order `getSlots` gives a container that is not a `WorldlyContainer`.
floor(4, 4)
put(0, (4, 1, 4), 'hopper', facing='down')
entities(4, (4, 2, 4), [vessel('oak_chest_boat', (0, 'minecraft:stone', 6), (26, 'minecraft:dirt', 1))])
entities(15, (4, 2, 4), [])
watch += [[4, 1, 4], [4, 2, 4]]

# B. A chest raft with 27 full stacks under a hopper that holds a single item. `ejectItems` draws
#    in `getAttachedContainer` and then `isFullContainer` refuses, so the tick moves nothing and
#    leaves the cooldown alone — the next tick draws again, and so on for as long as the raft is
#    there. The hopper's own cell is not watched (see the geometry note); instead the raft is
#    withdrawn at tick 27 and a chest takes its place at tick 28, so tick 29 drains the hopper's
#    single item into the chest and proves it survived all eleven idle ticks.
floor(11, 4)
put(0, (11, 2, 4), 'hopper', facing='down')
inventory(0, (11, 2, 4), [{'slot': 0, 'item': 'minecraft:stone', 'count': 1}])
entities(16, (11, 1, 4), [vessel('bamboo_chest_raft', *((slot, 'minecraft:stone', 64) for slot in range(27)))])
entities(27, (11, 1, 4), [])
put(28, (11, 1, 4), 'chest')
watch += [[11, 1, 4]]

# C. A chest minecart and a chest boat in one cell, the minecart declared first. Both are spawned
#    at the same point, so `AbstractMinecart.push` sees a squared separation below 1e-4 and does
#    nothing; the pair simply stands there. `nextInt(2)` then decides which one loses an item on
#    each of the three pulls, and the two item types make the choice visible.
floor(18, 4)
put(0, (18, 1, 4), 'hopper', facing='down')
entities(28, (18, 2, 4), [vessel('chest_minecart', (0, 'minecraft:stone', 8)),
                          vessel('oak_chest_boat', (0, 'minecraft:dirt', 8))])
entities(51, (18, 2, 4), [])
watch += [[18, 1, 4], [18, 2, 4]]

# D. The same pair with the boat declared first and the item types swapped: the reported candidate
#    order follows the spawn order, not the entity class, and the pulled item names show it.
floor(25, 4)
put(0, (25, 1, 4), 'hopper', facing='down')
entities(52, (25, 2, 4), [vessel('oak_chest_boat', (0, 'minecraft:stone', 8)),
                          vessel('chest_minecart', (0, 'minecraft:dirt', 8))])
entities(75, (25, 2, 4), [])
watch += [[25, 1, 4], [25, 2, 4]]

# E. Pushing into an empty chest raft. The hopper stays empty (and therefore silent) until tick 76;
#    from then on it holds three items and the raft accepts one per successful push, which does set
#    the 8 gt cooldown — draws at 77 and 85 only. Slot 0 filling first is again the flat order.
floor(32, 4)
put(0, (32, 2, 4), 'hopper', facing='down')
inventory(76, (32, 2, 4), [{'slot': 0, 'item': 'minecraft:stone', 'count': 3}])
entities(76, (32, 1, 4), [vessel('bamboo_chest_raft')])
watch += [[32, 1, 4]]

if __name__ == '__main__':
    runVanillaCapture(commands, watch, END, 'java26_2BoatContainers', ORIGIN,
                      randomSeed=SEED, watchContainerEntities=True)
