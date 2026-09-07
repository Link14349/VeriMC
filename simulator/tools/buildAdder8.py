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
    # Reuse a track only after its last consumer. The right-to-left carry
    # input keeps its own track, since its source is beyond the final gate.
    netRows = {
        'a': 0, 'q': 0, 's': 0, 'u': 0, 'sum': 0,
        'b': 3, 'r': 3, 't': 3,
        'c': 6, 'p': 9, 'carry': 9, 'v': 12,
    }
    gatePitch, bitPitch = 6, 22
    busY, bridgeY, carryY = 4, 7, 10
    busEnd = 62
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
        baseZ = bit * bitPitch
        consumers = {name: [] for name in netRows}
        for index, (_, left, right) in enumerate(gates, 1):
            x = index * gatePitch
            consumers[left].append(x - 2)
            consumers[right].append(x + 2)
        for net, row in netRows.items():
            z = baseZ + row
            color = colors.get(net, 'white_concrete')
            startX = -4 if net in ('a', 'b') else busEnd if net == 'c' else (
                gatePitch * (next(i for i, gate in enumerate(gates) if gate[0] == net) + 1) + 2)
            endX = min(consumers[net]) if net == 'c' else max(consumers[net], default=busEnd)
            for x in range(min(startX, endX), max(startX, endX) + 1):
                if x % 12 == 0 and x not in (startX, endX):
                    part((x, busY, z), 'repeater', color, facing='east' if net == 'c' else 'west', delay='1')
                else:
                    part((x, busY, z), color=color)

        for name, value in [('a', inputA), ('b', inputB)]:
            pos = (-5, busY, baseZ + netRows[name])
            part(pos, 'lever', colors[name], face='floor', facing='west', powered=str(bool(value & (1 << bit))).lower())
            ports[name].append(list(pos))
            probe(pos, f'{name.upper()}{bit}')
        if bit == 0:
            pos = (busEnd + 1, busY, baseZ + netRows['c'])
            part(pos, 'lever', colors['c'], face='floor', powered=str(bool(carryIn)).lower())
            ports['carryIn'] = list(pos)
            probe(pos, 'Cin')

        for index, (net, left, right) in enumerate(gates, 1):
            x = index * gatePitch
            gateZ = baseZ + min(netRows[left], netRows[right], netRows[net]) - 5
            directOutput = (netRows[net] == netRows[left] <= netRows[right]
                            and max(consumers[left]) == x - 2)
            put((x, 0, gateZ), 'white_concrete')
            put((x, 1, gateZ), 'white_concrete')
            put((x, 1, gateZ + (1 if directOutput else -1)), 'redstone_wall_torch',
                facing='south' if directOutput else 'north', lit='true')
            for side, source in [(-1, left), (1, right)]:
                color = colors.get(source, 'white_concrete')
                part((x + side, 1, gateZ), 'repeater', color, facing='west' if side == -1 else 'east', delay='1')
                inputX = x + side * 2
                inputZ = baseZ + netRows[source]
                # Three-block layer spacing and three-block track pitch keep
                # the descending branches clear of the other signal tracks.
                for step in range(1, busY):
                    part((inputX, busY - step, inputZ - step), color=color)
                for z in range(gateZ, inputZ - (busY - 1)):
                    if z != gateZ and (inputZ - busY - z) % 8 == 0:
                        part((inputX, 1, z), 'repeater', color, facing='south', delay='1')
                    else:
                        part((inputX, 1, z), color=color)

            color = colors.get(net, 'white_concrete')
            if directOutput:
                # This gate ends the old signal's lifetime. Its output can
                # climb straight into the vacated track without a bridge.
                for step in range(busY):
                    part((x, 1 + step, gateZ + 2 + step), color=color)
                part((x + 1, busY, baseZ + netRows[net]), 'repeater', color, facing='west', delay='1')
                continue

            # Other outputs cross above the still-live buses.
            topZ = gateZ - 2 - (bridgeY - 1)
            for step in range(bridgeY):
                part((x, 1 + step, gateZ - 2 - step), color=color)
            for offset in (1, 2):
                part((x + offset, bridgeY, topZ), color=color)
            outputZ = baseZ + netRows[net]
            slopeZ = outputZ - (bridgeY - busY)
            for z in range(topZ + 1, slopeZ + 1):
                if z == slopeZ - 1 or (z < slopeZ - 1 and (z - (topZ + 1)) % 10 == 0):
                    part((x + 2, bridgeY, z), 'repeater', color, facing='north', delay='1')
                else:
                    part((x + 2, bridgeY, z), color=color)
            for step in range(1, bridgeY - busY):
                part((x + 2, bridgeY - step, slopeZ + step), color=color)

        sumZ = baseZ + netRows['sum']
        part((busEnd + 1, busY, sumZ), 'repeater', colors['sum'], facing='west', delay='1')
        part((busEnd + 2, busY, sumZ), 'redstone_lamp', colors['sum'], lit='false')
        ports['sum'].append([busEnd + 2, busY, sumZ])
        probe((busEnd + 2, busY, sumZ), f'S{bit}')
        carryZ = baseZ + netRows['carry']
        part((busEnd - 1, busY, carryZ + 1), 'repeater', colors['carry'], facing='north', delay='1')
        part((busEnd - 1, busY, carryZ + 2), 'redstone_lamp', colors['carry'], lit='false')
        ports['carry'].append([busEnd - 1, busY, carryZ + 2])
        probe((busEnd - 1, busY, carryZ + 2), f'C{bit + 1}')

        if bit + 1 < bitCount:
            nextZ = baseZ + bitPitch + netRows['c']
            carryX = busEnd + carryY - busY
            for step in range(1, carryY - busY + 1):
                part((busEnd + step, busY + step, carryZ), color=colors['carry'])
            for z in range(carryZ + 1, nextZ + 1):
                if z == nextZ - 1 or (z < nextZ - 1 and (z - carryZ - 1) % 10 == 0):
                    part((carryX, carryY, z), 'repeater', colors['carry'], facing='north', delay='1')
                else:
                    part((carryX, carryY, z), color=colors['carry'])
            for step in range(1, carryY - busY):
                part((carryX - step, carryY - step, nextZ), color=colors['carry'])

    # Keep the nine result bits visible before the input and intermediate channels.
    resultNames = ['C8'] + [f'S{bit}' for bit in reversed(range(bitCount))]
    probes.sort(key=lambda item: (resultNames.index(item['name']) if item['name'] in resultNames else len(resultNames)))
    return {
        'format': 'verimc.simulator', 'formatVersion': 1,
        'minecraftVersion': '26.2', 'edition': 'java', 'kind': 'circuit',
        'name': '紧凑 8 位加法器 · 37 + 19 = 56',
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
