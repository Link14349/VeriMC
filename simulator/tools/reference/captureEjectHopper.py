#!/usr/bin/env python3
"""投掷器朝空气抛出 -> 物品飞行落下 -> 漏斗吸走：这一整条链路的原版捕获。

场景（相对捕获原点）：

    (10,6,10) dropper facing=down，槽 4 放 2 个 stone；(11,6,10) 在第 2 刻放红石块触发
    (10,5,10) 及以下留空 —— 目标格是空气，所以原版走的是 DefaultDispenseItemBehavior.spawnItem
    (10,1,10) hopper facing=down，下面 (10,0,10) 是石头（没有容器可推）
    (11,1,10) 从第 0 刻起放红石块 —— 漏斗被充能，飞行途中不会把物品吸走
              到第 `UNPOWER` 刻撤掉红石块，漏斗才开始吸

**关于中间那一段的口径（必须原样保留）**：内核**没有实现也没有验证任何物品运动**。
投掷点到落点之间的飞行（初速、重力、阻力、与漏斗碗底的碰撞、静止）在内核里根本不存在。
本脚本产出的 fixture 只证明两件事：

  1. 抛出**瞬间**的初值（位置、速度，逐位 IEEE754）与原版一致 —— 由 `expectedActions` 比对；
  2. 给定一个**外部声明的落点**之后，漏斗的吸取时序与原版一致 —— 由 groundItems/inventories 比对。

中间那一段是**外部输入**：fixture 在 `HANDOFF` 刻用 groundItems 刺激声明「物品此刻停在这里」，
坐标 `REST` 是从原版实测轨迹里读出来的（见下），不是算出来的。原版侧在同一刻把那个还在
下落的真实掉落物撤走、换成这条声明（CaptureRedstone 的 groundItems 刺激现在会清掉漏斗那三格
立方里的真实掉落物，因为这条刺激是该格可见掉落物集合的**全量声明**）。交接刻选在物品已经
进入漏斗那一列、但**还没进入吸取体积**（y 还在漏斗上方 2 格以上）的时候，所以两侧在交接之前
的 groundItems 观测都是空的。

`--probe` 模式跑同一个场景但**不做交接**，物品真的飞完全程并停下，逐刻记录掉落物的位置、
速度和 onGround。REST / HANDOFF 两个常数就是从那次实测里读出来的。那次全程真实飞行的探针
在第 55 刻撤掉红石块、第 56 刻漏斗吸走物品；带交接的这份 fixture 必须给出同样的时序，
这就是「外部声明确实替代了那个真实掉落物」的证据。
"""
import sys

from captureRedstone import runCapture, state

SEED = 20260910

DROPPER = (10, 6, 10)
HOPPER = (10, 1, 10)
ENDTICK = 70
UNPOWER = 55            # 撤掉漏斗旁边的红石块，让它开始吸

# ---- 以下两项来自 `--probe` 实测（origin = [8238952,-58,944185]），脚本本身不推导它们 ----
# 实测轨迹（相对漏斗方块角点，逐刻）：
#   6  (0.4635, 4.7500, 0.5410)  抛出当刻就被上方投掷器的底面挡住，y 速度被撞成 0
#   ...
#   15 (0.1665, 3.0427, 0.8746)  还在漏斗那三格立方之外
#   16 (0.1367, 2.6769, 0.9080)  进入那三格立方，但 y > 2 还不在吸取体积里
#   17 (0.1075, 2.2783, 0.9408)  同上
#   18 (0.0789, 1.8478, 0.9729)  **第一次进入吸取体积**；原版 getItemsAtAndAbove 从这一刻起非空
#   19 (0.0508, 1.3858, 1.0044)
#   20 (0.0233, 1.0000, 1.0353)  落地：停在漏斗的边缘顶面 y=+1.0，onGround
#   21..44 只在水平方向以指数衰减继续蹭，44 刻之后不再变化
# 交接刻取 18：正好是原版第一次报告掉落物在吸取范围里的那一刻，
# 于是「有没有可吸的掉落物」这条观测在交接前后与全程真实飞行的那次探针**逐刻一致**。
HANDOFF = 18
# 原版实测落点：物品最终停下时相对漏斗方块角点的偏移（第 44 刻起不再变化）。
REST = {'x': -0.015568056143820286, 'y': 1.0, 'z': 1.0789726477814838}

commands = []
watch = [list(DROPPER), list(HOPPER)]


def put(tick, pos, name, **props):
    commands.append({'tick': tick, 'pos': list(pos), 'stateId': state(name, **props)})


def clear(tick, pos):
    commands.append({'tick': tick, 'pos': list(pos), 'stateId': 0})


def build(handoff):
    commands.clear()
    for dx in range(-1, 2):
        for dz in range(-1, 2):
            put(0, (HOPPER[0] + dx, 0, HOPPER[2] + dz), 'stone')
    put(0, HOPPER, 'hopper', facing='down')
    put(0, (HOPPER[0] + 1, HOPPER[1], HOPPER[2]), 'redstone_block')
    put(0, DROPPER, 'dropper', facing='down')
    commands.append({'tick': 0, 'pos': list(DROPPER),
                     'stimulus': {'inventory': [{'slot': 4, 'item': 'minecraft:stone', 'count': 2}]}})
    put(2, (DROPPER[0] + 1, DROPPER[1], DROPPER[2]), 'redstone_block')
    if handoff is not None:
        commands.append({'tick': handoff, 'pos': list(HOPPER),
                         'stimulus': {'groundItems': [{'item': 'minecraft:stone', 'count': 1, **REST}]}})
    clear(UNPOWER, (HOPPER[0] + 1, HOPPER[1], HOPPER[2]))


if __name__ == '__main__':
    probe = '--probe' in sys.argv
    build(None if probe else HANDOFF)
    options = dict(randomSeed=SEED, watchGroundItems=True,
                   watchEjections=[list(DROPPER)], forceLoadedNeighborhood=True)
    if probe:
        # 轨迹只在探针里记：逐刻坐标依赖捕获原点的浮点量级，不适合当签入 fixture 的断言。
        target = sys.argv[sys.argv.index('--probe') + 1]
        runCapture(commands, watch, ENDTICK, 'java26_2EjectHopper',
                   capturePath=target, watchItemEntities=True, **options)
    else:
        runCapture(commands, watch, ENDTICK, 'java26_2EjectHopper', **options)
