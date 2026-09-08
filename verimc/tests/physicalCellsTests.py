"""physicalCells 的独立单元测试：每个用例使用自己的临时项目根，不触碰仓库文件。

运行：python verimc/tests/physicalCellsTests.py
"""

import copy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import physicalCells
from physicalCells import PhysicalError, loadCell, placeCell


def wire(**overrides):
    properties = {"east": "none", "north": "none", "power": "0", "south": "side", "west": "none"}
    properties.update(overrides)
    return dict(sorted(properties.items()))


# 基准组件：2x2x2，覆盖全部五种受支持方块与三类属性旋转语义。
baseBlocks = [
    {"pos": [0, 0, 0], "name": "minecraft:stone", "properties": {}},
    {"pos": [0, 0, 1], "name": "minecraft:stone", "properties": {}},
    {"pos": [0, 1, 0], "name": "minecraft:redstone_wire", "properties": wire()},
    {"pos": [0, 1, 1], "name": "minecraft:repeater",
     "properties": {"delay": "1", "facing": "north", "locked": "false", "powered": "false"}},
    {"pos": [1, 0, 0], "name": "minecraft:stone", "properties": {}},
    {"pos": [1, 0, 1], "name": "minecraft:stone", "properties": {}},
    {"pos": [1, 1, 0], "name": "minecraft:lever",
     "properties": {"face": "floor", "facing": "east", "powered": "false"}},
    {"pos": [1, 1, 1], "name": "minecraft:redstone_lamp", "properties": {"lit": "false"}},
]

baseManifest = {
    "format": "verimc.cell",
    "formatVersion": 1,
    "id": "demo:buffer",
    "version": "0.1.0",
    "targetId": "java26_2",
    "ports": [
        {"name": "dataIn", "direction": "input", "type": "bit"},
        {"name": "dataOut", "direction": "output", "type": "bit"},
    ],
    "pins": [
        {"port": "dataIn", "leaf": [], "pos": [0, 1, 0], "face": "north",
         "channel": "receivePower",
         "encoding": {"kind": "digital", "low": [0, 0], "high": [1, 15]}, "maxLoads": None},
        {"port": "dataOut", "leaf": [], "pos": [0, 1, 1], "face": "south",
         "channel": "weakOutput",
         "encoding": {"kind": "digital", "low": [0, 0], "high": [1, 15]}, "maxLoads": 2},
    ],
    "blocks": "demoBlocks.json",
    "blockEntities": "demoBlockEntities.json",
    "bounds": {"min": [0, 0, 0], "max": [2, 2, 2]},
    "keepouts": [{"min": [0, 2, 0], "max": [2, 3, 2]}],
    "transforms": ["north", "east", "south", "west"],
    "placementDomain": {
        "originMin": [-32, 0, -32], "originMax": [32, 16, 32],
        "chunkResidues": [[0, 0], [4, 12]], "dimension": "minecraft:overworld",
    },
    "initialization": "demoInit.json",
    "model": None,
    "timing": {
        "arcs": [{"from": "dataIn", "to": "dataOut", "minGt": 2, "maxGt": None}],
        "clockChecks": [],
        "settling": [{"output": "dataOut", "maxGt": None}],
        "characterization": "demoCharacterization.json",
    },
    "requiredCapabilities": [],
    "evidence": {"status": "unverified", "cases": []},
    "annotations": {"note": "测试夹具，不代表已验证的红石实现"},
}

basePlan = {
    "format": "verimc.initPlan",
    "formatVersion": 1,
    "strategy": "orderedPlacement",
    "steps": [{"op": "place", "pos": list(block["pos"])} for block in baseBlocks]
             + [{"op": "wait", "ticks": 2}],
    "externalRequirements": [{"port": "dataIn", "purpose": "外部提供输入电平"}],
}

baseCharacterization = {
    "profileId": "demoProfile",
    "conditions": ["未在 Minecraft 26.2 参考构建上运行"],
    "caseIds": [],
    "limitations": ["本记录仅为测试夹具，不构成时序证据"],
}


class PhysicalCellsTestCase(unittest.TestCase):
    """提供临时项目根与夹具写入；每个用例独立目录，互不影响。"""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="verimcPhysical")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        (self.root / "cells").mkdir()

    def writeJson(self, relative, value):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        return path

    def writeCell(self, manifest=None, blocks=None, plan=None, entities=None,
                  characterization=None):
        self.writeJson("cells/demoBlocks.json",
                       baseBlocks if blocks is None else blocks)
        self.writeJson("cells/demoBlockEntities.json", [] if entities is None else entities)
        self.writeJson("cells/demoInit.json", copy.deepcopy(basePlan) if plan is None else plan)
        self.writeJson("cells/demoCharacterization.json",
                       baseCharacterization if characterization is None else characterization)
        return self.writeJson("cells/demo.vmcell.json",
                              copy.deepcopy(baseManifest) if manifest is None else manifest)

    def load(self, path=None, targetId="java26_2", **options):
        return loadCell(path or (self.root / "cells/demo.vmcell.json"), self.root, targetId,
                        **options)

    def loadMutated(self, mutate, **options):
        """按 mutate(manifest) 修改基准清单后加载。"""
        manifest = copy.deepcopy(baseManifest)
        mutate(manifest)
        self.writeCell(manifest=manifest)
        return self.load(**options)

    def expectError(self, code, action):
        with self.assertRaises(PhysicalError) as caught:
            action()
        self.assertEqual(caught.exception.code, code, str(caught.exception))
        self.assertTrue(str(caught.exception).startswith(code + ": "), str(caught.exception))
        return caught.exception


