#!/usr/bin/env python3
"""Build the fixed, hand-routed eight-bit NOR adder example (no HDL input)."""
import json
from pathlib import Path


def buildAdder(bitCount=8, inputA=37, inputB=19, carryIn=0):
    blocks = {}
    ports = {'a': [], 'b': [], 'sum': [], 'carry': []}
    probes = []
    gates = [
        ('p', 'a', 'b'), ('q', 'a', 'p'), ('r', 'b', 'p'),
        ('s', 'q', 'r'), ('t', 's', 'c'), ('u', 's', 't'),
        ('v', 'c', 't'), ('sum', 'u', 'v'), ('carry', 'p', 't'),
    ]
    netRows = {name: index * 4 for index, name in enumerate(
        ['a', 'b', 'c'] + [gate[0] for gate in gates])}
    colors = {'a': 'light_blue_concrete', 'b': 'orange_concrete',
              'c': 'yellow_concrete', 'sum': 'lime_concrete',
              'carry': 'yellow_concrete'}

    def put(pos, name, **properties):
        row = {'pos': list(pos), 'name': 'minecraft:' + name, 'properties': properties}
        if pos in blocks and blocks[pos] != row:
            raise ValueError(f'Overlapping blocks: {blocks[pos]} / {row}')
        blocks[pos] = row

    def part(pos, name='redstone_wire', color='white_concrete', **properties):
        x, y, z = pos
        put((x, y - 1, z), color)
        put(pos, name, **properties)

    def probe(pos, name):
        probes.append({'pos': list(pos), 'name': name, 'mode': 'output', 'direction': 'up'})

    for bit in range(bitCount):
        baseZ = bit * 72
        consumers = {name: [] for name in netRows}
        for index, (_, left, right) in enumerate(gates, 1):
            x = index * 8
            consumers[left].append(x - 2)
            consumers[right].append(x + 2)
        for net, row in netRows.items():
            z = baseZ + row
            color = colors.get(net, 'white_concrete')
            startX = -4 if net in ('a', 'b') else 82 if net == 'c' else (
                8 * (next(i for i, gate in enumerate(gates) if gate[0] == net) + 1) + 2)
            endX = min(consumers[net]) if net == 'c' else max(consumers[net], default=82)
            for x in range(min(startX, endX), max(startX, endX) + 1):
                if x % 8 == 4 and x not in (startX, endX):
                    part((x, 5, z), 'repeater', color, facing='east' if net == 'c' else 'west', delay='1')
                else:
                    part((x, 5, z), color=color)

        for name, value in [('a', inputA), ('b', inputB)]:
            pos = (-5, 5, baseZ + netRows[name])
            part(pos, 'lever', colors[name], face='floor', facing='west', powered=str(bool(value & (1 << bit))).lower())
            ports[name].append(list(pos))
            probe(pos, f'{name.upper()}{bit}')
        if bit == 0:
            pos = (83, 5, baseZ + netRows['c'])
            part(pos, 'lever', colors['c'], face='floor', powered=str(bool(carryIn)).lower())
            ports['carryIn'] = list(pos)
            probe(pos, 'Cin')

        for index, (net, left, right) in enumerate(gates, 1):
            x = index * 8
            gateZ = baseZ - 6
            put((x, 0, gateZ), 'white_concrete')
            put((x, 1, gateZ), 'white_concrete')
            put((x, 1, gateZ - 1), 'redstone_wall_torch', facing='north', lit='true')
            for side, source in [(-1, left), (1, right)]:
                color = colors.get(source, 'white_concrete')
                part((x + side, 1, gateZ), 'repeater', color, facing='west' if side == -1 else 'east', delay='1')
                inputX = x + side * 2
                inputZ = baseZ + netRows[source]
                # Branch down from the bus. All crossings have four blocks of clearance.
                for step in range(1, 5):
                    part((inputX, 5 - step, inputZ - step), color=color)
                for z in range(gateZ, inputZ - 4):
                    if (inputZ - 5 - z) % 8 == 0:
                        part((inputX, 1, z), 'repeater', color, facing='south', delay='1')
                    else:
                        part((inputX, 1, z), color=color)

            # The output climbs away from the gate, then crosses above all buses.
            color = colors.get(net, 'white_concrete')
            for step in range(9):
                part((x, 1 + step, baseZ - 8 - step), color=color)
            for offset in (1, 2):
                part((x + offset, 9, baseZ - 16), color=color)
            outputZ = baseZ + netRows[net]
            for z in range(baseZ - 15, outputZ - 3):
                if z == outputZ - 5 or (z < outputZ - 5 and (z - (baseZ - 15)) % 10 == 0):
                    part((x + 2, 9, z), 'repeater', color, facing='north', delay='1')
                else:
                    part((x + 2, 9, z), color=color)
            for step in range(1, 4):
                part((x + 2, 9 - step, outputZ - 4 + step), color=color)

        sumZ = baseZ + netRows['sum']
        part((83, 5, sumZ), 'repeater', colors['sum'], facing='west', delay='1')
        part((84, 5, sumZ), 'redstone_lamp', colors['sum'], lit='false')
        ports['sum'].append([84, 5, sumZ])
        probe((84, 5, sumZ), f'S{bit}')
        carryZ = baseZ + netRows['carry']
        part((80, 5, carryZ + 1), 'repeater', colors['carry'], facing='north', delay='1')
        part((80, 5, carryZ + 2), 'redstone_lamp', colors['carry'], lit='false')
        ports['carry'].append([80, 5, carryZ + 2])
        probe((80, 5, carryZ + 2), f'C{bit + 1}')

        if bit + 1 < bitCount:
            nextZ = baseZ + 72 + netRows['c']
            for step in range(1, 9):
                part((82 + step, 5 + step, carryZ), color=colors['carry'])
            for z in range(carryZ + 1, nextZ + 1):
                if z == nextZ - 1 or (z < nextZ - 1 and (z - carryZ - 1) % 10 == 0):
                    part((90, 13, z), 'repeater', colors['carry'], facing='north', delay='1')
                else:
                    part((90, 13, z), color=colors['carry'])
            for step in range(1, 8):
                part((90 - step, 13 - step, nextZ), color=colors['carry'])

    # Keep the nine result bits visible before the input and intermediate channels.
    resultNames = ['C8'] + [f'S{bit}' for bit in reversed(range(bitCount))]
    probes.sort(key=lambda item: (resultNames.index(item['name']) if item['name'] in resultNames else len(resultNames)))
    return {
        'format': 'verimc.simulator', 'formatVersion': 1,
        'minecraftVersion': '26.2', 'edition': 'java', 'kind': 'circuit',
        'name': '8 位加法器 · 37 + 19 = 56',
        'profile': {'experimentalRedstone': False, 'naturalRandomTicks': False, 'loadedRegionOnly': True},
        'randomSource': {'algorithm': 'javaLegacy48', 'seed': '0'},
        'description': 'A、B 为无符号 8 位数；蓝色 A，橙色 B，黄色进位，绿色结果。S0/C1 所在行是最低位。',
        'example': {'bitCount': bitCount, 'inputA': inputA, 'inputB': inputB,
                    'carryIn': carryIn, 'expected': inputA + inputB + carryIn, 'ports': ports},
        'blocks': list(blocks.values()), 'probes': probes,
    }


if __name__ == '__main__':
    output = Path(__file__).resolve().parents[1] / 'examples' / 'adder8.json'
    output.parent.mkdir(parents=True, exist_ok=True)
    project = buildAdder()
    # Keep one block per line so the generated JSON remains inspectable.
    metadata = {key: value for key, value in project.items() if key != 'blocks'}
    header = json.dumps(metadata, ensure_ascii=False, indent=2)[:-2]
    rows = ',\n'.join('    ' + json.dumps(row, ensure_ascii=False) for row in project['blocks'])
    output.write_text(header + ',\n  "blocks": [\n' + rows + '\n  ]\n}\n')
    print(f'{output}: {len(project["blocks"])} blocks')
