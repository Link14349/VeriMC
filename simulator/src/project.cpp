#include "simulator/simulator.hpp"
#include <sstream>
#include <cctype>

namespace simulator {
namespace {
Json eventJson(const ScheduledEvent& e) {
    return {{"tick", e.tick}, {"priority", e.priority}, {"order", e.order}, {"pos", e.pos}, {"type", e.type}, {"phase", e.phase}, {"data", e.data}, {"entityOrder", e.entityOrder}};
}
ScheduledEvent readEvent(const Json& row) {
    return {row.at("tick"), row.at("priority"), row.at("order"), row.at("pos").get<BlockPos>(), row.at("type"), row.value("phase", std::uint8_t{0}), row.value("data", std::uint64_t{0}), row.value("entityOrder", std::uint64_t{0})};
}
}
std::unique_ptr<Simulator> Simulator::clone() const {
    auto result = std::make_unique<Simulator>(registry); result->restore(*this); return result;
}
void Simulator::restore(const Simulator& snapshot) {
    if (&registry != &snapshot.registry) throw std::invalid_argument("运行快照的注册表不匹配");
    world = snapshot.world; runtime = snapshot.runtime; motions = snapshot.motions; scheduled = snapshot.scheduled; scheduledKeys = snapshot.scheduledKeys;
    blockTicks = snapshot.blockTicks;
    hoppers = snapshot.hoppers; entityOrders = snapshot.entityOrders; nextEntityOrder = snapshot.nextEntityOrder;
    recentTorchToggles = snapshot.recentTorchToggles; torchToggleCounts = snapshot.torchToggleCounts;
    probes = snapshot.probes; probeDependencies = snapshot.probeDependencies; trace = snapshot.trace;
    currentTick = snapshot.currentTick; nextOrder = snapshot.nextOrder; sequence = snapshot.sequence; nextProbeId = snapshot.nextProbeId;
    traceDropped = snapshot.traceDropped; traceCapacity = snapshot.traceCapacity; updateBudget = snapshot.updateBudget; statistics = snapshot.statistics;
    breakRequested = snapshot.breakRequested; faulted = snapshot.faulted; pauseReason = snapshot.pauseReason; changes.clear(); ++revision;
}
Json Simulator::saveProject(const std::string& name, bool checkpoint) const {
    if (!checkpoint && !motions.empty()) throw std::invalid_argument("活塞正在运动，请导出运行快照，或等待动作完成后导出电路");
    Json data{{"format", "verimc.simulator"}, {"formatVersion", 1}, {"minecraftVersion", "26.2"}, {"edition", "java"}, {"kind", checkpoint ? "checkpoint" : "circuit"}, {"name", name}};
    data["profile"] = {{"experimentalRedstone", false}, {"naturalRandomTicks", false}, {"loadedRegionOnly", true}};
    data["blocks"] = Json::array();
    for (const auto& cell : world.cells()) { auto block = registry.describe(cell.state); block.erase("stateId"); block["pos"] = cell.pos; data["blocks"].push_back(block); }
    data["probes"] = Json::array();
    for (const auto& p : probes) data["probes"].push_back({{"id", p.id}, {"pos", p.pos}, {"name", p.name}, {"mode", p.mode}, {"direction", directionNames[static_cast<unsigned>(p.direction)]}, {"trigger", p.trigger}, {"triggerValue", p.triggerValue}, {"lastValue", p.lastValue}});
    data["blockData"] = Json::array();
    for (const auto& [pos, state] : runtime) {
        Json row{{"pos", pos}, {"values", state.values}};
        if (inventorySize(world.get(pos))) row["inventory"] = inventoryJson(pos, false);
        if (checkpoint) row["output"] = state.output;
        data["blockData"].push_back(std::move(row));
    }
    auto positionOrder = [](const Json& a, const Json& b) { return a.at("pos") < b.at("pos"); };
    std::sort(data["blockData"].begin(), data["blockData"].end(), positionOrder);
    data["entityOrder"] = Json::array();
    std::vector<std::pair<std::uint64_t, BlockPos>> orderedEntities;
    for (const auto& [pos, rank] : entityOrders) orderedEntities.push_back({rank, pos});
    std::sort(orderedEntities.begin(), orderedEntities.end());
    for (const auto& [rank, pos] : orderedEntities) data["entityOrder"].push_back({{"pos", pos}, {"order", rank}});
    data["nextEntityOrder"] = nextEntityOrder;
    if (checkpoint) {
        data["torchToggles"] = Json::array();
        for (const auto& toggle : recentTorchToggles) data["torchToggles"].push_back({{"pos", toggle.pos}, {"tick", toggle.tick}});
        data["faulted"] = faulted;
        data["tick"] = currentTick; data["nextOrder"] = nextOrder; data["sequence"] = sequence; data["nextProbeId"] = nextProbeId;
        auto events = blockTicks.queuedEvents(); auto queue = scheduled;
        while (!queue.empty()) { const auto e = queue.top(); queue.pop(); if (scheduledKeys.contains({e.pos, e.type, e.phase, e.data})) events.push_back(e); }
        std::sort(events.begin(), events.end(), [](const auto& a, const auto& b) { return a.key() < b.key(); });
        data["events"] = Json::array();
        for (const auto& event : events) data["events"].push_back(eventJson(event));
        data["blockTickState"] = {{"earliestCollection", blockTicks.earliestTick()}, {"batch", Json::array()}};
        for (const auto& event : blockTicks.batchEvents()) data["blockTickState"]["batch"].push_back(eventJson(event));
        data["hoppers"] = Json::array();
        for (const auto& [pos, h] : hoppers) data["hoppers"].push_back({{"pos", pos}, {"readyAt", h.readyAt}, {"firstTick", h.firstTick}, {"wakeAt", h.wakeAt}, {"generation", h.generation}});
        std::sort(data["hoppers"].begin(), data["hoppers"].end(), positionOrder);
        data["motions"] = Json::array();
        for (const auto& [p, m] : motions) data["motions"].push_back({{"pos", p}, {"movedState", m.movedState}, {"facing", static_cast<unsigned>(m.facing)}, {"extending", m.extending}, {"source", m.source}, {"progress", m.progress}, {"previousProgress", m.previousProgress}, {"lastTicked", m.lastTicked}, {"generation", m.generation}});
        std::sort(data["motions"].begin(), data["motions"].end(), positionOrder);
        data["trace"] = Json::array(); for (const auto& e : trace) data["trace"].push_back({e.probeId, e.tick, e.sequence, e.value});
        data["traceDropped"] = traceDropped;
    }
    return data;
}
void Simulator::loadProject(const Json& data) {
    if (data.value("faulted", false)) throw std::invalid_argument("此记录来自中止的执行，仅供检查，不能作为可运行快照加载");
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
        if (!checkpoint && registry[id].device == Device::movingPiston) throw std::invalid_argument("运动中的活塞需要包含内部状态的运行快照");
        if (!occupied.insert(p).second) throw std::invalid_argument("工程包含重复坐标");
        candidate.world.set(p, id);
    }
    std::unordered_set<std::uint64_t> usedRanks;
    candidate.nextEntityOrder = data.value("nextEntityOrder", std::uint64_t{0});
    for (const auto& row : data.value("entityOrder", Json::array())) {
        auto pos = row.at("pos").get<BlockPos>(); auto rank = row.at("order").get<std::uint64_t>();
        auto device = candidate.at(pos).device;
        if ((device != Device::hopper && device != Device::daylight && device != Device::movingPiston) || rank >= candidate.nextEntityOrder || !usedRanks.insert(rank).second || !candidate.entityOrders.emplace(pos, rank).second) throw std::invalid_argument("无效方块实体执行顺序");
    }
    for (const auto& row : data.value("blockData", Json::array())) {
        auto p = row.at("pos").get<BlockPos>();
        if (candidate.world.get(p) == 0) throw std::invalid_argument("器件数据对应位置没有方块");
        auto& state = candidate.runtime[p]; state.values = row.at("values");
        if (checkpoint) {
            state.output = row.at("output");
            if (!data.contains("torchToggles")) for (auto tick : row.value("torchToggles", std::vector<Tick>{})) candidate.recentTorchToggles.push_back({p, tick});
        }
        if (row.contains("inventory")) candidate.setInventory(p, row.at("inventory"), false, false);
        if (candidate.at(p).device == Device::detectorRail) state.values["carts"] = candidate.normalizeCarts(state.values.value("carts", Json::array()));
        candidate.validateRuntime(p);
    }
    if (checkpoint) {
        candidate.currentTick = data.at("tick"); candidate.nextOrder = data.at("nextOrder"); candidate.sequence = data.at("sequence");
        if (candidate.currentTick == UINT64_MAX) throw std::invalid_argument("仿真时间超出范围");
        if (data.contains("torchToggles")) for (const auto& row : data.at("torchToggles")) candidate.recentTorchToggles.push_back({row.at("pos").get<BlockPos>(), row.at("tick").get<Tick>()});
        std::stable_sort(candidate.recentTorchToggles.begin(), candidate.recentTorchToggles.end(), [](const auto& a, const auto& b) { return a.tick < b.tick; });
        for (const auto& toggle : candidate.recentTorchToggles) {
            if (toggle.tick > candidate.currentTick) throw std::invalid_argument("火把历史包含未来事件");
            ++candidate.torchToggleCounts[toggle.pos];
        }
        if (data.at("events").size() > 2000000) throw std::invalid_argument("工程计划事件过多");
        std::unordered_set<std::uint64_t> usedOrders;
        for (const auto& row : data.at("events")) {
            auto e = readEvent(row);
            if (e.phase == 2) {
                if (!row.contains("entityOrder")) throw std::invalid_argument("旧版快照缺少方块实体执行顺序，请使用电路工程重新开始运行");
                e.entityOrder = row.at("entityOrder");
                auto rank = candidate.entityOrders.find(e.pos);
                if (rank == candidate.entityOrders.end() || rank->second != e.entityOrder || candidate.at(e.pos).type != e.type) throw std::invalid_argument("运行事件的方块实体顺序不一致");
            }
            if (e.phase == 1 && ((e.data & 3u) > 2 || (e.data >> 2) > 5)) throw std::invalid_argument("无效活塞方块事件");
            if (e.phase == 3 && (e.type != registry[registry.state("tripwire")].type || e.data != 0 || e.entityOrder != 0)) throw std::invalid_argument("无效的环境接触事件");
            if ((e.phase != 0 && e.tick < candidate.currentTick) || e.type >= registry.typeCount() || e.priority < -3 || e.priority > 3 || e.phase > 3 || e.order >= candidate.nextOrder || !usedOrders.insert(e.order).second) throw std::invalid_argument("无效的运行队列");
            if (e.phase == 0) {
                if (!data.contains("blockTickState") && e.tick <= candidate.currentTick) throw std::invalid_argument("旧版快照缺少本刻计划事件批次，请使用电路工程重新开始运行");
                if (!candidate.blockTicks.schedule(e)) throw std::invalid_argument("重复方块计划刻");
            } else {
                if (!candidate.scheduledKeys.insert({e.pos, e.type, e.phase, e.data}).second) throw std::invalid_argument("重复运行事件");
                candidate.scheduled.push(e);
            }
        }
        auto earliest = candidate.currentTick + 1;
        std::deque<ScheduledEvent> batch;
        if (data.contains("blockTickState")) {
            earliest = data.at("blockTickState").at("earliestCollection");
            if (earliest != candidate.currentTick + 1) throw std::invalid_argument("本刻计划事件批次时间不一致");
            for (const auto& row : data.at("blockTickState").at("batch")) {
                auto e = readEvent(row);
                if (e.type >= registry.typeCount() || e.order >= candidate.nextOrder || !usedOrders.insert(e.order).second) throw std::invalid_argument("本刻计划事件批次顺序无效");
                batch.push_back(e);
            }
        }
        candidate.blockTicks.restoreBatch(earliest, batch);
        for (const auto& row : data.value("hoppers", Json::array())) {
            auto pos = row.at("pos").get<BlockPos>();
            HopperState hopper{row.at("readyAt"), row.at("firstTick"), row.at("wakeAt"), row.at("generation")};
            if (candidate.at(pos).device != Device::hopper || !candidate.entityOrders.contains(pos) || hopper.generation >= candidate.nextOrder || (hopper.wakeAt != UINT64_MAX && hopper.wakeAt < candidate.currentTick) || !candidate.hoppers.emplace(pos, hopper).second) throw std::invalid_argument("无效漏斗运行状态");
            if (hopper.wakeAt != UINT64_MAX && !candidate.scheduledKeys.contains({pos, candidate.at(pos).type, 2, hopper.generation})) throw std::invalid_argument("快照缺少漏斗唤醒事件");
        }
        auto hopperQueue = candidate.scheduled;
        while (!hopperQueue.empty()) {
            const auto event = hopperQueue.top(); hopperQueue.pop();
            if (event.phase != 2 || candidate.at(event.pos).device != Device::hopper) continue;
            auto hopper = candidate.hoppers.find(event.pos);
            if (hopper == candidate.hoppers.end() || event.data != hopper->second.generation || event.tick != hopper->second.wakeAt) throw std::invalid_argument("漏斗冷却与唤醒队列不一致");
        }
        for (const auto& row : data.value("motions", Json::array())) {
            auto p = row.at("pos").get<BlockPos>(); auto direction = row.at("facing").get<unsigned>(); auto moved = row.at("movedState").get<StateId>();
            if (direction > 5 || moved >= registry.stateCount() || candidate.at(p).device != Device::movingPiston || row.at("progress").get<unsigned>() > 2 || row.at("previousProgress").get<unsigned>() > 2) throw std::invalid_argument("无效活塞运动状态");
            candidate.motions[p] = {moved, static_cast<Direction>(direction), row.at("extending"), row.at("source"), row.at("progress"), row.at("previousProgress"), row.at("lastTicked"), row.at("generation")};
        }
        for (const auto& cell : candidate.world.cells()) if (registry[cell.state].device == Device::movingPiston && !candidate.motions.contains(cell.pos)) throw std::invalid_argument("运行快照缺少活塞运动数据");
        for (const auto& cell : candidate.world.cells()) if (registry[cell.state].device == Device::hopper && !candidate.hoppers.contains(cell.pos)) throw std::invalid_argument("运行快照缺少漏斗数据");
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
    swap(world, candidate.world); swap(runtime, candidate.runtime); swap(motions, candidate.motions); swap(scheduled, candidate.scheduled); swap(scheduledKeys, candidate.scheduledKeys);
    swap(blockTicks, candidate.blockTicks);
    swap(hoppers, candidate.hoppers); swap(entityOrders, candidate.entityOrders); nextEntityOrder = candidate.nextEntityOrder;
    swap(recentTorchToggles, candidate.recentTorchToggles); swap(torchToggleCounts, candidate.torchToggleCounts);
    swap(probes, candidate.probes); swap(probeDependencies, candidate.probeDependencies); swap(trace, candidate.trace);
    currentTick = candidate.currentTick; nextOrder = candidate.nextOrder; sequence = candidate.sequence; nextProbeId = candidate.nextProbeId; traceDropped = candidate.traceDropped;
    statistics = {}; changes.clear(); breakRequested = false; faulted = false; pauseReason.clear(); ++revision;
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