class LoadValidCellTests(PhysicalCellsTestCase):
    def testLoadsValidCell(self):
        self.writeCell()
        cell = self.load()
        self.assertEqual(set(cell), {"manifest", "blocks", "blockEntities", "initialization",
                                     "assets"})
        self.assertEqual(cell["manifest"]["id"], "demo:buffer")
        self.assertEqual(cell["blocks"], baseBlocks)
        self.assertEqual(cell["blockEntities"], [])
        self.assertEqual(cell["initialization"]["strategy"], "orderedPlacement")

    def testAssetsLockEveryReferencedFile(self):
        self.writeCell()
        cell = self.load()
        self.assertEqual(sorted(cell["assets"]), [
            "cells/demo.vmcell.json", "cells/demoBlockEntities.json", "cells/demoBlocks.json",
            "cells/demoCharacterization.json", "cells/demoInit.json",
        ])
        for relative, digest in cell["assets"].items():
            expected = hashlib.sha256((self.root / relative).read_bytes()).hexdigest()
            self.assertEqual(digest, expected)

    def testAssetsChangeWhenReferencedFileChanges(self):
        self.writeCell()
        before = self.load()["assets"]["cells/demoBlocks.json"]
        blocks = copy.deepcopy(baseBlocks)
        blocks[7]["properties"]["lit"] = "true"
        self.writeJson("cells/demoBlocks.json", blocks)
        self.assertNotEqual(self.load()["assets"]["cells/demoBlocks.json"], before)

    def testLoadIsDeterministic(self):
        self.writeCell()
        self.assertEqual(self.load(), self.load())

    def testNullModelIsPreserved(self):
        self.writeCell()
        self.assertIsNone(self.load()["manifest"]["model"])

    def testModelSourceIsLockedByHash(self):
        source = self.root / "cells/buffer.vmc"
        source.write_text('language "0.1";\npackage demo;\nmodule Buffer {}\n', encoding="utf-8")
        digest = hashlib.sha256(source.read_bytes()).hexdigest()
        cell = self.loadMutated(lambda manifest: manifest.update(model={
            "source": "buffer.vmc", "module": "Buffer",
            "parameters": {"width": 4, "inverted": False}, "sourceSha256": digest}))
        self.assertEqual(cell["assets"]["cells/buffer.vmc"], digest)

    def testModelHashMismatchRejected(self):
        source = self.root / "cells/buffer.vmc"
        source.write_text("module Buffer {}\n", encoding="utf-8")
        self.expectError("EFormat", lambda: self.loadMutated(lambda manifest: manifest.update(
            model={"source": "buffer.vmc", "module": "Buffer", "parameters": {},
                   "sourceSha256": "0" * 64})))

    def testModelParameterMustBeIntegerOrBoolean(self):
        source = self.root / "cells/buffer.vmc"
        source.write_text("module Buffer {}\n", encoding="utf-8")
        digest = hashlib.sha256(source.read_bytes()).hexdigest()
        self.expectError("EFormat", lambda: self.loadMutated(lambda manifest: manifest.update(
            model={"source": "buffer.vmc", "module": "Buffer", "parameters": {"width": "4"},
                   "sourceSha256": digest})))


class JsonStrictnessTests(PhysicalCellsTestCase):
    def testMalformedJsonRejected(self):
        self.writeCell()
        (self.root / "cells/demo.vmcell.json").write_text("{\"format\": ", encoding="utf-8")
        self.expectError("EFormat", self.load)

    def testDuplicateKeysRejected(self):
        self.writeCell()
        text = json.dumps(baseManifest, ensure_ascii=False)
        duplicated = "{" + '"targetId": "other", ' + text[1:]
        (self.root / "cells/demo.vmcell.json").write_text(duplicated, encoding="utf-8")
        error = self.expectError("EFormat", self.load)
        self.assertIn("重复 JSON 键", str(error))

    def testDuplicateKeysInReferencedFileRejected(self):
        self.writeCell()
        (self.root / "cells/demoInit.json").write_text(
            '{"format": "verimc.initPlan", "format": "verimc.initPlan"}', encoding="utf-8")
        self.expectError("EFormat", self.load)

    def testUnknownFieldRejected(self):
        self.expectError("EFormat", lambda: self.loadMutated(
            lambda manifest: manifest.update(routing="auto")))

    def testUnknownNestedFieldRejected(self):
        self.expectError("EFormat", lambda: self.loadMutated(
            lambda manifest: manifest["bounds"].update(center=[0, 0, 0])))

    def testAnnotationsAreAllowedAndOptional(self):
        self.writeCell()
        self.assertIn("annotations", self.load()["manifest"])
        cell = self.loadMutated(lambda manifest: manifest.pop("annotations"))
        self.assertNotIn("annotations", cell["manifest"])

    def testMissingRequiredFieldRejected(self):
        for field in ("format", "ports", "pins", "bounds", "model", "timing", "evidence"):
            with self.subTest(field=field):
                error = self.expectError("EFormat", lambda name=field: self.loadMutated(
                    lambda manifest: manifest.pop(name)))
                self.assertIn(field, str(error))

    def testBomRejected(self):
        self.writeCell()
        path = self.root / "cells/demo.vmcell.json"
        path.write_bytes(b"\xef\xbb\xbf" + path.read_bytes())
        self.expectError("EFormat", self.load)

    def testNanRejected(self):
        self.writeCell()
        (self.root / "cells/demoBlockEntities.json").write_text("[NaN]", encoding="utf-8")
        self.expectError("EFormat", self.load)

    def testNonUtf8Rejected(self):
        self.writeCell()
        (self.root / "cells/demoBlockEntities.json").write_bytes(b"[\xff\xfe]")
        self.expectError("EFormat", self.load)

    def testBooleanIsNotAnInteger(self):
        blocks = copy.deepcopy(baseBlocks)
        blocks[0]["pos"] = [False, 0, 0]
        self.writeCell(blocks=blocks)
        error = self.expectError("EFormat", self.load)
        self.assertIn("JSON 整数", str(error))

    def testFloatCoordinateRejected(self):
        blocks = copy.deepcopy(baseBlocks)
        blocks[0]["pos"] = [0.0, 0, 0]
        self.writeCell(blocks=blocks)
        self.expectError("EFormat", self.load)

    def testBooleanFormatVersionRejected(self):
        self.expectError("EArtifactVersion", lambda: self.loadMutated(
            lambda manifest: manifest.update(formatVersion=True)))

    def testUnsupportedFormatVersionRejected(self):
        self.expectError("EArtifactVersion", lambda: self.loadMutated(
            lambda manifest: manifest.update(formatVersion=2)))

    def testWrongFormatNameRejected(self):
        self.expectError("EFormat", lambda: self.loadMutated(
            lambda manifest: manifest.update(format="verimc.library")))


