#include "simulator/simulator.hpp"
#include "buildIdentity.hpp"
#include <fstream>
#include <iostream>
#include <map>
#include <set>

using namespace simulator;
namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::invalid_argument(message);
}
BlockPos position(const Json& row) {
    const auto p = row.get<BlockPos>();
    require(p.y >= -64 && p.y < 320, "EPlacement Y outside -64..319");
    return p;
}
void advance(Simulator& sim, Tick ticks) {
    const auto target = sim.currentTick + ticks;
    sim.advanceTo(target);
    require(!sim.breakRequested && !sim.faulted, "ESimulation execution interrupted: " + sim.pauseReason);
    require(sim.currentTick == target, "ESimulation execution budget exhausted before observation tick");
}
void checkBlocks(const Simulator& sim, const std::map<BlockPos, StateId>& design) {
    require(sim.world.size() == design.size(), "ESimulation block disappeared during ordered placement");
    for (const auto& [pos, state] : design)
        require(sim.at(pos).type == sim.registry[state].type, "ESimulation block type changed");
}
std::string hex(const std::vector<std::uint8_t>& bytes) {
    const char* digits = "0123456789abcdef";
    std::string result;
    for (const auto byte : bytes) { result += digits[byte >> 4]; result += digits[byte & 15]; }
    return result;
}
}

