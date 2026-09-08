"""Restricted physical experiment; never substitutes for the HDL compiler's build."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

from physicalCells import PhysicalError, loadCell, placeCell

horizontal = {"north": (0, 0, -1), "east": (1, 0, 0),
              "south": (0, 0, 1), "west": (-1, 0, 0)}
opposite = {"north": "south", "south": "north", "east": "west", "west": "east"}
digital = {"kind": "digital", "low": [0, 0], "high": [1, 15]}


def require(condition, message):
    if not condition:
        code, _, detail = message.partition(" ")
        raise PhysicalError(code, detail)


def uniqueObject(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, f"EFormat duplicate key: {key}")
        result[key] = value
    return result


def readJson(path):
    try:
        return json.loads(Path(path).read_text(encoding="utf-8"), object_pairs_hook=uniqueObject,
                          parse_constant=lambda text: (_ for _ in ()).throw(PhysicalError("EFormat", text)))
    except (UnicodeError, json.JSONDecodeError) as error:
        raise PhysicalError("EFormat", f"{path}: {error}") from error


def canonical(value):
    return (json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2) + "\n").encode("utf-8")


def digest(data):
    return hashlib.sha256(data).hexdigest()


def vector(value):
    require(isinstance(value, list) and len(value) == 3 and
            all(type(n) is int and -29999984 <= n <= 29999984 for n in value), "EFormat coordinate")
    return tuple(value)


def inside(pos, box):
    return all(box["min"][i] <= pos[i] < box["max"][i] for i in range(3))


def add(pos, delta):
    return tuple(a + b for a, b in zip(pos, delta))


def staircase(source, sink, facing):
    """A finite explicit resource pattern, not a 3D search or general redstone rule."""
    require(facing in horizontal, "ECapability horizontal route only")
    direction = horizontal[facing]
    delta = tuple(b - a for a, b in zip(source, sink))
    length = delta[0] * direction[0] + delta[2] * direction[2]
    rise = delta[1]
    require(delta[0] == length * direction[0] and delta[2] == length * direction[2],
            "ERoute endpoints must share one horizontal axis")
    require(3 <= length <= 14, "ERoute supported span is 3..14 blocks; no implicit regeneration")
    require(0 <= rise <= length - 3, "ERoute only ascending stairs with level endpoint approaches")
    # First wire is level with source; last wire is level with sink.
    return [(source[0] + direction[0] * i, source[1] + min(i - 1, rise),
             source[2] + direction[2] * i) for i in range(1, length)]


def checkBuffer(cell):
    """Recognize only the two-block endpoint this checker was designed for."""
    blocks = {tuple(b["pos"]): b for b in cell["blocks"]}
    require(cell["manifest"]["bounds"] == {"min": [0, -1, 0], "max": [1, 1, 1]},
            "ECapability unexpected protected buffer envelope")
    require(cell["manifest"]["placementDomain"]["dimension"] == "minecraft:overworld",
            "ECapability experiment dimension must be overworld")
    require(set(blocks) == {(0, -1, 0), (0, 0, 0)}, "ECapability expected protected two-block buffer")
    require(blocks[(0, -1, 0)]["name"] == "minecraft:stone" and
            blocks[(0, -1, 0)]["properties"] == {}, "ECapability buffer support must be stone")
    require(blocks[(0, 0, 0)]["name"] == "minecraft:repeater" and
            blocks[(0, 0, 0)]["properties"] ==
            {"facing": "west", "delay": "1", "locked": "false", "powered": "false"},
            "ECapability unsupported buffer design state")
    expectedPins = [
        {"port": "dataIn", "leaf": [], "pos": [0, 0, 0], "face": "west",
         "channel": "receivePower", "encoding": digital, "maxLoads": None},
        {"port": "dataOut", "leaf": [], "pos": [0, 0, 0], "face": "east",
         "channel": "strongOutput", "encoding": digital, "maxLoads": 1}]
    require(sorted(cell["manifest"]["pins"], key=lambda p: p["port"]) == expectedPins,
            "ECapability buffer pin contract differs from checker")
    require(cell["initialization"]["steps"] ==
            [{"op": "place", "pos": [0, -1, 0]}, {"op": "place", "pos": [0, 0, 0]}],
            "ECapability buffer requires support-first ordered initialization")
    require(not cell["initialization"]["externalRequirements"], "ECapability unmet component environment")


def buildExperiment(requestPath, projectRoot):
    root = Path(projectRoot).resolve()
    requestPath = Path(requestPath).resolve()
    require(requestPath.is_relative_to(root), "EPath request outside project root")
    request = readJson(requestPath)
    require(isinstance(request, dict) and set(request) ==
            {"format", "formatVersion", "targetId", "bounds", "instances", "connection", "verification"},
            "EFormat experiment fields")
    require(request["format"] == "verimc.fixedExperiment" and type(request["formatVersion"]) is int and
            request["formatVersion"] == 1, "EFormat experiment version")
    require(request["targetId"] == "java26_2-experimental", "ECapability unsupported experiment target")
    bounds = request["bounds"]
    require(isinstance(bounds, dict) and set(bounds) == {"min", "max"}, "EFormat bounds")
    lower, upper = vector(bounds["min"]), vector(bounds["max"])
    require(all(a < b for a, b in zip(lower, upper)) and -64 <= lower[1] < upper[1] <= 320,
            "EPlacement illegal build bounds")
    instances = request["instances"]
    require(isinstance(instances, list) and len(instances) == 2, "ECapability exactly two fixed buffers")
    placed, assets, sourceMap = {}, {}, []
    blocks, owners, steps = {}, {}, []

    def put(block, owner):
        pos = vector(block["pos"])
        require(inside(pos, bounds), f"EPlacement out of bounds: {owner} {pos}")
        require(pos not in blocks, f"EPlacement occupied: {owner} {pos}")
        for item in placed.values():
            if item["instanceId"] != owner:
                require(not any(inside(pos, box) for box in item["keepouts"]),
                        f"EPlacement keepout: {owner} {pos}")
        blocks[pos], owners[pos] = block, owner

    assets[requestPath.relative_to(root).as_posix()] = digest(requestPath.read_bytes())
    for item in instances:
        require(isinstance(item, dict) and set(item) == {"id", "cell", "origin", "facing"}, "EFormat instance")
        name = item["id"]
        require(isinstance(name, str) and name and name not in placed, "EFormat duplicate/empty instance")
        require(isinstance(item["cell"], str) and not Path(item["cell"]).is_absolute() and
                ":" not in item["cell"], "EPath relative component path required")
        cellPath = (requestPath.parent / item["cell"]).resolve()
        cell = loadCell(cellPath, root, request["targetId"],
                        providedCapabilities={"orderedPlacement", "repeaterScheduledTicks"})
        checkBuffer(cell)
        result = placeCell(cell, name, list(vector(item["origin"])), item["facing"], worldYRange=(-64, 320))
        require(all(inside(p, bounds) for p in [result["bounds"]["min"],
                [n - 1 for n in result["bounds"]["max"]]]), "EPlacement component envelope outside bounds")
        for box in result["keepouts"]:
            require(inside(box["min"], bounds) and inside([n - 1 for n in box["max"]], bounds),
                    "EPlacement component keepout outside bounds")
        for oldPos in blocks:
            require(not any(inside(oldPos, box) for box in result["keepouts"]), "EPlacement overlapping keepout")
        placed[name] = result
        assets.update(cell["assets"])
        for block in result["blocks"]:
            put(block, name)
        steps.extend(dict(step, owner=name) for step in result["initialization"]["steps"])
        sourceMap.append({"instance": name, "source": cellPath.relative_to(root).as_posix(),
                          "sha256": digest(cellPath.read_bytes()), "start": 0,
                          "end": len(cellPath.read_bytes()), "line": 1, "column": 1,
                          "logicalResource": None, "blocks": [b["pos"] for b in result["blocks"]]})
    connection = request["connection"]
    require(isinstance(connection, dict) and set(connection) == {"from", "to", "checkerId"} and
            connection["checkerId"] == "straightAscendingDustV1", "ECapability unknown route checker")
    require(all(isinstance(connection[k], str) and connection[k] in placed for k in ("from", "to")) and
            connection["from"] != connection["to"], "ERoute invalid instances")
    source, sink = placed[connection["from"]], placed[connection["to"]]

    def pin(instance, port):
        return next(p for p in instance["pins"] if p["port"] == port)

    outputPin, inputPin = pin(source, "dataOut"), pin(sink, "dataIn")
    facing = outputPin["face"]
    require(inputPin["face"] == opposite[facing], "ERoute sink orientation incompatible")
    wires = staircase(vector(outputPin["pos"]), vector(inputPin["pos"]), facing)
    routeBlocks = []
    for pos in wires:
        routeBlocks.append({"pos": list(add(pos, (0, -1, 0))), "name": "minecraft:stone", "properties": {}})
    routeBlocks.extend({"pos": list(pos), "name": "minecraft:redstone_wire", "properties": {"power": "0"}}
                       for pos in wires)
    for block in routeBlocks:
        put(block, "$route")
        steps.append({"op": "place", "pos": block["pos"], "owner": "$route"})
    # The pattern owns the full one-block horizontal halo and two blocks above
    # the dust. Reject component air envelopes as well as actual obstructions.
    endpointOwners = {source["instanceId"], sink["instanceId"]}
    for pos in wires:
        for dx in (-1, 0, 1):
            for dz in (-1, 0, 1):
                for dy in (0, 1, 2):
                    probe = add(pos, (dx, dy, dz))
                    require(inside(probe, bounds), "ERoute isolation halo outside bounds")
                    for item in placed.values():
                        require(not any(inside(probe, box) for box in item["keepouts"]),
                                f"ERoute isolation halo overlaps keepout: {probe}")
                    if probe in blocks and owners[probe] in endpointOwners:
                        require(probe in (tuple(outputPin["pos"]), tuple(inputPin["pos"])),
                                f"ERoute foreign block in isolation halo: {probe}")
        require(add(pos, (0, 1, 0)) not in blocks, "ERoute staircase headroom blocked")
    sourceInput, sinkOutput = pin(source, "dataIn"), pin(sink, "dataOut")
    leverPos = add(vector(sourceInput["pos"]), horizontal[sourceInput["face"]])
    for block in [{"pos": list(add(leverPos, (0, -1, 0))), "name": "minecraft:stone", "properties": {}},
                  {"pos": list(leverPos), "name": "minecraft:lever",
                   "properties": {"face": "floor", "facing": facing, "powered": "false"}}]:
        put(block, "$inputAdapter")
        steps.append({"op": "place", "pos": block["pos"], "owner": "$inputAdapter"})
    verification = request["verification"]
    require(isinstance(verification, dict) and set(verification) == {"model", "steps"} and
            verification["model"] == "identityBit", "ECapability only explicit identityBit experiment model")
    require(isinstance(verification["steps"], list) and 1 <= len(verification["steps"]) <= 256,
            "EFormat verification requires 1..256 steps")
    for step in verification["steps"]:
        require(isinstance(step, dict) and set(step) == {"value", "waitGt"} and type(step["value"]) is bool
                and type(step["waitGt"]) is int and 1 <= step["waitGt"] <= 1000, "EFormat invalid input step")
    allPos = list(blocks)
    extent = [max(p[i] for p in allPos) - min(p[i] for p in allPos) + 1 for i in range(3)]
    for owner in ("$route", "$inputAdapter"):
        sourceMap.append({"instance": owner, "source": requestPath.relative_to(root).as_posix(),
                          "sha256": digest(requestPath.read_bytes()), "start": 0,
                          "end": len(requestPath.read_bytes()), "line": 1, "column": 1,
                          "logicalResource": None, "blocks": [list(p) for p in allPos if owners[p] == owner]})
    toolHashes = {name: digest(Path(__file__).with_name(name).read_bytes())
                  for name in ("physicalCells.py", "physicalClosure.py")}
    return {"format": "verimc.physicalExperiment", "formatVersion": 1,
            "targetId": request["targetId"], "status": "unverified",
            "circuitDesign": {"blocks": [blocks[p] for p in sorted(blocks)], "blockEntities": []},
            "interfaceMap": {"dataIn": sourceInput, "dataOut": sinkOutput,
                             "stimulus": {"adapterId": "floorLeverInteractV1", "pos": list(leverPos)}},
            "initialization": {"format": "verimc.initPlan", "formatVersion": 1,
                               "strategy": "orderedPlacement", "steps": steps,
                               "externalRequirements": [{"port": "dataIn", "purpose": "测试适配器按时间线操作拉杆"}]},
            "sourceMap": sourceMap, "verification": verification,
            "implementationReport": {"spatialCheck": "passRestrictedPattern", "simulation": "notRun",
                                     "minecraftReference": "notRun", "timingUpperBoundGt": None,
                                     "materials": len(blocks), "extent": extent,
                                     "volume": extent[0] * extent[1] * extent[2],
                                     "route": {"checkerId": connection["checkerId"], "wirePositions": wires,
                                               "wireCount": len(wires), "layerRise": wires[-1][1] - wires[0][1]},
                                     "limitations": ["仅固定双中继器与单根直线阶梯，无自动搜索",
                                                     "无 HDL 源码映射或组件模型等价证明",
                                                     "未验证其它环境方块、脉宽或同步采样"]},
            "lock": {"assets": assets, "tools": toolHashes, "gameBuildSha256": None,
                     "search": {"algorithm": "straightAscendingDustV1", "seed": 0, "candidateBudget": 1}}}


def verifyExperiment(packagePath, adapterPath, outputDir, projectRoot):
    package = readJson(packagePath)
    root = Path(projectRoot).resolve()
    for relative, expected in package["lock"]["assets"].items():
        source = (root / relative).resolve()
        require(source.is_relative_to(root) and source.is_file(), f"EPath missing or escaped locked asset: {relative}")
        require(digest(source.read_bytes()) == expected, f"ESourceChanged stale sourceMap/asset: {relative}")
    for name in ("physicalCells.py", "physicalClosure.py"):
        require(digest(Path(__file__).with_name(name).read_bytes()) == package["lock"]["tools"][name],
                f"EArtifactVersion rebuild package after tool change: {name}")
    result = subprocess.run([str(Path(adapterPath).resolve()), str(Path(packagePath).resolve())],
                            capture_output=True, text=True, encoding="utf-8", timeout=120)
    require(result.returncode == 0, f"ESimulation {result.stderr.strip()}")
    payload = json.loads(result.stdout, object_pairs_hook=uniqueObject)
    report = payload["report"]
    require(len(report["observations"]) == len(package["verification"]["steps"]), "ESimulation missing observations")
    report["logicalComparison"] = "pass" if all(
        row["input"] == step["value"] and row["output"] == step["value"]
        for row, step in zip(report["observations"], package["verification"]["steps"])) else "fail"
    report["model"] = "identityBit"
    report["packageSha256"] = digest(Path(packagePath).read_bytes())
    report["initializationSha256"] = digest(canonical(package["initialization"]))
    report["inputTimelineSha256"] = digest(canonical(package["verification"]["steps"]))
    report["observationsSha256"] = digest(canonical(report["observations"]))
    report["minecraftReference"] = "notRun"
    report["timingUpperBoundGt"] = None
    report["limitations"] = ["有限稳定观察点比较，不是脉冲/采样窗口完整验证", "未进行原版差分或组件全域表征"]
    output = Path(outputDir)
    output.mkdir(parents=True, exist_ok=True)
    (output / "simulationReport.json").write_bytes(canonical(report))
    (output / "circuitDesign.json").write_bytes(canonical(payload["project"]))
    require(report["logicalComparison"] == "pass", "ESimulation logical comparison failed; report retained")
    return report


def main():
    parser = argparse.ArgumentParser(description="受限物理闭环实验，非自动 HDL 物理编译器")
    commands = parser.add_subparsers(dest="command", required=True)
    build = commands.add_parser("build")
    build.add_argument("request"); build.add_argument("--root", required=True); build.add_argument("--output", required=True)
    verify = commands.add_parser("verify")
    verify.add_argument("package"); verify.add_argument("--adapter", required=True); verify.add_argument("--output-dir", required=True)
    verify.add_argument("--root", required=True)
    args = parser.parse_args()
    try:
        if args.command == "build":
            package = buildExperiment(args.request, args.root)
            output = Path(args.output)
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_bytes(canonical(package))
            print("已生成 unverified 实验设计包；仿真尚未运行")
        else:
            report = verifyExperiment(args.package, args.adapter, args.output_dir, args.root)
            print(f"C++ 仿真与 identityBit 比较通过：{len(report['observations'])} 个观察点；原版未验证")
    except (PhysicalError, OSError, ValueError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
