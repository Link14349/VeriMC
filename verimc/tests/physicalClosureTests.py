import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

verimcRoot = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(verimcRoot / "tools"))
sys.path.insert(0, str(verimcRoot / "examples" / "physical"))
from physicalClosure import PhysicalError, buildExperiment, canonical, staircase, verifyExperiment
from createExample import createExample


class PhysicalClosureTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        createExample(self.root)
        self.requestPath = self.root / "ascending.fixed.json"
        self.request = json.loads(self.requestPath.read_text(encoding="utf-8"))

    def build(self):
        self.requestPath.write_bytes(canonical(self.request))
        return buildExperiment(self.requestPath, self.root)

    def changeCell(self, change):
        path = self.root / "buffer.vmcell.json"
        cell = json.loads(path.read_text(encoding="utf-8"))
        change(cell)
        path.write_bytes(canonical(cell))

    def testAscendingRealBlocks(self):
        package = self.build()
        self.assertEqual(package["status"], "unverified")
        self.assertEqual(len(package["circuitDesign"]["blocks"]), 16)
        self.assertEqual(package["implementationReport"]["route"]["layerRise"], 2)
        self.assertIsNone(package["lock"]["gameBuildSha256"])
        self.assertIsNone(package["implementationReport"]["timingUpperBoundGt"])

    def testDeterministic(self):
        self.assertEqual(canonical(self.build()), canonical(self.build()))

    def testSourceCoverageAndInitialization(self):
        package = self.build()
        positions = {tuple(b["pos"]) for b in package["circuitDesign"]["blocks"]}
        mapped = [tuple(p) for row in package["sourceMap"] for p in row["blocks"]]
        initialized = [tuple(row["pos"]) for row in package["initialization"]["steps"]]
        self.assertEqual(positions, set(mapped))
        self.assertEqual(len(mapped), len(positions))
        self.assertEqual(positions, set(initialized))
        self.assertEqual(len(initialized), len(positions))

    def testNegativeCoordinates(self):
        self.request["instances"][0]["origin"] = [-17, -4, -17]
        self.request["instances"][1]["origin"] = [-11, -2, -17]
        self.assertEqual(self.build()["interfaceMap"]["stimulus"]["pos"], [-18, -4, -17])

    def testFourRotations(self):
        for facing, sink in [("north", [6, 3, 0]), ("east", [0, 3, 6]),
                             ("south", [-6, 3, 0]), ("west", [0, 3, -6])]:
            with self.subTest(facing=facing):
                self.request["instances"][0]["facing"] = facing
                self.request["instances"][1].update(facing=facing, origin=sink)
                self.assertEqual(len(self.build()["circuitDesign"]["blocks"]), 16)

    def testMaxSpan(self):
        self.assertEqual(len(staircase((0, 0, 0), (14, 11, 0), "east")), 13)

    def testRejectLongRoute(self):
        self.request["instances"][1]["origin"] = [15, 3, 0]
        with self.assertRaisesRegex(PhysicalError, "ERoute"):
            self.build()

    def testRejectDescending(self):
        self.request["instances"][1]["origin"] = [6, 0, 0]
        with self.assertRaisesRegex(PhysicalError, "ERoute"):
            self.build()

    def testRejectSteepRoute(self):
        self.request["instances"][1]["origin"] = [6, 5, 0]
        with self.assertRaisesRegex(PhysicalError, "ERoute"):
            self.build()

    def testRejectMisaligned(self):
        self.request["instances"][1]["origin"] = [6, 3, 1]
        with self.assertRaisesRegex(PhysicalError, "ERoute"):
            self.build()

    def testRejectFacing(self):
        self.request["instances"][1]["facing"] = "south"
        with self.assertRaisesRegex(PhysicalError, "ERoute"):
            self.build()

    def testRejectCollision(self):
        self.request["instances"][1]["origin"] = [0, 1, 0]
        with self.assertRaisesRegex(PhysicalError, "EPlacement"):
            self.build()

    def testRejectKeepout(self):
        self.changeCell(lambda cell: cell.update(keepouts=[{"min": [1, -1, -1], "max": [3, 3, 2]}]))
        with self.assertRaisesRegex(PhysicalError, "EPlacement|ERoute|EFormat"):
            self.build()

    def testRejectStimulusOutOfBounds(self):
        self.request["bounds"]["min"][0] = 0
        with self.assertRaisesRegex(PhysicalError, "EPlacement"):
            self.build()

    def testRejectBoolCoordinate(self):
        self.request["instances"][0]["origin"][0] = True
        with self.assertRaisesRegex(PhysicalError, "EFormat"):
            self.build()

    def testRejectUnknownChecker(self):
        self.request["connection"]["checkerId"] = "routeAnything"
        with self.assertRaisesRegex(PhysicalError, "ECapability"):
            self.build()

    def testRejectExtraField(self):
        self.request["hiddenClock"] = True
        with self.assertRaisesRegex(PhysicalError, "EFormat"):
            self.build()

    def testRejectEmptyTimeline(self):
        self.request["verification"]["steps"] = []
        with self.assertRaisesRegex(PhysicalError, "EFormat"):
            self.build()

    def testRejectUnknownModel(self):
        self.request["verification"]["model"] = "magicAnd"
        with self.assertRaisesRegex(PhysicalError, "ECapability"):
            self.build()

    def testAssetHashChanges(self):
        first = self.build()["lock"]["assets"]
        self.changeCell(lambda cell: cell["annotations"].update(note="changed"))
        self.assertNotEqual(first, self.build()["lock"]["assets"])

    def testStaleSourceRejectedBeforeExecution(self):
        packagePath = self.root / "package.json"
        packagePath.write_bytes(canonical(self.build()))
        self.changeCell(lambda cell: cell["annotations"].update(note="changed"))
        with self.assertRaisesRegex(PhysicalError, "ESourceChanged"):
            verifyExperiment(packagePath, "mustNotExecute", self.root / "out", self.root)

    def testStaleToolRejectedBeforeExecution(self):
        package = self.build()
        package["lock"]["tools"]["physicalCells.py"] = "0" * 64
        packagePath = self.root / "package.json"
        packagePath.write_bytes(canonical(package))
        with self.assertRaisesRegex(PhysicalError, "EArtifactVersion"):
            verifyExperiment(packagePath, "mustNotExecute", self.root / "out", self.root)

    def testFloatManifestVersionRejected(self):
        self.changeCell(lambda cell: cell.update(formatVersion=1.0))
        with self.assertRaisesRegex(PhysicalError, "EArtifactVersion"):
            self.build()

    def testFloatInitVersionRejected(self):
        path = self.root / "bufferInit.json"
        plan = json.loads(path.read_text(encoding="utf-8"))
        plan["formatVersion"] = 1.0
        path.write_bytes(canonical(plan))
        with self.assertRaisesRegex(PhysicalError, "EArtifactVersion"):
            self.build()


if __name__ == "__main__":
    unittest.main()
