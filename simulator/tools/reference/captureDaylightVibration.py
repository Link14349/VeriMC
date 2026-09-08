#!/usr/bin/env python3
"""Daylight detector `inverted` toggles reaching unmodified vanilla vibration listeners.

The detector itself is never observed: its own POWER depends on sky brightness, which is
an explicit external stimulus in the simulator, and must not be mixed into the event chain
under test. Groups are spaced beyond the 8 / 16 block listener ranges so each detector
only feeds its own sensor.
"""
from captureRedstone import commands, watch, setBlock, runCapture

commands.clear()
watch.clear()


def interact(tick, pos):
    commands.append({'tick': tick, 'pos': list(pos), 'interact': True})


def floor(x, z, width, depth=1):
    for dx in range(width):
        for dz in range(depth):
            setBlock(0, (x + dx, 1, z + dz), 'stone')


def filterChain(x, z, strength):
    """Redstone wire ending at x with the requested strength, fed from a redstone block."""
    length = 15 - strength
    for offset in range(length + 1):
        setBlock(0, (x + offset, 2, z), 'redstone_wire')
    setBlock(0, (x + length + 1, 2, z), 'redstone_block')


# A: plain sensor three blocks away; the second toggle waits out the 30 gt active phase and
# the 10 gt cooldown, so the inverted -> normal direction is observed as its own activation.
floor(2, 3, 8)
setBlock(0, (3, 2, 3), 'daylight_detector')
setBlock(0, (6, 2, 3), 'sculk_sensor')
setBlock(0, (7, 2, 3), 'redstone_lamp')
watch.extend([[6, 2, 3], [7, 2, 3]])
interact(10, (3, 2, 3))
interact(60, (3, 2, 3))

# B: calibrated sensor tuned to 11 accepts block_change.
floor(2, 23, 13)
setBlock(0, (3, 2, 23), 'daylight_detector')
setBlock(0, (6, 2, 23), 'calibrated_sculk_sensor', facing='west')
filterChain(7, 23, 11)
watch.append([6, 2, 23])
interact(10, (3, 2, 23))

# C: calibrated sensor tuned to 9 must reject the same event.
floor(22, 3, 15)
setBlock(0, (23, 2, 3), 'daylight_detector')
setBlock(0, (26, 2, 3), 'calibrated_sculk_sensor', facing='west')
filterChain(27, 3, 9)
watch.append([26, 2, 3])
interact(10, (23, 2, 3))

# D: five blocks away, so the travel delay follows the detector's own block centre.
floor(22, 23, 9)
setBlock(0, (23, 2, 23), 'daylight_detector')
setBlock(0, (28, 2, 23), 'sculk_sensor')
setBlock(0, (29, 2, 23), 'redstone_lamp')
watch.extend([[28, 2, 23], [29, 2, 23]])
interact(10, (23, 2, 23))

if __name__ == '__main__':
    runCapture(commands, watch, 80, 'java26_2DaylightVibration', forceLoadedNeighborhood=True)
