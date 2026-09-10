#!/usr/bin/env python3
"""R13：`FullNeighborUpdate` 携带的**过期快照**被原样写回世界的可达见证。

26.2 只有直线铁轨会给**自己**排一个 Full 更新：`BaseRailBlock.updateState` 先跑
`updateDir`（`RailState.place()`，里面已经 `setBlock` 过自己和相连铁轨），**之后**才
`level.neighborChanged(state, pos, this, ...)`。所以这条 Full 是这批兄弟里最后入队的，
`place()` 引发的整棵级联在它之前跑完；只要级联把这一格改掉，Full 就会带着过期状态执行。

`PoweredRailBlock.updateState` 是把差别写进世界的那一步：
`isPowered` 取自**快照**，`shouldPower` 取自**世界**，一旦两者不同就
`level.setBlock(pos, state.setValue(POWERED, shouldPower), 3)` —— 把**快照整体**写回去，
连快照里的 `SHAPE` 一起。于是「快照」与「实时重读」得到不同的铁轨形状。

场景（每组自带地面，组间相隔 16 格）：

    z-1        J(rail)      lever(powered)
    z          W(wire)  P   K(rail)   Ke(rail)
    z+1                 torch         Ks(rail)
                        x-1  x   x+1  x+2

- 第 0..3 刻 P 是红石块，把 W 拉到 15；K 被拐角规则钉在 SOUTH_EAST。
- 第 4 刻把 P 换成直线铁轨（放置形状 east_west）。`onPlace` → `updateState`：
  `place()` 算出 NORTH_SOUTH（只有北边 J 是可连铁轨），与放置形状不同，所以内层
  `setBlock(P, NORTH_SOUTH, 3)` 真的改了方块，发出 `MultiNeighborUpdate(P, powered_rail)`。
- 那条 Multi 通知到 W。红石块没了，W 掉到 0，`DefaultRedstoneWireEvaluator.updatePowerStrength`
  对自己和六个邻居各发一条 `updateNeighborsAt(·, REDSTONE_WIRE)`；其中一条落到 P 的邻居集合上，
  **以红石粉（信号源）为来源**通知 K。
- `RailBlock.updateState` 只在「来源是信号源且有 3 个潜在连接」时重排形状：K 恰好三连
  （P/Ke/Ks），拐角在有信号时翻成 SOUTH_WEST，于是 `connectTo(P)` 把 **P 改成 EAST_WEST**。
- 级联结束，最后那条 Full 才执行，带的是 NORTH_SOUTH 快照：
  `shouldPower`（torch 给的，true）≠ 快照的 `POWERED`（false），
  于是把 `NORTH_SOUTH` 连同 `POWERED=true` 一起写回 —— **形状被快照覆盖回 NORTH_SOUTH**。
  若把 Full 实现成执行时重读世界，这一步读到的是 EAST_WEST 且 POWERED 已经是 true，
  条件不成立、不会写回，P 停在 EAST_WEST。两种语义在 P 这一格上给出不同方块状态。

对照组用普通 `rail`：`isStraight` 为假，`BaseRailBlock.updateState` 不发这条 Full，
同一套级联照跑，P 停在级联算出的形状上。
"""
from captureRedstone import state
from captureVanillaScenario import runVanillaCapture

commands = []
watch = []


def put(tick, pos, name, **props):
    commands.append({'tick': tick, 'pos': list(pos), 'stateId': state(name, **props)})


def group(x, z, railName):
    """One race site. `railName` is the block placed at P on tick 4."""
    for dx in range(-3, 5):
        for dz in range(-3, 4):
            put(0, (x + dx, 1, z + dz), 'stone')
    # P: a redstone block first, so the wire has something to lose.
    put(0, (x, 2, z), 'redstone_block')
    # J gives P its single connectable neighbour, so `place()` computes NORTH_SOUTH.
    put(0, (x, 2, z - 1), 'rail', shape='north_south')
    # The three-way junction that flips on a signal-source notification.
    put(0, (x + 1, 2, z), 'rail', shape='south_east')
    put(0, (x + 2, 2, z), 'rail', shape='east_west')
    put(0, (x + 1, 2, z + 1), 'rail', shape='north_south')
    # The junction's standing signal; a lever is not a rail, so K keeps exactly three
    # potential connections once P becomes one.
    put(0, (x + 1, 2, z - 1), 'lever', face='floor', facing='north', powered='true')
    # The wire that carries the redstone-block loss into a signal-source notification.
    put(0, (x - 1, 2, z), 'redstone_wire')
    # P's own standing signal, so `shouldPower` is true when the stale update runs.
    put(0, (x, 2, z + 1), 'redstone_torch')
    watch.extend([[x, 2, z], [x, 2, z - 1], [x + 1, 2, z], [x + 2, 2, z],
                  [x + 1, 2, z + 1], [x - 1, 2, z]])
    # Pin the shapes before the race: same block, so `onPlace` skips `updateState`.
    put(2, (x + 1, 2, z), 'rail', shape='south_east')
    put(2, (x, 2, z - 1), 'rail', shape='north_south')
    # The race itself. The placed shape differs from the computed one on purpose, so the
    # nested `setBlock` inside `place()` is a real change and emits the multi update.
    put(4, (x, 2, z), railName, shape='east_west')


# Straight rails: the only blocks that notify themselves with a state snapshot.
group(6, 6, 'powered_rail')
group(22, 6, 'detector_rail')
group(38, 6, 'activator_rail')
# Control: a curvable rail runs the identical cascade but never issues the full update.
group(6, 22, 'rail')

if __name__ == '__main__':
    # 严格 vanilla-only 服务器，固定原点，方便在同一坐标上复现这条竞争。
    runVanillaCapture(commands, watch, 12, 'java26_2FullUpdateSnapshot', [64, -59, 64],
                      discardDrops=True, updateTraceLimit=200000)
