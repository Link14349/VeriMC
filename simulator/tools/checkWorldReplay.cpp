#include "simulator/simulator.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace simulator;
namespace {
// Stream canonical records: queues, RNG, inventories, same-tick edges and
// display changes are compared, not merely final lamp states or wall time.
void record(Simulator& sim, const std::string& scenario, std::size_t point) {
    Json changes = Json::array();
    for (const auto& cell : sim.takeChanges()) changes.push_back({cell.pos, cell.state});
    std::cout << Json{{"scenario", scenario}, {"point", point},
        {"checkpoint", sim.saveProject(scenario, true)}, {"changes", changes},
        {"updates", sim.statistics.updates}, {"events", sim.statistics.scheduledEvents},
        {"stateChanges", sim.statistics.stateChanges}, {"revision", sim.revision}}.dump() << '\n';
}
void replayFixture(const BlockRegistry& registry, const std::filesystem::path& path) {
    std::ifstream input(path);
    const auto fixture = Json::parse(input);
    if (!fixture.contains("frames") || !fixture.contains("commands") || !fixture.contains("origin")) return;
    Simulator sim(registry);
    const auto origin = fixture.at("origin").get<BlockPos>();
    auto absolute = [&](const Json& row) {
        const auto pos = row.get<BlockPos>();
        return BlockPos{origin.x + pos.x, origin.y + pos.y, origin.z + pos.z};
    };
    for (const auto& pos : fixture.at("watch")) sim.addProbe(absolute(pos));
    std::size_t point = 0;
    const auto scenario = path.stem().string();
    for (const auto& frame : fixture.at("frames")) {
        sim.advanceTo(frame.at("tick"));
        record(sim, scenario, point++);
        for (const auto& command : fixture.at("commands")) if (command.at("tick") == frame.at("tick")) {
            const auto pos = absolute(command.at("pos"));
            if (command.contains("placedBy") || command.contains("playerPlace")) sim.place(pos, command.at("stateId"));
            else if (command.contains("stateId")) sim.setBlock(pos, command.at("stateId"));
            else if (command.contains("interact")) sim.interact(pos);
            else sim.stimulate(pos, command.at("stimulus"));
            record(sim, scenario, point++);
        }
    }
}
void replayChains(const BlockRegistry& registry) {
    Simulator sim(registry);
    std::vector<BlockPos> inputs;
    for (int i = 0; i < 200; ++i) {
        // Straddle negative and positive chunk boundaries in all three axes.
        BlockPos base{i % 20 * 8 - 81, i % 3 * 16 - 17, i / 20 * 4 - 17};
        for (int x = 0; x < 6; ++x) sim.world.set({base.x + x, base.y, base.z}, registry.state("stone"));
        inputs.push_back({base.x, base.y + 1, base.z});
        sim.place(inputs.back(), registry.state("lever", {{"face", "floor"}}));
        sim.place({base.x + 1, base.y + 1, base.z}, registry.state("redstone_wire"));
        sim.place({base.x + 2, base.y + 1, base.z}, registry.state("repeater", {{"facing", "west"}, {"delay", std::to_string(i % 4 + 1)}}));
        sim.place({base.x + 3, base.y + 1, base.z}, registry.state("redstone_wire"));
        sim.place({base.x + 4, base.y + 1, base.z}, registry.state("redstone_lamp"));
        if (i < 64) sim.addProbe({base.x + 3, base.y + 1, base.z});
    }
    std::size_t point = 0;
    record(sim, "negativeChains64Probes", point++);
    for (int cycle = 0; cycle < 12; ++cycle) {
        for (const auto& pos : inputs) sim.interact(pos);
        record(sim, "negativeChains64Probes", point++);
        for (int tick = 0; tick < 12; ++tick) {
            sim.advanceTo(sim.currentTick + 1);
            record(sim, "negativeChains64Probes", point++);
        }
    }
}
}
int main() {
    try {
        BlockRegistry registry;
        std::vector<std::filesystem::path> paths;
        for (const auto& entry : std::filesystem::directory_iterator(std::string(SIMULATOR_DATA_DIR) + "/../tests/fixtures"))
            if (entry.path().extension() == ".json") paths.push_back(entry.path());
        std::sort(paths.begin(), paths.end());
        for (const auto& path : paths) replayFixture(registry, path);
        replayChains(registry);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
