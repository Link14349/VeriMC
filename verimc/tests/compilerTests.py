"""End-to-end contract tests, independent arithmetic oracle and graph consumer."""
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from logicModel import LogicModel

cli = str(Path(sys.argv.pop(1)).resolve())
project = Path(__file__).resolve().parents[1]

class CompilerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='verimcTest')
        self.directory = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def runCli(self, *args):
        return subprocess.run([cli, *map(str, args)], text=True, capture_output=True, timeout=30)

    def compile(self, source, top, *args, error=None):
        output = self.directory / 'result.vmcl'
        result = self.runCli('compile', source, '--top', top, '-o', output, '--force', *args)
        if error:
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertEqual(json.loads(result.stderr)['code'], error, result.stderr)
            return
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.runCli('validate', output).returncode, 0)
        return json.loads(output.read_text())

    def source(self, body, name='test.vmc'):
        path = self.directory / name
        path.write_text('language "0.1";\npackage fixtures;\n' + body, encoding='utf-8')
        return path

    def testGrammarCorpus(self):
        manifest = json.loads((project / 'examples/exampleCases.json').read_text())
        for case in manifest['cases']:
            with self.subTest(case=case['file']):
                result = self.runCli('parse', project / 'examples' / case['file'])
                self.assertEqual(result.returncode == 0, case['syntax'] == 'accept', result.stderr)
        for path in (project / 'tutorials').glob('*.vmc'):
            self.assertEqual(self.runCli('parse', path).returncode, 0)

    def testAllPureModules(self):
        files = {
            'adder': ['Adder'], 'counter': ['Counter'], 'counterPair': ['CounterPair'],
            'registerSwap': ['RegisterSwap'], 'rippleAdder': ['FullAdder', 'RippleAdder'],
            'stateMachine': ['Controller'], 'valueOperations': ['BitBuffer', 'WordChoice', 'SignalArray'],
        }
        for stem, names in files.items():
            for name in names:
                with self.subTest(module=name):
                    self.compile(project / 'examples/valid' / (stem + '.vmc'), name)

    def testAdderExhaustive(self):
        for file, top in [('adder', 'Adder'), ('rippleAdder', 'RippleAdder')]:
            model = LogicModel(self.compile(project / 'examples/valid' / (file + '.vmc'), top))
            for a in range(16):
                for b in range(16):
                    model.drive(a=a, b=b)
                    self.assertEqual(model.sample('sum'), (a + b) & 15)
                    self.assertEqual(model.sample('carry'), (a + b) >> 4)

    def testCounter(self):
        model = LogicModel(self.compile(project / 'examples/valid/counter.vmc', 'Counter'))
        with self.assertRaises(ValueError): model.sample('count')
        model.drive(reset=1, enable=1); model.cycle()
        self.assertEqual(model.sample('count'), 0)
        model.drive(reset=0)
        for i in range(1, 34):
            model.cycle(); self.assertEqual(model.sample('count'), i & 15)
        model.drive(enable=0); model.cycle()
        self.assertEqual(model.sample('count'), 1)
        model.drive(reset=1); model.cycle()
        self.assertEqual(model.sample('count'), 0)

    def testSimultaneousRegisters(self):
        model = LogicModel(self.compile(project / 'examples/valid/registerSwap.vmc', 'RegisterSwap'))
        model.drive(reset=1); model.cycle()
        self.assertEqual((model.sample('a'), model.sample('b')), (1, 2))
        model.drive(reset=0); model.cycle()
        self.assertEqual((model.sample('a'), model.sample('b')), (2, 1))
        pair = LogicModel(self.compile(project / 'examples/valid/counterPair.vmc', 'CounterPair'))
        pair.drive(reset=1, enableA=1, enableB=1); pair.cycle()
        pair.drive(reset=0); pair.cycle()
        self.assertEqual(pair.sample('sum'), 2)

    def testEnumAndArray(self):
        model = LogicModel(self.compile(project / 'examples/valid/stateMachine.vmc', 'Controller'))
        model.drive(reset=1, start=0); model.cycle()
        self.assertEqual(model.sample('busy'), 0)
        model.drive(reset=0, start=1); model.cycle()
        self.assertEqual(model.sample('busy'), 1)
        model.cycle(); self.assertEqual(model.sample('busy'), 0)
        array = LogicModel(self.compile(project / 'examples/valid/valueOperations.vmc', 'SignalArray'))
        for value in range(16):
            array.drive(a=value); self.assertEqual(array.sample('y'), value)
        choice = LogicModel(self.compile(project / 'examples/valid/valueOperations.vmc', 'WordChoice'))
        choice.drive(chooseB=0, a=10, b=3); self.assertEqual(choice.sample('y'), 5)
        choice.drive(chooseB=1); self.assertEqual(choice.sample('y'), 3)

    def testStaticRejections(self):
        manifest = json.loads((project / 'examples/exampleCases.json').read_text())
        for case in manifest['cases']:
            if case['stage'] != 'static' or case['expected'] == 'pass': continue
            file = project / 'examples' / case['file']
            # Only top-name discovery in fixtures; never used by compiler or graph oracle.
            import re
            top = re.search(r'module\s+(\w+)', file.read_text()).group(1)
            with self.subTest(case=case['file']):
                self.compile(file, top, '--root', project / 'examples', error=case['expected'])

    def testNumericBoundaries(self):
        file = self.source('''module Numbers {
          input a: int<4>; input b: int<4>;
          output sum: int<5>; output diff: int<5>; output widened: int<8>;
          output negated: int<5>; output wrapped: int<4>; output shifted: int<4>;
          connect sum = a + b; connect diff = a - b; connect widened = widen(a, 8);
          connect negated = -a; connect wrapped = wrapAdd(a,b); connect shifted = a >> 100000000000000000000;
        }''')
        model = LogicModel(self.compile(file, 'Numbers'))
        for a in range(-8, 8):
            for b in range(-8, 8):
                model.drive(a=a, b=b)
                expected = {'sum': a+b, 'diff': a-b, 'widened': a, 'negated': -a, 'wrapped': ((a+b+8)%16)-8, 'shifted': -1 if a<0 else 0}
                for name, value in expected.items(): self.assertEqual(model.sample(name), value)
        big = self.source('''const wide: nat = 4096;
          module Big { output a: uint<wide>; connect a = u(wide, 340282366920938463463374607431768211457); }''')
        model = LogicModel(self.compile(big, 'Big'))
        self.assertEqual(model.sample('a'), (1 << 128) + 1)

    def testExtraErrors(self):
        cases = [
          ('module A { wire x: bit; }', 'EUndriven'),
          ('module A { input x: bit; connect x = true; }', 'EDriveDirection'),
          ('module A { input x: uint<4>; output y: uint<4>; connect y = x * x; }','EOperatorDomain'),
          ('module A { input x: uint<4>; output y: bit; connect y = x[4]; }','EWidthRange'),
          ('module A { inst self: A; }','ERecursiveDesign'),
          ('module A { output y: bit; comb { if true { y = true; } } }','EUndriven'),
          ('module A { output y: bit; comb { y = false; y = true; } }','EMultipleDriver'),
          ('module A { reg x: bit reset false; }','EUnownedRegister'),
          ('module A { output x: nat; }','ETypeMismatch'),
          ('module A { output y: bit; connect y = false ? u(4,1) : true; }','ETypeMismatch'),
        ]
        for body, error in cases:
            with self.subTest(error=error, body=body): self.compile(self.source(body), 'A', error=error)

    def testDeterminismAndSourceMap(self):
        file = self.source('// 中文定位\nmodule A { output y: bit; connect y = true; }')
        graph = self.compile(file, 'A')
        first = (self.directory / 'result.vmcl').read_bytes()
        self.compile(file, 'A')
        self.assertEqual(first, (self.directory / 'result.vmcl').read_bytes())
        self.assertEqual(graph['sources'][0]['sha256'], hashlib.sha256(file.read_bytes()).hexdigest())
        for node in graph['nodes']:
            span = node['source']
            fragment = file.read_bytes()[span['start']:span['end']].decode('utf-8')
            self.assertTrue(fragment)
        self.compile(file, 'A', '--max-nodes', '1', error='EElaborationLimit')
        self.assertEqual(first, (self.directory / 'result.vmcl').read_bytes())
        other = self.directory / 'copy'; other.mkdir()
        otherFile = other / file.name; otherFile.write_bytes(file.read_bytes())
        self.compile(otherFile, 'A')
        self.assertEqual(first, (self.directory / 'result.vmcl').read_bytes())

    def testImportConstraints(self):
        file = self.source('import "loop.vmc" as loop; module A {}')
        self.source('import "test.vmc" as loop; module B {}', 'loop.vmc')
        self.compile(file, 'A', error='EImportCycle')
        outside = self.directory / 'outside'; outside.mkdir()
        self.source('module Child {}', 'child.vmc')
        source = outside / 'main.vmc'; source.write_text('language "0.1"; package local; import "../child.vmc" as child; module A {}')
        self.compile(source, 'A', error='EImport')
        self.compile(source, 'A', '--root', self.directory)

    def testTutorialsAndParameters(self):
        names = {'adder':'Adder4','counter':'Counter','doubleInverter':'DoubleInverter',
                 'inverter':'Inverter','oneBitMemory':'OneBitMemory','passThrough':'PassThrough','selector':'Selector'}
        for stem, top in names.items():
            self.compile(project / 'tutorials' / (stem + '.vmc'), top)
        graph = self.compile(project / 'examples/valid/adder.vmc', 'Adder', '--param', 'width=8')
        model = LogicModel(graph)
        model.drive(a=255, b=255)
        self.assertEqual((model.sample('sum'), model.sample('carry')), (254, 1))
        self.compile(project / 'examples/valid/adder.vmc', 'Adder', '--param', 'unknown=8', error='EName')
        self.compile(project / 'examples/valid/adder.vmc', 'Adder', '--param', 'width=0', error='ERequire')

    def testAggregatesAndConversions(self):
        source = self.source('''enum Choice { first, second, third }
          module A {
            input c: bit; input a: uint<4>; input data: [int<4>; 2]; input clk: clock; input reset: bit;
            output wordOut: bits<8>; output arr: [int<4>; 2]; output chosen: Choice;
            reg saved: [int<4>; 2] reset [s(4,-1),s(4,2)];
            on rising(clk) reset(reset) { if c { next saved = data; } }
            connect arr = saved;
            connect chosen = c ? Choice::first : Choice::third;
            connect wordOut = concat(asBits(a), repeatBits(c,4));
          }''')
        model = LogicModel(self.compile(source, 'A'))
        model.drive(c=0, a=10, data=0x38, reset=1); model.cycle()
        self.assertEqual(model.sample('arr'), 0x2f)
        self.assertEqual(model.sample('wordOut'), 0xa0)
        self.assertEqual(model.sample('chosen'), 2)
        model.drive(c=1, reset=0); model.cycle()
        self.assertEqual(model.sample('arr'), 0x38)
        self.assertEqual(model.sample('wordOut'), 0xaf)
        self.assertEqual(model.sample('chosen'), 0)

    def testLexicalAndBudgetEdges(self):
        valid = self.source('module A { output y: bit; connect y = true; }')
        text = valid.read_text()
        valid.write_text('\ufeff' + text)
        self.compile(valid, 'A')
        lexical = ['module A { input module: bit; }', 'module A { output y: bit; connect y = @; }',
                   'module A { output y: bit; connect y = 01; }', 'module A { /* unterminated',
                   'module A { output y: bit; connect y = true false; }']
        for text in lexical:
            self.compile(self.source(text), 'A', error='ESyntax')
        self.compile(self.source('module A { output y: bit; connect y = ' + '!'*1000 + 'true; }'), 'A', error='EElaborationLimit')
        self.compile(self.source('const huge: integer = ' + '9'*20000 + '; module A {}'), 'A', error='EElaborationLimit')
        self.compile(self.source('module A { generate lanes for i in 0..100 { wire x: bit; connect x=true; } }'), 'A', '--max-steps', '100', error='EElaborationLimit')
        self.compile(self.source('module A { input x: uint<4097>; }'), 'A', error='EElaborationLimit')

    def testProtectedScopes(self):
        cases = [
          ('fn u(a: bit) -> bit { return a; } module A {}', 'EName'),
          ('fn f(a: bit) -> bit { return f(a); } module A { output y: bit; connect y=f(true); }','ERecursiveDesign'),
          ('module Child { input a: bit; output y: bit; connect y=a; } module A { inst child: Child; output y: bit; connect child.a=true; connect y=child.a; }','EDriveDirection'),
          ('module A { input a: bit; output y: bit; comb { let a=true; y=a; } }','EName'),
          ('module A { input clk: clock; input reset: bit; output y: bit; on rising(clk) reset(reset) { next y=true; } connect y=true; }','ENextTarget'),
          ('cell Device { input a: bit; output y: bit; source "absent.vmcell.json"; } module A { inst child: Device; }','EModelMissing'),
        ]
        for text, code in cases:
            self.compile(self.source(text), 'A', error=code)

    def testPartialCombAndNoFalseCycle(self):
        source = self.source('''module A {
            input c: bit; input a: bits<4>; output y: bits<4>;
            comb { if c { y = a; } else { y[0:2] = b(2,1); y[2:4] = b(2,2); } }
        }''')
        model = LogicModel(self.compile(source, 'A'))
        model.drive(c=0,a=15); self.assertEqual(model.sample('y'),9)
        model.drive(c=1); self.assertEqual(model.sample('y'),15)
        source = self.source('''module A { input x: bit; output y: bit; wire chain: bits<4>;
          connect chain[0]=x; connect chain[1]=chain[0]; connect chain[2]=chain[1];
          connect chain[3]=chain[2]; connect y=chain[3]; }''')
        model = LogicModel(self.compile(source,'A'))
        for value in [0,1]: model.drive(x=value); self.assertEqual(model.sample('y'),value)

    def testExclusiveOutputAndJsonParsing(self):
        source = self.source('module A { output y: bit; connect y=true; }')
        output = self.directory / 'result.vmcl'
        result = self.runCli('compile',source,'--top','A','-o',output)
        self.assertEqual(result.returncode,0,result.stderr)
        original = output.read_bytes()
        result = self.runCli('compile',source,'--top','A','-o',output)
        self.assertNotEqual(result.returncode,0)
        self.assertEqual(output.read_bytes(),original)
        output.write_text('{"format":"verimc.logic","format":"verimc.logic"}')
        self.assertNotEqual(self.runCli('validate',output).returncode,0)
        output.write_text('['*200 + '0' + ']'*200)
        self.assertNotEqual(self.runCli('validate',output).returncode,0)

    def testConstantSelectionValidity(self):
        source = self.source('''const n: nat = false ? 1/0 : 4;
          const good: bit = true || (1/0 == 0);
          module A { output x: uint<n>; output y: bit;
            connect x=select(false,u(4,1/0),u(4,3));
            connect y=good && !(false && (1/0 == 1)); }
        ''')
        model = LogicModel(self.compile(source,'A'))
        self.assertEqual(model.sample('x'),3)
        self.assertEqual(model.sample('y'),1)
        self.compile(self.source('const bad: integer = 1/0; module A {}'),'A',error='EOperatorDomain')
        self.compile(self.source('const bad: bit = false && (u(4,1) * u(4,1) == u(4,1)); module A {}'),'A',error='EOperatorDomain')

    def testDiagnosticSources(self):
        file = self.source('module A { output y: bit; connect y=true; connect y=false; }')
        result = self.runCli('compile',file,'--top','A','-o',self.directory/'bad.vmcl')
        error = json.loads(result.stderr)
        self.assertEqual(error['code'],'EMultipleDriver')
        self.assertEqual(error['instance'],'A')
        self.assertEqual(error['sourceSha256'],hashlib.sha256(file.read_bytes()).hexdigest())
        self.assertTrue(error['related'])

    def testGraphCorruption(self):
        graph = self.compile(project / 'examples/valid/counter.vmc', 'Counter')
        mutations = []
        bad = copy.deepcopy(graph); bad['formatVersion'] = 77; mutations.append(bad)
        bad = copy.deepcopy(graph); bad['nodes'][0]['op'] = 'imaginary'; mutations.append(bad)
        bad = copy.deepcopy(graph); bad['connections'][0]['source'] = 100000; mutations.append(bad)
        bad = copy.deepcopy(graph); bad['connections'].append(bad['connections'][0]); mutations.append(bad)
        bad = copy.deepcopy(graph); bad['connections'].pop(); mutations.append(bad)
        bad = copy.deepcopy(graph); bad['semantics']['stateUpdate'] = 'sequential'; mutations.append(bad)
        for i, bad in enumerate(mutations):
            path = self.directory / f'bad{i}.vmcl'; path.write_text(json.dumps(bad))
            self.assertNotEqual(self.runCli('validate', path).returncode, 0)

if __name__ == '__main__':
    unittest.main()