class PathSafetyTests(PhysicalCellsTestCase):
    def testParentEscapeRejected(self):
        outside = self.makeOutsideDirectory() / "blocks.json"
        reference = os.path.relpath(outside, self.root / "cells").replace("\\", "/")
        error = self.expectError("EPath", lambda: self.loadMutated(
            lambda manifest: manifest.update(blocks=reference)))
        self.assertIn("项目根", str(error))

    def testAbsoluteReferenceRejected(self):
        target = str((self.root / "cells/demoBlocks.json").as_posix())
        self.expectError("EPath", lambda: self.loadMutated(
            lambda manifest: manifest.update(blocks="/" + target.lstrip("/"))))

    def testDriveLetterReferenceRejected(self):
        self.expectError("EPath", lambda: self.loadMutated(
            lambda manifest: manifest.update(blocks="C:/cells/demoBlocks.json")))

    def testNetworkReferenceRejected(self):
        self.expectError("EPath", lambda: self.loadMutated(
            lambda manifest: manifest.update(blocks="https://example.invalid/blocks.json")))

    def testBackslashReferenceRejected(self):
        self.expectError("EPath", lambda: self.loadMutated(
            lambda manifest: manifest.update(blocks="cells\\demoBlocks.json")))

    def testMissingReferenceRejected(self):
        self.expectError("EResourceMissing", lambda: self.loadMutated(
            lambda manifest: manifest.update(blocks="demoBlocksMissing.json")))

    def testDirectoryReferenceRejected(self):
        self.expectError("EResourceMissing", lambda: self.loadMutated(
            lambda manifest: manifest.update(blocks=".")))

    def makeOutsideDirectory(self):
        outside = tempfile.TemporaryDirectory(prefix="verimcOutside")
        self.addCleanup(outside.cleanup)
        directory = Path(outside.name).resolve()
        (directory / "blocks.json").write_text("[]", encoding="utf-8")
        return directory

    def testSymlinkEscapeRejected(self):
        outside = self.makeOutsideDirectory()
        try:
            os.symlink(outside / "blocks.json", self.root / "cells/linkedBlocks.json")
        except (OSError, NotImplementedError, AttributeError) as error:
            self.skipTest(f"当前环境无法创建符号链接：{error}")
        self.expectError("EPath", lambda: self.loadMutated(
            lambda manifest: manifest.update(blocks="linkedBlocks.json")))

    def testDirectoryLinkEscapeRejected(self):
        """目录级链接（POSIX 符号链接或 Windows 目录联接）也不能绕开项目根。"""
        outside = self.makeOutsideDirectory()
        link = self.root / "cells/linkedDirectory"
        try:
            os.symlink(outside, link, target_is_directory=True)
        except (OSError, NotImplementedError, AttributeError):
            if os.name != "nt":
                self.skipTest("当前环境无法创建目录链接")
            result = subprocess.run(["cmd", "/c", "mklink", "/J", str(link), str(outside)],
                                    capture_output=True)
            if result.returncode != 0 or not link.exists():
                self.skipTest("当前环境无法创建目录联接")
        self.expectError("EPath", lambda: self.loadMutated(
            lambda manifest: manifest.update(blocks="linkedDirectory/blocks.json")))

    def testManifestOutsideRootRejected(self):
        self.writeCell()
        with tempfile.TemporaryDirectory(prefix="verimcOther") as other:
            self.expectError("EPath",
                             lambda: loadCell(self.root / "cells/demo.vmcell.json",
                                              Path(other).resolve(), "java26_2"))

    def testManifestSuffixEnforced(self):
        self.writeCell()
        renamed = self.root / "cells/demo.json"
        renamed.write_bytes((self.root / "cells/demo.vmcell.json").read_bytes())
        self.expectError("EFormat", lambda: self.load(path=renamed))

    def testProjectRootMustBeDirectory(self):
        self.writeCell()
        self.expectError("EPath", lambda: loadCell(self.root / "cells/demo.vmcell.json",
                                                   self.root / "cells/demo.vmcell.json",
                                                   "java26_2"))


class BlockValidationTests(PhysicalCellsTestCase):
    def testDuplicateCoordinateRejected(self):
        blocks = copy.deepcopy(baseBlocks)
        blocks[1]["pos"] = [0, 0, 0]
        self.writeCell(blocks=blocks)
        error = self.expectError("EFormat", self.load)
        self.assertIn("坐标重复", str(error))

    def testUnsortedBlocksRejected(self):
        blocks = copy.deepcopy(baseBlocks)
        blocks[0], blocks[1] = blocks[1], blocks[0]
        self.writeCell(blocks=blocks)
        error = self.expectError("EFormat", self.load)
        self.assertIn("字典序", str(error))

    def testUnsortedPropertyKeysRejected(self):
        blocks = copy.deepcopy(baseBlocks)
        blocks[3]["properties"] = {"facing": "north", "delay": "1", "locked": "false",
                                   "powered": "false"}
        self.writeCell(blocks=blocks)
        error = self.expectError("EFormat", self.load)
        self.assertIn("属性键未排序", str(error))

    def testBlockOutsideBoundsRejected(self):
        blocks = copy.deepcopy(baseBlocks) + [
            {"pos": [2, 0, 0], "name": "minecraft:stone", "properties": {}}]
        self.writeCell(blocks=blocks)
        error = self.expectError("EFormat", self.load)
        self.assertIn("bounds", str(error))

    def testUnsupportedBlockRejected(self):
        blocks = copy.deepcopy(baseBlocks)
        blocks[0]["name"] = "minecraft:sticky_piston"
        self.writeCell(blocks=blocks)
        self.expectError("ECapability", self.load)

    def testMalformedBlockNameRejected(self):
        blocks = copy.deepcopy(baseBlocks)
        blocks[0]["name"] = "Stone"
        self.writeCell(blocks=blocks)
        self.expectError("EFormat", self.load)

    def testMissingBlockPropertyRejected(self):
        blocks = copy.deepcopy(baseBlocks)
        blocks[3]["properties"].pop("locked")
        self.writeCell(blocks=blocks)
        error = self.expectError("EFormat", self.load)
        self.assertIn("缺少方块状态属性", str(error))

    def testNonStringPropertyValueRejected(self):
        blocks = copy.deepcopy(baseBlocks)
        blocks[3]["properties"]["locked"] = False
        self.writeCell(blocks=blocks)
        self.expectError("EFormat", self.load)

    def testEmptyBlockArrayRejected(self):
        self.writeCell(blocks=[])
        self.expectError("EFormat", self.load)

    def testKeepoutOverlappingOwnBlockRejected(self):
        self.expectError("EPlacement", lambda: self.loadMutated(
            lambda manifest: manifest.update(keepouts=[{"min": [0, 0, 0], "max": [1, 1, 1]}])))

    def testEmptyBoxRejected(self):
        self.expectError("EFormat", lambda: self.loadMutated(
            lambda manifest: manifest.update(bounds={"min": [0, 0, 0], "max": [2, 0, 2]})))

    def testNonEmptyBlockEntitiesRejected(self):
        self.writeCell(entities=[{"pos": [0, 0, 0], "dataVersion": 1, "data": {}}])
        error = self.expectError("ECapability", self.load)
        self.assertIn("NBT", str(error))


