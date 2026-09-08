"""Failure-path checks for the reference fuzzer; no game launch or cache mutation."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

root = Path(__file__).resolve().parents[1]
captureStub = types.ModuleType('captureRedstone')
captureStub.runCapture = None
captureStub.blocks = {}
captureStub.state = None
spec = importlib.util.spec_from_file_location('referenceFuzzer', root / 'tools/reference/fuzzRedstone.py')
fuzzer = importlib.util.module_from_spec(spec)
with patch.dict(sys.modules, {'captureRedstone': captureStub}):
    spec.loader.exec_module(fuzzer)


class ReferenceFuzzTests(unittest.TestCase):
    def testCheckerCannotReportMatchAfterFailure(self):
        response = subprocess.CompletedProcess([], 2, '{"status":"match"}', '')
        with patch.object(fuzzer.subprocess, 'run', return_value=response):
            with self.assertRaisesRegex(RuntimeError, 'contradicts'):
                fuzzer.check(Path('checker'), Path('capture.json'))

    def testUnknownStatusCannotPass(self):
        response = subprocess.CompletedProcess([], 0, '{"status":"pending"}', '')
        with patch.object(fuzzer.subprocess, 'run', return_value=response):
            with self.assertRaisesRegex(RuntimeError, 'Unknown'):
                fuzzer.check(Path('checker'), Path('capture.json'))

    def testChangedMismatchIsNotTheSameWitness(self):
        def result(expected):
            return {'firstDifference': {'relativePos': [3, 2, 3], 'tick': 0,
                                       'field': 'states', 'expected': expected, 'actual': 0}}
        self.assertNotEqual(fuzzer.differenceKey(result(1)), fuzzer.differenceKey(result(2)))

    def testFinalRecaptureMustStillFail(self):
        # Original and isolated runs differ; recapturing the candidate at a new origin matches.
        # Such a candidate must never be reported as a confirmed reduced witness.
        failure = {'status': 'difference', 'origin': [12, -58, 20],
                   'firstDifference': {'relativePos': [3, 2, 3], 'tick': 0,
                                       'field': 'states', 'expected': 1, 'actual': 0}}
        tempRoot = root / 'testResults/referenceFuzzTests'
        tempRoot.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=tempRoot) as temporary:
            output = Path(temporary) / 'run'
            def capture(commands, watch, endTick, path):
                path.write_text(json.dumps({'origin': [13, -58, 21], 'referenceEnvironment': {}}))
            args = ['fuzzRedstone.py', '--output', str(output), '--rounds', '2', '--shrinkBudget', '0']
            with patch.object(sys, 'argv', args), patch.object(fuzzer, 'GRID', 1), \
                 patch.object(fuzzer, 'buildCell', return_value=([{'tick': 0}], [[3,2,3]])), \
                 patch.object(fuzzer, 'capture', side_effect=capture), \
                 patch.object(fuzzer, 'check', side_effect=[failure, failure, {'status': 'match'}] * 2):
                self.assertEqual(fuzzer.main(), 1)
            report = json.loads((output / 'report.json').read_text())
            for row in report['results']:
                self.assertFalse(row['reducedWitnessConfirmed'])
                self.assertTrue(row['shrinkBudgetReached'])
            self.assertTrue((output / 'round0Reduction').is_dir())
            self.assertTrue((output / 'round1Reduction').is_dir())


if __name__ == '__main__':
    unittest.main()
