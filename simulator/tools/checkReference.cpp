#include "simulator/simulator.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>

using namespace simulator;

// Compare externally captured vanilla observations at their actual coordinates.
// Unlike checkWorldReplay, this checks vanilla values, not two C++ snapshots.
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
            if (!fixture.at("frames").is_array() || fixture.at("frames").empty()
                || !fixture.at("watch").is_array() || fixture.at("watch").empty())
                throw std::runtime_error("Fixture must contain observations");
            const auto origin = fixture.at("origin").get<BlockPos>();
            Simulator simulation(registry);
            // 刻内更新轨迹：只有捕获里带轨迹时才开启，容量与原版侧一致。
            const bool lenientInteract = fixture.value("lenientInteract", false);
            const bool tracing = fixture.contains("updateTrace");
            if (tracing) simulation.updateTraceLimit = fixture.at("updateTraceLimit").get<std::size_t>();
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
                simulation.markUpdateTrace(tick);
                for (const auto& command : fixture.at("commands")) if (command.at("tick") == tick) {
                    const auto pos = absolute(command.at("pos"));
                    if (command.contains("placedBy") || command.contains("playerPlace")) simulation.place(pos, command.at("stateId"));
                    else if (command.contains("stateId")) simulation.setBlock(pos, command.at("stateId"));
                    else if (command.contains("interact")) {
                        std::optional<Direction> facing;
                        if (command.contains("playerFacing")) facing = parseDirection(command.at("playerFacing"));
                        // 与捕获器的 lenientInteract 对应：目标已经不可交互时两侧都跳过。
                        if (lenientInteract) { try { simulation.interact(pos, facing); } catch (const std::invalid_argument&) {} }
                        else simulation.interact(pos, facing);
                    }
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
                    if (frame.contains("groundItems") && !frame.at("groundItems").at(index).is_null())
                        compare("groundItems", frame.at("groundItems").at(index), simulation.suckableItems(pos));
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
            if (tracing && !differs) {
                // 截断的轨迹不能判为通过：两侧任何一边耗尽容量都直接失败。
                if (fixture.value("updateTraceTruncated", false) || simulation.updateTraceTruncated)
                    throw std::runtime_error("Update trace truncated; raise the capacity or shorten the scenario");
                const auto& expectedTrace = fixture.at("updateTrace");
                // 原版侧记录的是相对坐标；内核记录绝对坐标，这里换算回相对再比较。
                // 条目形如 ["m",x,y,z,...] / ["s",x,y,z,nx,ny,nz,...] / ["n"|"f",x,y,z,...]，
                // 形状更新有两组坐标，其余只有一组；刻标记是裸整数。
                Json actualTrace = Json::array();
                for (const auto& entry : simulation.updateTrace) {
                    if (!entry.is_array()) { actualTrace.push_back(entry); continue; }
                    Json converted = entry;
                    const std::size_t groups = entry.at(0).get<std::string>() == "s" ? 2 : 1;
                    for (std::size_t group = 0; group < groups; ++group) {
                        const std::size_t base = 1 + group * 3;
                        converted[base] = entry[base].get<int>() - origin.x;
                        converted[base + 1] = entry[base + 1].get<int>() - origin.y;
                        converted[base + 2] = entry[base + 2].get<int>() - origin.z;
                    }
                    actualTrace.push_back(std::move(converted));
                }
                const auto shared = std::min(expectedTrace.size(), actualTrace.size());
                for (std::size_t i = 0; i < shared; ++i) if (expectedTrace[i] != actualTrace[i]) {
                    result["status"] = "difference";
                    Json context = Json::object();
                    const auto from = i > 6 ? i - 6 : 0;
                    context["expected"] = Json::array(); context["actual"] = Json::array();
                    for (std::size_t j = from; j < std::min(shared, i + 4); ++j) {
                        context["expected"].push_back(expectedTrace[j]);
                        context["actual"].push_back(actualTrace[j]);
                    }
                    result["firstTraceDifference"] = {{"index", i}, {"expected", expectedTrace[i]}, {"actual", actualTrace[i]}, {"context", context}};
                    differs = true;
                    break;
                }
                if (!differs && expectedTrace.size() != actualTrace.size()) {
                    result["status"] = "difference";
                    result["firstTraceDifference"] = {{"index", shared}, {"expectedLength", expectedTrace.size()}, {"actualLength", actualTrace.size()}};
                    differs = true;
                }
                result["traceEntries"] = actualTrace.size();
                if (!traceOut.empty()) { std::ofstream dump(traceOut); dump << actualTrace.dump(); }
            }
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
