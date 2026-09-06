#include "simulator/simulator.hpp"
#include <fstream>
#include <iostream>
#include <chrono>

using namespace simulator;
int main(int argc, char** argv) {
    try {
        BlockRegistry registry; Simulator sim(registry);
        if (argc > 1 && std::string(argv[1]) == "--benchmark") {
            const int count = argc > 2 ? std::stoi(argv[2]) : 1000;
            if (count < 1 || count > 100000) throw std::invalid_argument("电路数量需为 1–100000");
            auto solid = registry.state("stone"), wire = registry.state("redstone_wire"), repeater = registry.state("repeater", {{"facing", "west"}});
            for (int i = 0; i < count; ++i) {
                BlockPos base{i % 100 * 8, 0, i / 100 * 4};
                for (int x = 0; x < 6; ++x) sim.world.set({base.x + x, 0, base.z}, solid);
                sim.place({base.x, 1, base.z}, registry.state("lever", {{"face", "floor"}}));
                sim.place({base.x + 1, 1, base.z}, wire); sim.place({base.x + 2, 1, base.z}, repeater);
                sim.place({base.x + 3, 1, base.z}, wire); sim.place({base.x + 4, 1, base.z}, registry.state("redstone_lamp"));
            }
            sim.statistics = {}; auto start = std::chrono::steady_clock::now();
            for (int cycle = 0; cycle < 40; ++cycle) {
                for (int i = 0; i < count; ++i) sim.interact({i % 100 * 8, 1, i / 100 * 4});
                sim.advanceTo(sim.currentTick + 8);
                sim.takeChanges();
            }
            const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            std::cout << Json{{"benchmark", "drivenRepeaterChains"}, {"version", "26.2"}, {"circuits", count}, {"blocks", sim.world.size()}, {"ticks", sim.currentTick}, {"seconds", elapsed}, {"gameTicksPerSecond", static_cast<double>(sim.currentTick) / elapsed}, {"events", sim.statistics.scheduledEvents}, {"neighborUpdates", sim.statistics.updates}, {"chunkStorageBytes", sim.world.storageBytes()}}.dump(2) << '\n';
            return 0;
        }
        if (argc < 2) { std::cout << "simulatorCli <project.json> [gameTicks]\nsimulatorCli --benchmark [circuitCount]\n"; return 0; }
        std::ifstream input(argv[1]); if (!input) throw std::runtime_error("无法打开工程"); sim.loadProject(Json::parse(input));
        Tick target = sim.currentTick + (argc > 2 ? std::stoull(argv[2]) : 20);
        while (sim.currentTick < target && !sim.breakRequested) sim.advanceTo(target);
        std::cout << sim.saveProject("CLI 运行快照", true).dump(2) << '\n';
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