class PortAndPinTests(PhysicalCellsTestCase):
    def testUnsupportedPortTypeRejected(self):
        def mutate(manifest):
            manifest["ports"][0]["type"] = "uint<4>"
        self.expectError("ECapability", lambda: self.loadMutated(mutate))

    def testDuplicatePortNameRejected(self):
        def mutate(manifest):
            manifest["ports"][1]["name"] = "dataIn"
        self.expectError("EFormat", lambda: self.loadMutated(mutate))

    def testUncoveredPortLeafRejected(self):
        def mutate(manifest):
            manifest["pins"].pop()
        error = self.expectError("EFormat", lambda: self.loadMutated(mutate))
        self.assertIn("未覆盖端口叶子", str(error))

    def testDuplicatePinForSameLeafRejected(self):
        def mutate(manifest):
            duplicate = copy.deepcopy(manifest["pins"][1])
            duplicate["pos"] = [1, 1, 1]
            manifest["pins"].append(duplicate)
        error = self.expectError("EFormat", lambda: self.loadMutated(mutate))
        self.assertIn("端口叶子重复", str(error))

    def testNonEmptyLeafForBitPortRejected(self):
        def mutate(manifest):
            manifest["pins"][0]["leaf"] = [0]
        self.expectError("EFormat", lambda: self.loadMutated(mutate))

    def testInputChannelMustBeReceivePower(self):
        def mutate(manifest):
            manifest["pins"][0]["channel"] = "weakOutput"
        error = self.expectError("EFormat", lambda: self.loadMutated(mutate))
        self.assertIn("receivePower", str(error))

    def testOutputChannelMustBeWeakOrStrong(self):
        def mutate(manifest):
            manifest["pins"][1]["channel"] = "receivePower"
        self.expectError("EFormat", lambda: self.loadMutated(mutate))

    def testInputMaxLoadsMustBeNull(self):
        def mutate(manifest):
            manifest["pins"][0]["maxLoads"] = 0
        self.expectError("EFormat", lambda: self.loadMutated(mutate))

    def testOutputMaxLoadsMustBeNonNegativeInteger(self):
        for value in (None, -1, "2"):
            with self.subTest(value=value):
                self.expectError("EFormat", lambda item=value: self.loadMutated(
                    lambda manifest: manifest["pins"][1].update(maxLoads=item)))

    def testPinMustSitOnDeclaredBlock(self):
        def mutate(manifest):
            manifest["pins"][0]["pos"] = [1, 0, 1]
            manifest["pins"][0]["face"] = "up"
        self.loadMutated(mutate)  # 该位置有方块，允许

        def moveOutside(manifest):
            manifest["pins"][0]["pos"] = [5, 0, 0]
        self.expectError("EFormat", lambda: self.loadMutated(moveOutside))

    def testPinFaceMustBeSixDirections(self):
        def mutate(manifest):
            manifest["pins"][0]["face"] = "northeast"
        self.expectError("EFormat", lambda: self.loadMutated(mutate))

    def testDuplicatePinPositionAndFaceRejected(self):
        def mutate(manifest):
            manifest["pins"][1]["pos"] = [0, 1, 0]
            manifest["pins"][1]["face"] = "north"
        error = self.expectError("EFormat", lambda: self.loadMutated(mutate))
        self.assertIn("位置与面重复", str(error))

    def testStrengthEncodingRejectedAsCapability(self):
        # 规范允许 level 端口用 {kind: "strength"}，本工具没有该能力，按 ECapability 拒绝。
        def mutate(manifest):
            manifest["pins"][0]["encoding"] = {"kind": "strength"}
        error = self.expectError("ECapability", lambda: self.loadMutated(mutate))
        self.assertIn("strength", str(error))

    def testClockEncodingRejectedAsCapability(self):
        def mutate(manifest):
            manifest["pins"][0]["encoding"] = {"kind": "clock", "low": [0, 0], "high": [1, 15]}
        self.expectError("ECapability", lambda: self.loadMutated(mutate))

    def testUnknownEncodingKindRejected(self):
        def mutate(manifest):
            manifest["pins"][0]["encoding"]["kind"] = "analog"
        self.expectError("EFormat", lambda: self.loadMutated(mutate))

    def testOverlappingEncodingRangesRejected(self):
        def mutate(manifest):
            manifest["pins"][0]["encoding"] = {"kind": "digital", "low": [0, 5],
                                               "high": [4, 15]}
        error = self.expectError("EFormat", lambda: self.loadMutated(mutate))
        self.assertIn("重叠", str(error))

    def testEncodingRangeOutsideSignalLimitsRejected(self):
        def mutate(manifest):
            manifest["pins"][0]["encoding"]["high"] = [1, 16]
        self.expectError("EFormat", lambda: self.loadMutated(mutate))


