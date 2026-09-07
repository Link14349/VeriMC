"""Independent, test-only .vmcl consumer. No compiler/AST imports or Minecraft timing."""
class LogicModel:
    def __init__(self, graph):
        self.graph = graph
        self.nodes = graph['nodes']
        self.ports = {p['name']: p for p in graph['ports']}
        self.inputs = {}
        self.state = {n['id']: None for n in self.nodes if n['op'] == 'register'}
        self.drivers = {}
        self.cache = {}
        for edge in graph['connections']:
            for bit in range(edge['width']):
                self.drivers[edge['target'], edge['targetOffset'] + bit] = (edge['source'], edge['sourceOffset'] + bit)

    @staticmethod
    def width(t):
        return t['length'] * LogicModel.width(t['element']) if t['kind'] == 'array' else t['width']

    def drive(self, **values):
        for name, value in values.items():
            port = self.ports[name]
            assert port['direction'] == 'input'
            self.inputs[port['node']] = value
        self.cache.clear()

    def value(self, nodeId):
        t = self.nodes[nodeId]['type']
        width = self.width(t)
        value = sum(self.bit(nodeId, b) << b for b in range(width))
        if t['kind'] == 'int' and value & (1 << (width - 1)):
            value -= 1 << width
        return value

    def bit(self, nodeId, bit):
        key = nodeId, bit
        if key not in self.cache:
            self.cache[key] = (self.calculate(nodeId, bit) >> bit) & 1
        return self.cache[key]

    def calculate(self, nodeId, bit):
        n = self.nodes[nodeId]
        op, refs, attrs = n['op'], n['inputs'], n['attributes']
        if op == 'input':
            if nodeId not in self.inputs:
                raise ValueError('Undriven input')
            return self.inputs[nodeId]
        if op == 'constant':
            return int(attrs['value'])
        if op == 'register':
            if self.state[nodeId] is None:
                raise ValueError('Uninitialized register')
            return self.state[nodeId]
        if op == 'signal':
            ref, offset = self.drivers[nodeId, bit]
            return self.bit(ref, offset) << bit
        if op == 'slice':
            return self.bit(refs[0], bit + attrs['offset']) << bit
        if op in ('reinterpret', 'low'):
            return self.bit(refs[0], bit) << bit
        if op == 'mux':
            return self.bit(refs[1] if self.value(refs[0]) else refs[2], bit) << bit
        if op == 'logicalAnd':
            return int(bool(self.value(refs[0])) and bool(self.value(refs[1])))
        if op == 'logicalOr':
            return int(bool(self.value(refs[0])) or bool(self.value(refs[1])))
        if op == 'array' or op == 'concat':
            offset = 0
            for ref in refs if op == 'array' else reversed(refs):
                width = self.width(self.nodes[ref]['type'])
                if offset <= bit < offset + width:
                    return self.bit(ref, bit - offset) << bit
                offset += width
            raise AssertionError('Concat range')
        if op in ('and', 'or', 'xor'):
            a, b = [self.bit(ref, bit) for ref in refs]
            return {'and': a & b, 'or': a | b, 'xor': a ^ b}[op] << bit
        if op in ('bitNot', 'logicalNot'):
            return (1 - self.bit(refs[0], bit)) << bit
        if op in ('add', 'sub', 'wrapAdd', 'wrapSub', 'neg'):
            def lowValue(ref):
                t = self.nodes[ref]['type']
                width = self.width(t)
                value = sum(self.bit(ref, i) << i for i in range(min(width, bit + 1)))
                if t['kind'] == 'int' and bit >= width and self.bit(ref, width - 1):
                    value -= 1 << width
                return value
            a = lowValue(refs[0])
            if op == 'neg':
                return -a
            b = lowValue(refs[1])
            return a - b if op in ('sub', 'wrapSub') else a + b
        values = [self.value(ref) for ref in refs]
        a = values[0]
        if op == 'widen':
            return a
        if op == 'shiftLeft':
            return a << attrs['amount']
        if op == 'shiftRight':
            return a >> attrs['amount']
        b = values[1]
        if op == 'eq': return int(a == b)
        if op == 'ne': return int(a != b)
        if op == 'lt': return int(a < b)
        if op == 'le': return int(a <= b)
        if op == 'gt': return int(a > b)
        if op == 'ge': return int(a >= b)
        raise AssertionError(op)

    def sample(self, name):
        return self.value(self.ports[name]['node'])

    def cycle(self):
        self.cache.clear()
        nextState = {}
        for nodeId in self.state:
            node = self.nodes[nodeId]
            nextRef, _, resetRef = node['inputs']
            nextState[nodeId] = int(node['attributes']['resetValue']) if self.value(resetRef) else self.value(nextRef)
        self.state = nextState
        self.cache.clear()
