#include "simulator/simulator.hpp"
#include <sstream>
#include <cctype>

namespace simulator {
Json Simulator::saveProject(const std::string& name, bool checkpoint) const {
    Json data{{"format", "verimc.simulator"}, {"formatVersion", 1}, {"minecraftVersion", "26.2"}, {"edition", "java"}, {"kind", checkpoint ? "checkpoint" : "circuit"}, {"name", name}};
    data["profile"] = {{"experimentalRedstone", false}, {"naturalRandomTicks", false}, {"loadedRegionOnly", true}};
    data["blocks"] = Json::array();
    for (const auto& cell : world.cells()) { auto block = registry.describe(cell.state); block.erase("stateId"); block["pos"] = cell.pos; data["blocks"].push_back(block); }
    data["probes"] = Json::array();
    for (const auto& p : probes) data["probes"].push_back({{"id", p.id}, {"pos", p.pos}, {"name", p.name}, {"mode", p.mode}, {"direction", directionNames[static_cast<unsigned>(p.direction)]}, {"trigger", p.trigger}, {"triggerValue", p.triggerValue}, {"lastValue", p.lastValue}});
    data["blockData"] = Json::array();
    for (const auto& [pos, state] : runtime) {
        Json row{{"pos", pos}, {"values", state.values}};
        if (checkpoint) { row["output"] = state.output; row["torchToggles"] = state.torchToggles; }
        data["blockData"].push_back(std::move(row));
    }
    if (checkpoint) {
        data["tick"] = currentTick; data["nextOrder"] = nextOrder; data["sequence"] = sequence; data["nextProbeId"] = nextProbeId;
        data["events"] = Json::array(); auto queue = scheduled;
        while (!queue.empty()) { const auto e = queue.top(); queue.pop(); data["events"].push_back({{"tick", e.tick}, {"priority", e.priority}, {"order", e.order}, {"pos", e.pos}, {"type", e.type}}); }
        data["trace"] = Json::array(); for (const auto& e : trace) data["trace"].push_back({e.probeId, e.tick, e.sequence, e.value});
        data["traceDropped"] = traceDropped;
    }
    return data;
}
void Simulator::loadProject(const Json& data) {
    if (data.at("format") != "verimc.simulator" || data.at("formatVersion") != 1 || data.at("minecraftVersion") != "26.2" || data.at("edition") != "java") throw std::invalid_argument("工程格式或 Minecraft 版本不匹配");
    const auto& profile = data.at("profile");
    if (profile.at("experimentalRedstone") != false || profile.at("naturalRandomTicks") != false || profile.at("loadedRegionOnly") != true) throw std::invalid_argument("工程要求尚未支持的仿真规则");
    bool checkpoint = data.at("kind") == "checkpoint";
    if (!checkpoint && data.at("kind") != "circuit") throw std::invalid_argument("未知工程类型");
    if (data.at("blocks").size() > 2000000) throw std::invalid_argument("工程超过 200 万方块限制");
    Simulator candidate(registry); candidate.traceCapacity = traceCapacity; candidate.updateBudget = updateBudget;
    std::unordered_set<BlockPos, PosHash> occupied;
    for (const auto& row : data.at("blocks")) {
        auto p = row.at("pos").get<BlockPos>(); auto id = registry.state(row.at("name"), row.at("properties"));
        if (registry.type(id).supportLevel == "unimplemented") throw std::invalid_argument("工程包含尚未支持的器件：" + registry.type(id).name);
        if (!occupied.insert(p).second) throw std::invalid_argument("工程包含重复坐标");
        candidate.world.set(p, id);
    }
    for (const auto& row : data.value("blockData", Json::array())) {
        auto p = row.at("pos").get<BlockPos>();
        if (candidate.world.get(p) == 0) throw std::invalid_argument("器件数据对应位置没有方块");
        auto& state = candidate.runtime[p]; state.values = row.at("values");
        if (checkpoint) { state.output = row.at("output"); state.torchToggles = row.at("torchToggles").get<std::deque<Tick>>(); }
    }
    if (checkpoint) {
        candidate.currentTick = data.at("tick"); candidate.nextOrder = data.at("nextOrder"); candidate.sequence = data.at("sequence");
        if (data.at("events").size() > 2000000) throw std::invalid_argument("工程计划事件过多");
        for (const auto& row : data.at("events")) {
            ScheduledEvent e{row.at("tick"), row.at("priority"), row.at("order"), row.at("pos").get<BlockPos>(), row.at("type")};
            if (e.tick < candidate.currentTick || e.priority < -3 || e.priority > 3 || e.order >= candidate.nextOrder || !candidate.scheduledKeys.insert({e.pos, e.type}).second) throw std::invalid_argument("无效的运行队列");
            candidate.scheduled.push(e);
        }
    } else {
        for (const auto& cell : candidate.world.cells()) candidate.onPlace(cell.pos, cell.state, 0);
        for (const auto& cell : candidate.world.cells()) candidate.neighborChanged(cell.pos);
    }
    for (const auto& row : data.at("probes")) {
        auto newId = candidate.addProbe(row.at("pos").get<BlockPos>(), row.at("name"), row.at("mode"), parseDirection(row.at("direction")));
        candidate.configureProbe(newId, row);
        if (checkpoint) { candidate.probes.back().id = row.at("id"); candidate.probes.back().lastValue = row.at("lastValue"); }
    }
    if (checkpoint) {
        candidate.nextProbeId = data.at("nextProbeId"); candidate.rebuildProbeDependencies(); candidate.trace.clear();
        for (const auto& e : data.at("trace")) { if (candidate.trace.size() >= candidate.traceCapacity) candidate.trace.pop_front(); candidate.trace.push_back({e.at(0), e.at(1), e.at(2), e.at(3)}); }
        candidate.traceDropped = data.at("traceDropped");
    }
    using std::swap;
    swap(world, candidate.world); swap(runtime, candidate.runtime); swap(scheduled, candidate.scheduled); swap(scheduledKeys, candidate.scheduledKeys);
    swap(probes, candidate.probes); swap(probeDependencies, candidate.probeDependencies); swap(trace, candidate.trace);
    currentTick = candidate.currentTick; nextOrder = candidate.nextOrder; sequence = candidate.sequence; nextProbeId = candidate.nextProbeId; traceDropped = candidate.traceDropped;
    statistics = {}; changes.clear(); breakRequested = false; pauseReason.clear(); ++revision;
}
std::string Simulator::exportVcd() const {
    std::ostringstream out;
    out << "$version VeriMC simulator / Java 26.2 $end\n$timescale 1ms $end\n$comment One game tick = 50 ms. Same-tick transitions retain recorded order; no physical sub-tick time is invented. $end\n$scope module circuit $end\n";
    for (const auto& p : probes) { std::string name = p.name; for (auto& ch : name) if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_') ch = '_'; out << "$var wire 4 p" << p.id << ' ' << name << " $end\n"; }
    out << "$upscope $end\n$enddefinitions $end\n";
    Tick lastTick = std::numeric_limits<Tick>::max();
    for (const auto& e : trace) {
        if (std::none_of(probes.begin(), probes.end(), [&](const Probe& p) { return p.id == e.probeId; })) continue;
        if (lastTick != e.tick) { out << '#' << e.tick * 50 << '\n'; lastTick = e.tick; }
        out << "$comment sequence " << e.sequence << " $end\nb";
        for (int bit = 3; bit >= 0; --bit) out << ((e.value >> bit) & 1);
        out << " p" << e.probeId << '\n';
    }
    return out.str();
}
}