class TargetAndCapabilityTests(PhysicalCellsTestCase):
    def testTargetMismatchRejected(self):
        self.writeCell()
        self.expectError("ETarget", lambda: self.load(targetId="java26_1"))

    def testRequiredCapabilityWithoutProviderRejected(self):
        self.expectError("ECapability", lambda: self.loadMutated(
            lambda manifest: manifest.update(requiredCapabilities=["redstone.repeaterLock"])))

    def testRequiredCapabilityAcceptedWhenProvided(self):
        cell = self.loadMutated(
            lambda manifest: manifest.update(requiredCapabilities=["redstone.repeaterLock"]),
            providedCapabilities={"redstone.repeaterLock", "redstone.wirePower"})
        self.assertEqual(cell["manifest"]["requiredCapabilities"], ["redstone.repeaterLock"])

    def testMirrorTransformRejected(self):
        self.expectError("ECapability", lambda: self.loadMutated(
            lambda manifest: manifest.update(transforms=["north", "up"])))

    def testDuplicateTransformRejected(self):
        self.expectError("EFormat", lambda: self.loadMutated(
            lambda manifest: manifest.update(transforms=["north", "north"])))


class EvidenceAndTimingTests(PhysicalCellsTestCase):
    def testScenarioVerifiedEvidenceRejected(self):
        error = self.expectError("ECapability", lambda: self.loadMutated(
            lambda manifest: manifest.update(
                evidence={"status": "scenarioVerified", "cases": ["cases/one.json"]})))
        self.assertIn("无法核验", str(error))

    def testUnverifiedEvidenceMustHaveEmptyCases(self):
        self.expectError("EFormat", lambda: self.loadMutated(
            lambda manifest: manifest.update(
                evidence={"status": "unverified", "cases": ["cases/one.json"]})))

    def testUnknownEvidenceStatusRejected(self):
        self.expectError("EFormat", lambda: self.loadMutated(
            lambda manifest: manifest.update(evidence={"status": "verified", "cases": []})))

    def testClockChecksRejected(self):
        def mutate(manifest):
            manifest["timing"]["clockChecks"] = [{"clock": "clk", "data": ["dataIn"],
                                                  "setupGt": 1, "holdGt": 1, "minHighGt": 1,
                                                  "minLowGt": 1}]
        self.expectError("ECapability", lambda: self.loadMutated(mutate))

    def testNullSettlingBoundIsAcceptedAsUnknown(self):
        self.writeCell()
        self.assertIsNone(self.load()["manifest"]["timing"]["settling"][0]["maxGt"])

    def testSettlingMustCoverEveryOutput(self):
        def mutate(manifest):
            manifest["timing"]["settling"] = []
        error = self.expectError("EFormat", lambda: self.loadMutated(mutate))
        self.assertIn("dataOut", str(error))

    def testArcEndpointsMustMatchPortDirections(self):
        def mutate(manifest):
            manifest["timing"]["arcs"][0]["from"] = "dataOut"
        self.expectError("EFormat", lambda: self.loadMutated(mutate))

    def testArcMaxBelowMinRejected(self):
        def mutate(manifest):
            manifest["timing"]["arcs"][0]["maxGt"] = 1
        self.expectError("EFormat", lambda: self.loadMutated(mutate))

    def testCharacterizationRequiredWhenBoundsDeclared(self):
        def mutate(manifest):
            manifest["timing"]["characterization"] = None
        self.expectError("EFormat", lambda: self.loadMutated(mutate))

    def testCharacterizationOptionalWithoutBounds(self):
        def mutate(manifest):
            manifest["timing"]["arcs"] = []
            manifest["timing"]["characterization"] = None
        cell = self.loadMutated(mutate)
        self.assertIsNone(cell["manifest"]["timing"]["characterization"])

    def testCharacterizationMissingFieldRejected(self):
        record = dict(baseCharacterization)
        record.pop("limitations")
        self.writeCell(characterization=record)
        self.expectError("EFormat", self.load)

    def testCharacterizationCaseIdsWithoutEvidenceRejected(self):
        record = dict(baseCharacterization, caseIds=["caseA"])
        self.writeCell(characterization=record)
        self.expectError("EFormat", self.load)