int main(int argc, char** argv) {
    try {
        require(argc == 2, "Usage: physicalAdapter <physicalExperiment.json>");
        std::ifstream input(argv[1]);
        require(bool(input), "EFormat cannot open experiment");
        // Each object has its own key set; a set for the entire tree would reject
        // legitimate repeated field names in different block records.
        std::vector<std::set<std::string>> keys;
        auto callback = [&](int, Json::parse_event_t event, Json& parsed) {
            if (event == Json::parse_event_t::object_start) keys.emplace_back();
            if (event == Json::parse_event_t::key)
                require(keys.back().insert(parsed.get<std::string>()).second, "EFormat duplicate JSON key");
            if (event == Json::parse_event_t::object_end) keys.pop_back();
            return true;
        };
        const auto package = Json::parse(input, callback);
        require(package.at("format") == "verimc.physicalExperiment" &&
                package.at("formatVersion").is_number_integer() && package.at("formatVersion") == 1 &&
                package.at("targetId") == "java26_2-experimental" && package.at("status") == "unverified",
                "EFormat unsupported experimental package");
        const auto& init = package.at("initialization");
        require(init.at("strategy") == "orderedPlacement", "ECapability only ordered placement");
        const auto& circuit = package.at("circuitDesign");
        require(circuit.at("blockEntities").is_array() && circuit.at("blockEntities").empty(),
                "ECapability block entities not supported");
        require(circuit.at("blocks").is_array() && circuit.at("blocks").size() <= 128,
                "ECapability experiment block budget 128");
        BlockRegistry registry;
        std::map<BlockPos, StateId> design;
        for (const auto& block : circuit.at("blocks")) {
            const auto pos = position(block.at("pos"));
            const auto state = registry.state(block.at("name"), block.at("properties"));
            const auto device = registry[state].device;
            require(device == Device::solid || device == Device::wire || device == Device::repeater ||
                    device == Device::lever, "ECapability unsupported experiment block");
            require(design.emplace(pos, state).second, "EFormat duplicate block position");
            require(!registry[state].powered && registry[state].power == 0,
                    "ECapability derived powered design state");
        }
        require(init.at("steps").is_array() && init.at("steps").size() <= 256, "EFormat initialization budget");
        std::set<BlockPos> initialized;
        for (const auto& step : init.at("steps")) {
            require(step.at("op") == "place", "ECapability this adapter only supports place steps");
            const auto pos = position(step.at("pos"));
            require(design.contains(pos) && initialized.insert(pos).second, "EFormat invalid ordered placement");
        }
        require(initialized.size() == design.size(), "EFormat incomplete ordered placement");
        const auto& ports = package.at("interfaceMap");
        require(ports.at("stimulus").at("adapterId") == "floorLeverInteractV1", "ECapability input adapter");
        const auto lever = position(ports.at("stimulus").at("pos"));
        require(design.contains(lever) && registry[design.at(lever)].device == Device::lever &&
                registry.property(design.at(lever), "face") == "floor", "ECapability expected floor lever");
        const auto inPos = position(ports.at("dataIn").at("pos"));
        const auto outPos = position(ports.at("dataOut").at("pos"));
        const auto inFace = parseDirection(ports.at("dataIn").at("face"));
        const auto outFace = parseDirection(ports.at("dataOut").at("face"));
        const Json encoding{{"kind", "digital"}, {"low", Json::array({0, 0})}, {"high", Json::array({1, 15})}};
        require(ports.at("dataIn").at("encoding") == encoding && ports.at("dataOut").at("encoding") == encoding,
                "ECapability unsupported encoding");
        require(ports.at("dataIn").at("channel") == "receivePower" &&
                ports.at("dataOut").at("channel") == "strongOutput", "ECapability unsupported interface channel");
        require(design.contains(inPos) && registry[design.at(inPos)].device == Device::repeater &&
                inPos.relative(inFace) == lever && inFace == registry[design.at(inPos)].facing,
                "ECapability input pin does not face stimulus");
        require(design.contains(outPos) && registry[design.at(outPos)].device == Device::repeater &&
                outFace == opposite(registry[design.at(outPos)].facing), "ECapability output pin direction");
        const auto& timeline = package.at("verification").at("steps");
        require(package.at("verification").at("model") == "identityBit" && timeline.is_array() &&
                !timeline.empty() && timeline.size() <= 256, "ECapability verification contract");
        for (const auto& step : timeline)
            require(step.at("value").is_boolean() && step.at("waitGt").is_number_integer() &&
                    step.at("waitGt") >= 1 && step.at("waitGt") <= 1000, "EFormat invalid timeline");
        Simulator sim(registry);
        sim.setRandomSeed(0);
        for (const auto& step : init.at("steps")) {
            const auto pos = position(step.at("pos"));
            sim.place(pos, design.at(pos));
            require(!sim.breakRequested && !sim.faulted, "ESimulation initialization interrupted");
        }
        checkBlocks(sim, design);
        // Export a design before applying the test timeline. The package retains
        // authoritative initialization order; reimporting this convenience file
        // alone does not constitute a replay of the verification.
        auto project = sim.saveProject("跨层固定组件实验（原版未验证）", false);
        project["physicalExperiment"] = {{"status", "unverified"}, {"interfaceMap", ports},
                                         {"sourceMap", package.at("sourceMap")}, {"initialization", init}};
        // Core Direction is receiver-to-emitter, whereas a physical pin face is
        // emitter-to-receiver: convert exactly once for probes and observation.
        sim.addProbe(outPos, "dataOut", "direction", opposite(outFace));
        Json observations = Json::array();
        Tick maxObservedEdge = 0;
        for (const auto& step : timeline) {
            const bool value = step.at("value");
            const Tick start = sim.currentTick;
            if (sim.at(lever).powered != value) sim.interact(lever);
            advance(sim, step.at("waitGt").get<Tick>());
            require(sim.at(lever).powered == value, "ESimulation stimulus did not reach requested state");
            checkBlocks(sim, design);
            require(sim.pendingEvents() == 0, "ESimulation sampling while scheduled events remain");
            const auto strength = sim.directSignal(outPos, opposite(outFace));
            require(strength >= 0 && strength <= 15, "ESimulation invalid output strength");
            observations.push_back({{"input", value}, {"output", strength > 0}, {"strength", strength},
                                    {"stimulusTick", start}, {"observationTick", sim.currentTick}});
            for (const auto& edge : sim.getTrace())
                if (edge.tick >= start) maxObservedEdge = std::max(maxObservedEdge, edge.tick - start);
        }
        require(sim.traceDropped == 0, "ESimulation trace truncated");
        Json edges = Json::array();
        for (const auto& edge : sim.getTrace())
            edges.push_back({{"tick", edge.tick}, {"sequence", edge.sequence}, {"value", edge.value}});
        Json report{{"format", "verimc.simulationExperiment"}, {"formatVersion", 1},
                    {"adapterVersion", "fixedPhysicalV1"}, {"adapterSourceSha256", VERIMC_ADAPTER_SHA256},
                    {"simulatorBuildId", SIMULATOR_BUILD_ID}, {"rulesSha256", hex(registry.ruleFingerprint())},
                    {"profile", project.at("profile")}, {"randomSeed", "0"},
                    {"observations", observations}, {"edges", edges},
                    {"maximumObservedEdgeOffsetGt", maxObservedEdge}, {"blockCount", design.size()}};
        std::cout << Json{{"report", report}, {"project", project}}.dump(2) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
