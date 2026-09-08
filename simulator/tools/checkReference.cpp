#include "simulator/simulator.hpp"
#include <fstream>
#include <iostream>

using namespace simulator;

// Compare externally captured vanilla observations at their actual coordinates.
// Unlike checkWorldReplay, this checks vanilla values, not two C++ snapshots.
int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "Usage: checkReference fixture.json ...\n"; return 2; }
    BlockRegistry registry;
    bool failed = false;
    for (int arg = 1; arg < argc; ++arg) {
        Json result{{"fixture", argv[arg]}, {"status", "match"}};
        try {
            std::ifstream file(argv[arg]);
            if (!file) throw std::runtime_error("Cannot open fixture");
            const auto fixture = Json::parse(file);
            if (!fixture.at("frames").is_array() || fixture.at("frames").empty()
                || !fixture.at("watch").is_array() || fixture.at("watch").empty())
                throw std::runtime_error("Fixture must contain observations");
            const auto origin = fixture.at("origin").get<BlockPos>();
            Simulator simulation(registry);
            auto absolute = [&](const Json& value) {
                const auto pos = value.get<BlockPos>();
                return BlockPos{origin.x + pos.x, origin.y + pos.y, origin.z + pos.z};
            };
            bool differs = false;
            Tick expectedTick = 0;
            for (const auto& frame : fixture.at("frames")) {
                const Tick tick = frame.at("tick");
                if (tick != expectedTick++) throw std::runtime_error("Capture frames must be contiguous from tick zero");
                for (const char* field : {"states", "analogs", "inventories", "bells", "jukeboxes"}) {
                    if (frame.contains(field) && (!frame.at(field).is_array() || frame.at(field).size() != fixture.at("watch").size()))
                        throw std::runtime_error("Observation array length differs from watch list");
                }
                simulation.advanceTo(tick);
                for (const auto& command : fixture.at("commands")) if (command.at("tick") == tick) {
                    const auto pos = absolute(command.at("pos"));
                    if (command.contains("placedBy") || command.contains("playerPlace")) simulation.place(pos, command.at("stateId"));
                    else if (command.contains("stateId")) simulation.setBlock(pos, command.at("stateId"));
                    else if (command.contains("interact")) simulation.interact(pos);
                    else simulation.stimulate(pos, command.at("stimulus"));
                }
                for (std::size_t index = 0; index < fixture.at("watch").size(); ++index) {
                    const auto pos = absolute(fixture.at("watch").at(index));
                    auto compare = [&](const char* field, const Json& expected, const Json& actual) {
                        if (expected == actual || differs) return;
                        differs = true;
                        result["status"] = "difference";
                        result["firstDifference"] = {{"tick", tick}, {"relativePos", fixture.at("watch").at(index)},
                                                     {"field", field}, {"expected", expected}, {"actual", actual}};
                        if (std::string(field) == "states") {
                            result["firstDifference"]["expectedBlock"] = registry.describe(expected.get<StateId>());
                            result["firstDifference"]["actualBlock"] = registry.describe(actual.get<StateId>());
                        }
                    };
                    compare("states", frame.at("states").at(index), simulation.world.get(pos));
                    if (frame.at("analogs").at(index) != -1) compare("analogs", frame.at("analogs").at(index), simulation.analogOutput(pos));
                    if (frame.contains("inventories")) compare("inventories", frame.at("inventories").at(index), simulation.inventoryJson(pos, false));
                    if (frame.contains("bells")) compare("bells", frame.at("bells").at(index), simulation.inspect(pos)["runtime"].value("ringing", false));
                    if (frame.contains("jukeboxes") && !frame.at("jukeboxes").at(index).is_null()) {
                        const auto actual = simulation.inspect(pos).at("jukebox");
                        compare("jukeboxes", frame.at("jukeboxes").at(index), Json{{"playing", actual.at("playing")}, {"elapsed", actual.at("elapsed")}});
                    }
                    if (differs) break;
                }
                if (differs) break;
            }
            if (!differs && expectedTick - 1 != fixture.at("endTick").get<Tick>())
                throw std::runtime_error("Capture does not reach endTick");
            result["origin"] = fixture.at("origin");
            result["frames"] = fixture.at("frames").size();
            result["watch"] = fixture.at("watch").size();
        } catch (const std::exception& error) {
            result["status"] = "error";
            result["error"] = error.what();
        }
        if (result.at("status") != "match") failed = true;
        std::cout << result.dump() << '\n';
    }
    return failed ? 1 : 0;
}