class InitializationTests(PhysicalCellsTestCase):
    def testMissingPlaceStepRejected(self):
        plan = copy.deepcopy(basePlan)
        plan["steps"].pop(0)
        self.writeCell(plan=plan)
        error = self.expectError("EInitialization", self.load)
        self.assertIn("未覆盖", str(error))

    def testDuplicatePlaceStepRejected(self):
        plan = copy.deepcopy(basePlan)
        plan["steps"].insert(1, {"op": "place", "pos": [0, 0, 0]})
        self.writeCell(plan=plan)
        error = self.expectError("EInitialization", self.load)
        self.assertIn("重复放置", str(error))

    def testPlaceStepForUnknownBlockRejected(self):
        plan = copy.deepcopy(basePlan)
        plan["steps"][0]["pos"] = [7, 7, 7]
        self.writeCell(plan=plan)
        self.expectError("EInitialization", self.load)

    def testBulkThenUpdatesRejected(self):
        plan = copy.deepcopy(basePlan)
        plan["strategy"] = "bulkThenUpdates"
        plan["steps"] = [{"op": "wait", "ticks": 1}]
        self.writeCell(plan=plan)
        self.expectError("ECapability", self.load)

    def testUnknownStrategyRejected(self):
        plan = copy.deepcopy(basePlan)
        plan["strategy"] = "instantPaste"
        self.writeCell(plan=plan)
        self.expectError("EFormat", self.load)

    def testUpdateStepRejectedWithoutDeclaredKinds(self):
        plan = copy.deepcopy(basePlan)
        plan["steps"].append({"op": "update", "pos": [0, 1, 0], "kind": "neighborChanged"})
        self.writeCell(plan=plan)
        error = self.expectError("ECapability", self.load)
        self.assertIn("neighborChanged", str(error))

    def testUpdateStepAcceptedWhenKindDeclared(self):
        plan = copy.deepcopy(basePlan)
        plan["steps"].append({"op": "update", "pos": [0, 1, 0], "kind": "neighborChanged"})
        self.writeCell(plan=plan)
        cell = self.load(supportedUpdateKinds={"neighborChanged"})
        self.assertEqual(cell["initialization"]["steps"][-1]["kind"], "neighborChanged")

    def testUnknownOperationRejected(self):
        plan = copy.deepcopy(basePlan)
        plan["steps"].append({"op": "runScript", "path": "setup.py"})
        self.writeCell(plan=plan)
        self.expectError("EFormat", self.load)

    def testNegativeWaitRejected(self):
        plan = copy.deepcopy(basePlan)
        plan["steps"].append({"op": "wait", "ticks": -1})
        self.writeCell(plan=plan)
        self.expectError("EFormat", self.load)

    def testExternalRequirementPortMustExist(self):
        plan = copy.deepcopy(basePlan)
        plan["externalRequirements"][0]["port"] = "clk"
        self.writeCell(plan=plan)
        self.expectError("EFormat", self.load)

    def testInitPlanVersionEnforced(self):
        plan = copy.deepcopy(basePlan)
        plan["formatVersion"] = 2
        self.writeCell(plan=plan)
        self.expectError("EArtifactVersion", self.load)


class PlacementDomainTests(PhysicalCellsTestCase):
    def testResidueOutsideChunkRangeRejected(self):
        self.expectError("EFormat", lambda: self.loadMutated(
            lambda manifest: manifest["placementDomain"].update(chunkResidues=[[0, 16]])))

    def testNegativeResidueRejected(self):
        self.expectError("EFormat", lambda: self.loadMutated(
            lambda manifest: manifest["placementDomain"].update(chunkResidues=[[0, -1]])))

    def testDuplicateResidueRejected(self):
        self.expectError("EFormat", lambda: self.loadMutated(
            lambda manifest: manifest["placementDomain"].update(
                chunkResidues=[[0, 0], [0, 0]])))

    def testReversedOriginRangeRejected(self):
        self.expectError("EFormat", lambda: self.loadMutated(
            lambda manifest: manifest["placementDomain"].update(originMax=[-64, 16, 32])))

    def testMalformedDimensionRejected(self):
        self.expectError("EFormat", lambda: self.loadMutated(
            lambda manifest: manifest["placementDomain"].update(dimension="overworld")))

    def testFloorModIsNonNegative(self):
        self.assertEqual(physicalCells.floorMod(-1, 16), 15)
        self.assertEqual(physicalCells.floorMod(-16, 16), 0)
        self.assertEqual(physicalCells.floorMod(-17, 16), 15)
        self.assertEqual(physicalCells.floorMod(0, 16), 0)
        self.assertEqual(physicalCells.floorMod(20, 16), 4)

    def testNegativeOriginUsesFloorResidue(self):
        self.writeCell()
        cell = self.load()
        result = placeCell(cell, "top.buffer", [-16, 0, -16], "north")
        self.assertEqual(result["blocks"][0]["pos"], [-16, 0, -16])
        result = placeCell(cell, "top.buffer", [-12, 0, -4], "north")
        self.assertEqual(result["blocks"][0]["pos"], [-12, 0, -4])

    def testOriginWithWrongResidueRejected(self):
        self.writeCell()
        cell = self.load()
        error = self.expectError("EPlacement",
                                 lambda: placeCell(cell, "top.buffer", [-16, 0, -15], "north"))
        self.assertIn("区块余数", str(error))

    def testOriginOutsideHalfOpenRangeRejected(self):
        self.writeCell()
        cell = self.load()
        self.expectError("EPlacement", lambda: placeCell(cell, "top.buffer", [32, 0, 0], "north"))
        self.expectError("EPlacement", lambda: placeCell(cell, "top.buffer", [-48, 0, 0], "north"))

    def testEmptyResidueSetMeansNoPosition(self):
        cell = self.loadMutated(
            lambda manifest: manifest["placementDomain"].update(chunkResidues=[]))
        self.expectError("EPlacement", lambda: placeCell(cell, "top.buffer", [0, 0, 0], "north"))


