"""Exercise the real checker: an unobserved emitter must not turn a paused run into a match."""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

root = Path(__file__).resolve().parents[1]
checker = root / 'buildAudit/checkReference'


class ReferenceCheckerTests(unittest.TestCase):
    def testUnobservedExternalActionCannotPass(self):
        blocks = {b['name']: b for b in json.loads((root / 'data/blockStates.json').read_text())['blocks']}
        dropper = next(s['id'] for s in blocks['minecraft:dropper']['states']
                       if s['properties']['facing'] == 'east' and s['properties']['triggered'] == 'false')
        power = blocks['minecraft:redstone_block']['defaultState']
        fixture = {'origin': [0, 0, 0], 'watch': [[30, 0, 0]], 'endTick': 6,
                   'commands': [], 'frames': [{'tick': t, 'states': [0], 'analogs': [0]} for t in range(7)]}
        temporaryRoot = root / 'testResults/referenceCheckerTests'
        temporaryRoot.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=temporaryRoot) as directory:
            path = Path(directory) / 'pause.json'

            def run():
                path.write_text(json.dumps(fixture))
                result = subprocess.run([str(checker), str(path)], cwd=root,
                                        capture_output=True, text=True)
                return result.returncode, json.loads(result.stdout)

            code, report = run()
            self.assertEqual((code, report['status']), (0, 'match'), report)
            fixture['commands'] = [
                {'tick': 0, 'pos': [0, 0, 0], 'stateId': dropper},
                {'tick': 0, 'pos': [0, 0, 0], 'stimulus': {'inventory': [
                    {'slot': 0, 'item': 'minecraft:stone', 'count': 2}]}},
                {'tick': 0, 'pos': [-1, 0, 0], 'stateId': power},
            ]
            code, report = run()
            self.assertNotEqual(code, 0)
            self.assertEqual(report['status'], 'error')
            self.assertIn('Replay stopped', report['error'])


if __name__ == '__main__':
    unittest.main()
