#include "simulator/referenceReplay.hpp"
#include <fstream>
#include <iostream>

using namespace simulator;

// Compare externally captured vanilla observations at their actual coordinates.
// Unlike checkWorldReplay, this checks vanilla values, not two C++ snapshots.
// 重放循环本身在 simulator/referenceReplay.hpp，与 coreTests 的差分用例共用同一套语义。
int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "Usage: checkReference [--traceOut path] fixture.json ...\n"; return 2; }
    BlockRegistry registry;
    bool failed = false;
    std::string traceOut;
    for (int arg = 1; arg < argc; ++arg) {
        if (std::string(argv[arg]) == "--traceOut" && arg + 1 < argc) { traceOut = argv[++arg]; continue; }
        Json result{{"fixture", argv[arg]}, {"status", "match"}};
        try {
            std::ifstream file(argv[arg]);
            if (!file) throw std::runtime_error("Cannot open fixture");
            const auto fixture = Json::parse(file);
            Simulator simulation(registry);
            result.update(replayReferenceFixture(simulation, fixture, traceOut));
        } catch (const std::exception& error) {
            result["status"] = "error";
            result["error"] = error.what();
        }
        if (result.at("status") != "match") failed = true;
        std::cout << result.dump() << '\n';
    }
    return failed ? 1 : 0;
}