class PlacementTransformTests(PhysicalCellsTestCase):
    def setUp(self):
        super().setUp()
        self.writeCell()
        self.cell = self.load()

    def place(self, facing, origin=(0, 0, 0), **options):
        return placeCell(self.cell, "top.buffer", list(origin), facing, **options)

    def blockAt(self, result, position):
        for entry in result["blocks"]:
            if entry["pos"] == list(position):
                return entry
        self.fail(f"变换结果缺少方块 {list(position)}")

    def testNorthIsIdentityPlusOrigin(self):
        result = self.place("north", (16, 4, 16))
        self.assertEqual(result["instanceId"], "top.buffer")
        self.assertEqual(result["bounds"], {"min": [16, 4, 16], "max": [18, 6, 18]})
        self.assertEqual([entry["pos"] for entry in result["blocks"]],
                         [[16, 4, 16], [16, 4, 17], [16, 5, 16], [16, 5, 17],
                          [17, 4, 16], [17, 4, 17], [17, 5, 16], [17, 5, 17]])
        self.assertEqual(self.blockAt(result, [16, 5, 17])["properties"]["facing"], "north")

    def testEastRotatesCoordinates(self):
        result = self.place("east")
        positions = sorted(tuple(entry["pos"]) for entry in result["blocks"])
        self.assertEqual(positions, [(-1, 0, 0), (-1, 0, 1), (-1, 1, 0), (-1, 1, 1),
                                     (0, 0, 0), (0, 0, 1), (0, 1, 0), (0, 1, 1)])

    def testHalfOpenBoundsUseOccupiedCells(self):
        # 直接旋转 max 点会得到 [-2,0,0)..[0,2,2)，把 x=0 的方块排除在外。
        self.assertEqual(self.place("east")["bounds"], {"min": [-1, 0, 0], "max": [1, 2, 2]})
        self.assertEqual(self.place("south")["bounds"], {"min": [-1, 0, -1], "max": [1, 2, 1]})
        self.assertEqual(self.place("west")["bounds"], {"min": [0, 0, -1], "max": [2, 2, 1]})

    def testTransformedBlocksStayInsideTransformedBounds(self):
        for facing in ("north", "east", "south", "west"):
            with self.subTest(facing=facing):
                result = self.place(facing)
                low, high = result["bounds"]["min"], result["bounds"]["max"]
                for entry in result["blocks"]:
                    for axis in range(3):
                        self.assertTrue(low[axis] <= entry["pos"][axis] < high[axis])

    def testKeepoutIsTransformedToo(self):
        self.assertEqual(self.place("north")["keepouts"],
                         [{"min": [0, 2, 0], "max": [2, 3, 2]}])
        self.assertEqual(self.place("east")["keepouts"],
                         [{"min": [-1, 2, 0], "max": [1, 3, 2]}])

    def testFacingPropertyRotates(self):
        expected = {"north": ("north", "east"), "east": ("east", "south"),
                    "south": ("south", "west"), "west": ("west", "north")}
        for facing, (repeater, lever) in expected.items():
            with self.subTest(facing=facing):
                result = self.place(facing)
                repeaterPos = physicalCells.transformBlockPosition((0, 1, 1), facing, (0, 0, 0))
                leverPos = physicalCells.transformBlockPosition((1, 1, 0), facing, (0, 0, 0))
                self.assertEqual(self.blockAt(result, repeaterPos)["properties"]["facing"],
                                 repeater)
                self.assertEqual(self.blockAt(result, leverPos)["properties"]["facing"], lever)

    def testWireDirectionPropertiesAreRenamed(self):
        for facing, side in (("north", "south"), ("east", "west"), ("south", "north"),
                             ("west", "east")):
            with self.subTest(facing=facing):
                result = self.place(facing)
                position = physicalCells.transformBlockPosition((0, 1, 0), facing, (0, 0, 0))
                properties = self.blockAt(result, position)["properties"]
                self.assertEqual(properties[side], "side")
                self.assertEqual([key for key in properties if properties[key] == "side"], [side])
                self.assertEqual(properties["power"], "0")
                self.assertEqual(list(properties), sorted(properties))

    def testVerticalAndScalarPropertiesDoNotRotate(self):
        result = self.place("east")
        leverPos = physicalCells.transformBlockPosition((1, 1, 0), "east", (0, 0, 0))
        self.assertEqual(self.blockAt(result, leverPos)["properties"]["face"], "floor")
        lampPos = physicalCells.transformBlockPosition((1, 1, 1), "east", (0, 0, 0))
        self.assertEqual(self.blockAt(result, lampPos)["properties"]["lit"], "false")
        repeaterPos = physicalCells.transformBlockPosition((0, 1, 1), "east", (0, 0, 0))
        self.assertEqual(self.blockAt(result, repeaterPos)["properties"]["delay"], "1")

    def testPinPositionsAndFacesRotate(self):
        result = self.place("east", (16, 0, 16))
        pins = {entry["port"]: entry for entry in result["pins"]}
        self.assertEqual(pins["dataIn"]["pos"], [16, 1, 16])
        self.assertEqual(pins["dataIn"]["face"], "east")
        self.assertEqual(pins["dataOut"]["pos"], [15, 1, 16])
        self.assertEqual(pins["dataOut"]["face"], "west")
        self.assertEqual(pins["dataIn"]["maxLoads"], None)
        self.assertEqual(pins["dataOut"]["encoding"],
                         {"kind": "digital", "low": [0, 0], "high": [1, 15]})

    def testVerticalPinFaceIsUnchanged(self):
        manifest = copy.deepcopy(baseManifest)
        manifest["pins"][0]["face"] = "up"
        self.writeCell(manifest=manifest)
        result = placeCell(self.load(), "top.buffer", [0, 0, 0], "west")
        self.assertEqual(
            [entry["face"] for entry in result["pins"] if entry["port"] == "dataIn"], ["up"])

    def testInitializationCoordinatesAreTransformed(self):
        result = self.place("east", (16, 0, 16))
        steps = result["initialization"]["steps"]
        self.assertEqual(steps[0]["pos"], [16, 0, 16])
        self.assertEqual(steps[1]["pos"], [15, 0, 16])
        self.assertEqual(steps[-1], {"op": "wait", "ticks": 2})
        self.assertEqual(result["initialization"]["instanceId"], "top.buffer")
        self.assertEqual(result["initialization"]["externalRequirements"],
                         [{"port": "dataIn", "purpose": "外部提供输入电平"}])
        self.assertEqual(sorted(tuple(step["pos"]) for step in steps if step["op"] == "place"),
                         sorted(tuple(entry["pos"]) for entry in result["blocks"]))

    def testPlacementDoesNotMutateLoadedCell(self):
        before = copy.deepcopy(self.cell)
        self.place("east", (16, 0, 16))
        self.assertEqual(self.cell, before)

    def testFacingOutsideTransformsRejected(self):
        manifest = copy.deepcopy(baseManifest)
        manifest["transforms"] = ["north"]
        self.writeCell(manifest=manifest)
        cell = self.load()
        self.assertTrue(placeCell(cell, "top.buffer", [0, 0, 0], "north"))
        self.expectError("EPlacement", lambda: placeCell(cell, "top.buffer", [0, 0, 0], "east"))

    def testInvalidFacingRejected(self):
        self.expectError("EFormat", lambda: self.place("up"))

    def testInvalidOriginRejected(self):
        self.expectError("EFormat", lambda: self.place("north", (0, 0)))
        self.expectError("EFormat", lambda: placeCell(self.cell, "top.buffer", "abc", "north"))
        self.expectError("EFormat", lambda: placeCell(self.cell, "top.buffer",
                                                      [0, True, 0], "north"))

    def testInvalidInstanceIdRejected(self):
        self.expectError("EFormat", lambda: placeCell(self.cell, "", [0, 0, 0], "north"))
        self.expectError("EFormat", lambda: placeCell(self.cell, "top buffer", [0, 0, 0],
                                                      "north"))

    def testNonCellArgumentRejected(self):
        self.expectError("EFormat", lambda: placeCell({"blocks": []}, "x", [0, 0, 0], "north"))

    def testWorldYRangeIsCheckedWhenGiven(self):
        self.assertTrue(self.place("north", (0, 0, 0), worldYRange=(0, 3)))
        error = self.expectError("EPlacement",
                                 lambda: self.place("north", (0, 0, 0), worldYRange=(0, 2)))
        self.assertIn("y 区间", str(error))
        self.expectError("EPlacement",
                         lambda: self.place("north", (0, 15, 0), worldYRange=(0, 16)))

    def testWorldYRangeIsOptional(self):
        self.assertTrue(self.place("north", (0, 15, 0)))


