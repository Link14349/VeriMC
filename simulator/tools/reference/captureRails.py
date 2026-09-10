#!/usr/bin/env python3
"""Rail shape, powered paths, junction changes and support removal in 26.2."""
from captureRedstone import commands, watch, setBlock, runCapture

commands.clear()
watch.clear()
for x in range(1, 42):
    for z in range(1, 42):
        setBlock(0, (x, 1, z), 'stone')

for z, name in [(3, 'powered_rail'), (7, 'activator_rail')]:
    for x in range(3, 18):
        setBlock(0, (x, 2, z), name, shape='east_west')
        watch.append([x, 2, z])
    setBlock(1, (2, 2, z), 'redstone_block')
    setBlock(5, (2, 2, z), 'air')
    setBlock(9, (11, 1, z), 'redstone_block')
    setBlock(13, (11, 1, z), 'stone')

# Physical links do not let activator power propagate into powered rail.
for x in range(3, 10):
    setBlock(0, (x, 2, 11), 'powered_rail' if x < 6 else 'activator_rail', shape='east_west')
    watch.append([x, 2, 11])
setBlock(1, (2, 2, 11), 'redstone_block')

for center, arms in [((8, 2, 18), [(0,-1),(0,1),(1,0)]), ((18, 2, 18), [(-1,0),(1,0),(0,-1)]), ((28, 2, 18), [(-1,0),(1,0),(0,-1),(0,1)])]:
    x, y, z = center
    setBlock(0, center, 'rail')
    for dx, dz in arms:
        setBlock(0, (x+dx, y, z+dz), 'rail')
        watch.append([x+dx, y, z+dz])
    watch.append(list(center))
    setBlock(2, (x, y+1, z), 'redstone_block')
    setBlock(6, (x, y+1, z), 'air')
    setBlock(10, (x+1, y, z), 'air')
    setBlock(14, (x, y+1, z), 'redstone_block')

# Ascent in both horizontal axes and directions.
for baseX, z, dx, dz in [(5,27,1,0),(18,27,-1,0),(27,27,0,1),(36,31,0,-1)]:
    for distance in range(7):
        x, zz = baseX + dx*distance, z + dz*distance
        y = 2 if distance < 3 else 3
        if y == 3: setBlock(0, (x, 2, zz), 'stone')
        setBlock(0, (x, y, zz), 'powered_rail', shape='east_west' if dx else 'north_south')
        watch.append([x, y, zz])
    setBlock(1, (baseX-dx, 2, z-dz), 'redstone_block')
    setBlock(5, (baseX-dx, 2, z-dz), 'air')
    setBlock(9, (baseX+dx*3, 2, z+dz*3), 'air')

for x, cartType, size in [(3, 'minecart', 0), (15, 'chest_minecart', 27), (27, 'hopper_minecart', 5)]:
    pos = [x, 2, 38]
    setBlock(0, pos, 'detector_rail', shape='east_west')
    for offset in [1, 2, 3]:
        setBlock(0, (x+offset, 2, 38), 'powered_rail', shape='east_west')
        watch.append([x+offset, 2, 38])
    setBlock(0, (x, 2, 39), 'comparator', facing='north')
    cart = {'type': cartType}
    if size:
        cart['inventory'] = [{'slot': 0, 'item': 'minecraft:ender_pearl', 'count': 16}]
    commands.append({'tick': 1, 'pos': pos, 'stimulus': {'carts': [cart]}})
    if size:
        commands.append({'tick': 5, 'pos': pos, 'stimulus': {'cartInventory': [{'slot': i, 'item': 'minecraft:stone', 'count': 64} for i in range(size)]}})
    commands.append({'tick': 25 if size else 5, 'pos': pos, 'stimulus': {'carts': []}})
    watch += [pos, [x, 2, 39]]

# 命令方块矿车的比较器读数是它内部命令方块的 successCount，且**优先于**容器矿车。
# 本项目没有命令解释器，该计数是显式外部输入。
for x, counts in [(39, [7, 0, 15])]:
    pos = [x, 2, 38]
    setBlock(0, pos, 'detector_rail', shape='east_west')
    setBlock(0, (x, 2, 39), 'comparator', facing='north')
    watch += [pos, [x, 2, 39]]
    for index, count in enumerate(counts):
        # 第三次同时放一辆装满的运输矿车，验证命令方块矿车优先。
        carts = [{'type': 'command_block_minecart', 'successCount': count}]
        if index == 2:
            carts.append({'type': 'chest_minecart', 'inventory': [{'slot': 0, 'item': 'minecraft:stone', 'count': 64}]})
        commands.append({'tick': 1 + index * 8, 'pos': pos, 'stimulus': {'carts': carts}})
    commands.append({'tick': 1 + len(counts) * 8, 'pos': pos, 'stimulus': {'carts': []}})

runCapture(commands, watch, endTick=44, fixtureName='java26_2Rails')
