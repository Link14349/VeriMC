"""Recreate the committed, explicitly unverified fixed-buffer example assets."""
import json
from pathlib import Path


def createExample(directory):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    def write(name, value):
        (directory / name).write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    blocks = [{"pos": [0, -1, 0], "name": "minecraft:stone", "properties": {}},
              {"pos": [0, 0, 0], "name": "minecraft:repeater", "properties":
               {"delay": "1", "facing": "west", "locked": "false", "powered": "false"}}]
    write("bufferBlocks.json", blocks)
    write("bufferEntities.json", [])
    write("bufferInit.json", {"format": "verimc.initPlan", "formatVersion": 1, "strategy": "orderedPlacement",
                              "steps": [{"op": "place", "pos": b["pos"]} for b in blocks], "externalRequirements": []})
    write("bufferCharacterization.json", {
        "profileId": "java26_2-experimental", "conditions": ["只有显式组件与受限阶梯线路，无其它世界方块"],
        "caseIds": [], "limitations": ["尚无原版差分证据", "无时序上界及位置支持域等价证明"]})
    write("buffer.vmcell.json", {
        "format": "verimc.cell", "formatVersion": 1, "id": "experiment:buffer", "version": "0.1.0",
        "targetId": "java26_2-experimental",
        "ports": [{"name": "dataIn", "direction": "input", "type": "bit"},
                  {"name": "dataOut", "direction": "output", "type": "bit"}],
        "pins": [{"port": port, "leaf": [], "pos": [0, 0, 0], "face": face, "channel": channel,
                  "encoding": {"kind": "digital", "low": [0, 0], "high": [1, 15]}, "maxLoads": loads}
                 for port, face, channel, loads in [("dataIn", "west", "receivePower", None),
                                                   ("dataOut", "east", "strongOutput", 1)]],
        "blocks": "bufferBlocks.json", "blockEntities": "bufferEntities.json",
        "bounds": {"min": [0, -1, 0], "max": [1, 1, 1]}, "keepouts": [],
        "transforms": ["north", "east", "south", "west"],
        "placementDomain": {"originMin": [-128, -63, -128], "originMax": [129, 320, 129],
                            "chunkResidues": [[x, z] for x in range(16) for z in range(16)],
                            "dimension": "minecraft:overworld"},
        "initialization": "bufferInit.json", "model": None,
        "timing": {"arcs": [{"from": "dataIn", "to": "dataOut", "minGt": 0, "maxGt": None}],
                   "clockChecks": [], "settling": [{"output": "dataOut", "maxGt": None}],
                   "characterization": "bufferCharacterization.json"},
        "requiredCapabilities": ["orderedPlacement", "repeaterScheduledTicks"],
        "evidence": {"status": "unverified", "cases": []},
        "annotations": {"description": "仅实验候选。声明的位置集合供检查，不是已表征支持域。"}})
    write("ascending.fixed.json", {
        "format": "verimc.fixedExperiment", "formatVersion": 1, "targetId": "java26_2-experimental",
        "bounds": {"min": [-32, -16, -32], "max": [32, 32, 32]},
        "instances": [{"id": "lowBuffer", "cell": "buffer.vmcell.json", "origin": [0, 1, 0], "facing": "north"},
                      {"id": "highBuffer", "cell": "buffer.vmcell.json", "origin": [6, 3, 0], "facing": "north"}],
        "connection": {"from": "lowBuffer", "to": "highBuffer", "checkerId": "straightAscendingDustV1"},
        "verification": {"model": "identityBit", "steps": [{"value": value, "waitGt": 16}
                                                              for value in (False, True, False, True, True, False)]}})


if __name__ == "__main__":
    createExample(Path(__file__).parent)