class UnknownPropertyTests(PhysicalCellsTestCase):
    def blocksWithUnknownProperty(self):
        blocks = copy.deepcopy(baseBlocks)
        blocks[7]["properties"] = {"lit": "false", "waterlogged": "false"}
        return blocks

    def testUnknownPropertyIsNotSilentlyDropped(self):
        self.writeCell(blocks=self.blocksWithUnknownProperty())
        cell = self.load()
        result = placeCell(cell, "top.buffer", [0, 0, 0], "north")
        lamp = [entry for entry in result["blocks"] if entry["name"] == "minecraft:redstone_lamp"]
        self.assertEqual(lamp[0]["properties"], {"lit": "false", "waterlogged": "false"})

    def testUnknownPropertyBlocksRotation(self):
        self.writeCell(blocks=self.blocksWithUnknownProperty())
        cell = self.load()
        error = self.expectError("ECapability",
                                 lambda: placeCell(cell, "top.buffer", [0, 0, 0], "east"))
        self.assertIn("waterlogged", str(error))

    def testUnknownValueOfKnownPropertyBlocksRotation(self):
        blocks = copy.deepcopy(baseBlocks)
        blocks[3]["properties"]["facing"] = "up"
        self.writeCell(blocks=blocks)
        cell = self.load()
        self.assertTrue(placeCell(cell, "top.buffer", [0, 0, 0], "north"))
        self.expectError("ECapability",
                         lambda: placeCell(cell, "top.buffer", [0, 0, 0], "south"))


class RotationMathTests(unittest.TestCase):
    def testTransformPointMatchesSpec(self):
        self.assertEqual(physicalCells.transformPoint((1, 2, 3), "north"), (1, 2, 3))
        self.assertEqual(physicalCells.transformPoint((1, 2, 3), "east"), (-3, 2, 1))
        self.assertEqual(physicalCells.transformPoint((1, 2, 3), "south"), (-1, 2, -3))
        self.assertEqual(physicalCells.transformPoint((1, 2, 3), "west"), (3, 2, -1))

    def testFourRotationsReturnToIdentity(self):
        point = (5, -2, 7)
        current = point
        for _ in range(4):
            current = physicalCells.transformPoint(current, "east")
        self.assertEqual(current, point)

    def testDirectionRotationIsConsistentWithCoordinates(self):
        vectors = {"north": (0, 0, -1), "east": (1, 0, 0), "south": (0, 0, 1),
                   "west": (-1, 0, 0)}
        for facing in physicalCells.horizontalDirections:
            for direction, vector in vectors.items():
                rotated = physicalCells.rotateDirection(direction, facing)
                self.assertEqual(physicalCells.transformPoint(vector, facing), vectors[rotated])

    def testVerticalDirectionsDoNotRotate(self):
        for facing in physicalCells.horizontalDirections:
            self.assertEqual(physicalCells.rotateDirection("up", facing), "up")
            self.assertEqual(physicalCells.rotateDirection("down", facing), "down")

    def testTransformBoxKeepsCellCount(self):
        box = ((0, 0, 0), (3, 1, 5))
        for facing in physicalCells.horizontalDirections:
            low, high = physicalCells.transformBox(box, facing, (10, 0, -20))
            volume = 1
            for axis in range(3):
                volume *= high[axis] - low[axis]
            self.assertEqual(volume, 3 * 1 * 5)

    def testTransformBoxEqualsImageOfOccupiedCells(self):
        """半开盒的变换结果必须恰好等于原占用格集合的像，不多也不少。"""
        boxes = [((0, 0, 0), (1, 1, 1)), ((0, 0, 0), (3, 2, 5)), ((-4, -1, 2), (1, 3, 3)),
                 ((-7, 60, -9), (-2, 61, -1)), ((2, 0, -6), (9, 4, -5))]
        for box in boxes:
            for facing in physicalCells.horizontalDirections:
                for origin in ((0, 0, 0), (16, 4, -32)):
                    with self.subTest(box=box, facing=facing, origin=origin):
                        cells = {physicalCells.transformBlockPosition((x, y, z), facing, origin)
                                 for x in range(box[0][0], box[1][0])
                                 for y in range(box[0][1], box[1][1])
                                 for z in range(box[0][2], box[1][2])}
                        low, high = physicalCells.transformBox(box, facing, origin)
                        expected = {(x, y, z)
                                    for x in range(low[0], high[0])
                                    for y in range(low[1], high[1])
                                    for z in range(low[2], high[2])}
                        self.assertEqual(cells, expected)

    def testRotatedBlockSetIsABijection(self):
        source = {(x, y, z) for x in range(-2, 3) for y in range(0, 2) for z in range(-1, 4)}
        for facing in physicalCells.horizontalDirections:
            images = [physicalCells.transformBlockPosition(item, facing, (5, 0, -7))
                      for item in source]
            self.assertEqual(len(set(images)), len(source))


if __name__ == "__main__":
    unittest.main(verbosity=2)
