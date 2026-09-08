"""Run generated real blocks against the native C++ core (no mock kernel)."""
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile

verimcRoot = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(verimcRoot / "tools"))
sys.path.insert(0, str(verimcRoot / "examples" / "physical"))
from physicalClosure import buildExperiment, canonical, verifyExperiment
from createExample import createExample


def run(adapter):
    count = 0
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        createExample(root)
        requestPath = root / "ascending.fixed.json"
        base = json.loads(requestPath.read_text(encoding="utf-8"))
        for facing, delta in [("north", (6, 2, 0)), ("east", (0, 2, 6)),
                              ("south", (-6, 2, 0)), ("west", (0, 2, -6))]:
            for origin in [(0, 1, 0), (-17, -4, -17)]:
                request = copy.deepcopy(base)
                request["instances"][0].update(facing=facing, origin=list(origin))
                request["instances"][1].update(facing=facing, origin=[a + b for a, b in zip(origin, delta)])
                requestPath.write_bytes(canonical(request))
                package = buildExperiment(requestPath, root)
                packagePath = root / "package.json"
                packagePath.write_bytes(canonical(package))
                report = verifyExperiment(packagePath, adapter, root / "results", root)
                assert report["logicalComparison"] == "pass"
                # Initial zero plus four actual transitions; repeated high must
                # not create another edge. Compare both edge values and timings.
                assert [e["value"] > 0 for e in report["edges"]] == [False, True, False, True, False], report
                assert [e["tick"] for e in report["edges"]] == [0, 20, 36, 52, 84], report
                assert report["blockCount"] == 16
                count += 1
        # The checker declares a finite span/rise range. Exercise every such
        # geometry, not just the visually convenient demonstration staircase.
        for span in range(3, 15):
            for rise in range(span - 2):
                request = copy.deepcopy(base)
                request["instances"][1]["origin"] = [span, 1 + rise, 0]
                requestPath.write_bytes(canonical(request))
                packagePath.write_bytes(canonical(buildExperiment(requestPath, root)))
                report = verifyExperiment(packagePath, adapter, root / "results", root)
                assert report["logicalComparison"] == "pass", (span, rise)
                assert [e["tick"] for e in report["edges"]] == [0, 20, 36, 52, 84], (span, rise, report)
                assert report["blockCount"] == 2 * span + 4
                count += 1
        # A route cut must be caught by actual physical output, not a fabricated
        # report based solely on the identityBit declaration.
        requestPath.write_bytes(canonical(base))
        package = buildExperiment(requestPath, root)
        missing = package["implementationReport"]["route"]["wirePositions"][2]
        broken = copy.deepcopy(package)
        broken["circuitDesign"]["blocks"] = [b for b in broken["circuitDesign"]["blocks"] if tuple(b["pos"]) != tuple(missing)]
        broken["initialization"]["steps"] = [s for s in broken["initialization"]["steps"] if tuple(s["pos"]) != tuple(missing)]
        packagePath.write_bytes(canonical(broken))
        result = subprocess.run([adapter, str(packagePath)], capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
        assert any(row["input"] != row["output"] for row in json.loads(result.stdout)["report"]["observations"])
        count += 1
        for label, mutate in [
            ("missingInitialization", lambda p: p["initialization"]["steps"].pop()),
            ("badState", lambda p: p["circuitDesign"]["blocks"][0]["properties"].update(invalid="true")),
            ("wrongOutputFace", lambda p: p["interfaceMap"]["dataOut"].update(face="west")),
            ("earlySample", lambda p: p["verification"]["steps"][1].update(waitGt=1))]:
            malformed = copy.deepcopy(package)
            mutate(malformed)
            packagePath.write_bytes(canonical(malformed))
            result = subprocess.run([adapter, str(packagePath)], capture_output=True, text=True)
            assert result.returncode != 0, label
            count += 1
    print(f"PASS {count} native integration scenarios; 8 placements, 78 span/rise patterns, real edges, broken route and rejection cases")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: physicalIntegrationTests.py <physicalAdapter>")
    run(str(Path(sys.argv[1]).resolve()))
