#include "simulator/simulator.hpp"
#include "simulator/projectIo.hpp"
#include <sstream>
#include <cctype>
#include <cmath>

namespace simulator {
namespace {
Json wakeTimeJson(Tick value) { return value==UINT64_MAX?Json(nullptr):Json(value); }
Tick readWakeTime(const Json& value) {
    if(value.is_null())return UINT64_MAX;
    if(!value.is_number_integer() || (!value.is_number_unsigned() && value.get<std::int64_t>()<0))throw std::invalid_argument("无效唤醒时刻");
    // Accept exact old native snapshots, but never a rounded JavaScript float.
    return value.get<Tick>();
}
Json eventJson(const ScheduledEvent& e) {
    return {{"tick", e.tick}, {"priority", e.priority}, {"order", e.order}, {"pos", e.pos}, {"type", e.type}, {"phase", e.phase}, {"data", e.data}, {"entityOrder", e.entityOrder}};
}
ScheduledEvent readEvent(const Json& row) {
    return {row.at("tick"), row.at("priority"), row.at("order"), row.at("pos").get<BlockPos>(), row.at("type"), row.value("phase", std::uint8_t{0}), row.value("data", std::uint64_t{0}), row.value("entityOrder", std::uint64_t{0})};
}
std::uint64_t unsignedDecimal(const Json& value) {
    if (!value.is_string()) throw std::invalid_argument("随机种子和取样计数必须为十进制字符串");
    const auto text = value.get<std::string>();
    if (text.empty() || !std::all_of(text.begin(), text.end(), [](unsigned char c) { return c >= '0' && c <= '9'; })) throw std::invalid_argument("无效的无符号十进制字符串");
    return std::stoull(text);
}
Json vibrationJson(const VibrationInfo& value,const BlockRegistry& registry) {
    Json result{{"event",registry.gameEvent(value.event).name},{"origin",value.origin},{"distance",value.distance},
        {"source",{{"spectator",value.context.spectator},{"sneaking",value.context.sneaking},{"dampensVibrations",value.context.dampens}}}};
    if(value.context.affectedState!=UINT32_MAX) {auto block=registry.describe(value.context.affectedState);block.erase("stateId");result["affectedBlock"]=block;}
    return result;
}
VibrationInfo readVibration(const Json& row,const BlockRegistry& registry,BlockPos destination) {
    VibrationInfo value;value.event=registry.gameEventId(row.at("event"));
    const auto& origin=row.at("origin");
    if(!origin.is_array() || origin.size()!=3)throw std::invalid_argument("无效振动来源位置");
    for(std::size_t i=0;i<3;++i) {
        if(!origin[i].is_number())throw std::invalid_argument("无效振动来源坐标");
        value.origin[i]=origin[i];if(!std::isfinite(value.origin[i]) || value.origin[i]<-29999984 || value.origin[i]>29999985)throw std::invalid_argument("振动来源坐标越界");
    }
    const double x=value.origin[0]-(destination.x+.5),y=value.origin[1]-(destination.y+.5),z=value.origin[2]-(destination.z+.5);
    value.distance=row.at("distance");
    if(!std::isfinite(value.distance) || value.distance!=static_cast<float>(std::sqrt(x*x+y*y+z*z)) || value.distance>18)throw std::invalid_argument("振动传播距离不一致");
    const auto& source=row.at("source");
    value.context.spectator=source.at("spectator").get<bool>();value.context.sneaking=source.at("sneaking").get<bool>();value.context.dampens=source.at("dampensVibrations").get<bool>();
    if(row.contains("affectedBlock")) {const auto& block=row.at("affectedBlock");value.context.affectedState=registry.state(block.at("name"),block.at("properties"));}
    if(!registry.gameEvent(value.event).frequency)throw std::invalid_argument("无效振动事件频率");
    return value;
}
}
std::unique_ptr<Simulator> Simulator::clone() const {
    auto result = std::make_unique<Simulator>(registry); result->restore(*this); return result;
}
void Simulator::restore(const Simulator& snapshot) {
    if (&registry != &snapshot.registry) throw std::invalid_argument("运行快照的注册表不匹配");
    world = snapshot.world; runtime = snapshot.runtime; motions = snapshot.motions; scheduled = snapshot.scheduled; scheduledKeys = snapshot.scheduledKeys;
    chunkStates = snapshot.chunkStates;
    worldRandom = snapshot.worldRandom; randomSeed = snapshot.randomSeed;
    environmentActions = snapshot.environmentActions; pendingActionIds = snapshot.pendingActionIds; nextActionId = snapshot.nextActionId; actionsDropped = snapshot.actionsDropped;
    blockTicks = snapshot.blockTicks;
    hoppers = snapshot.hoppers; cartCells = snapshot.cartCells; entityCells = snapshot.entityCells; entityOrders = snapshot.entityOrders; nextEntityOrder = snapshot.nextEntityOrder;
    sensors = snapshot.sensors; sensorSections = snapshot.sensorSections;
    jukeboxes=snapshot.jukeboxes;
    recentTorchToggles = snapshot.recentTorchToggles; torchToggleCounts = snapshot.torchToggleCounts;
    probes = snapshot.probes; probeDependencies = snapshot.probeDependencies; trace = snapshot.trace;
    currentTick = snapshot.currentTick; nextOrder = snapshot.nextOrder; sequence = snapshot.sequence; nextProbeId = snapshot.nextProbeId;
    traceDropped = snapshot.traceDropped; traceCapacity = snapshot.traceCapacity; traceAtomicReserve = snapshot.traceAtomicReserve; updateBudget = snapshot.updateBudget; statistics = snapshot.statistics;
    if (retainedTrace) retainedTrace = traceDropped;
    breakRequested = snapshot.breakRequested; faulted = snapshot.faulted; pauseReason = snapshot.pauseReason; changes.clear(); ++revision;
}
void Simulator::writeProject(ProjectSink& sink, const std::string& name, bool checkpoint) const {
    if (!checkpoint && !motions.empty()) throw std::invalid_argument("活塞正在运动，请导出运行快照，或等待动作完成后导出电路");
    if (!checkpoint && hasPendingActions()) throw std::invalid_argument("仍有待处理的外部动作，请导出运行快照，或先确认环境反馈");
    if(!checkpoint) for(const auto& [pos,sensor]:sensors)
        if(sensor.candidate || sensor.current || registry.property(world.get(pos),"sculk_sensor_phase")!="inactive")throw std::invalid_argument("感测体正在接收振动或冷却，请保存运行快照，或等待空闲后导出电路");
    if(!checkpoint) for(const auto& event:scheduledKeys)
        if(event.phase==1 && event.type==registry[registry.state("note_block")].type)throw std::invalid_argument("音符盒等待演奏，请保存运行快照，或执行方块事件后导出电路");
    if(!checkpoint)for(const auto& [pos,data]:runtime)if(at(pos).device==Device::bell && data.values.value("ringing",false))throw std::invalid_argument("钟仍在摆动，请保存运行快照或等待停止");
    if(!checkpoint)for(const auto& [pos,player]:jukeboxes){(void)player;if((stackAt({pos,0}).count>0)!=(registry.property(world.get(pos),"has_record")=="true"))throw std::invalid_argument("唱片标志与库存不一致，请保存运行快照");}
    Json data{{"format", "verimc.simulator"}, {"formatVersion", 1}, {"minecraftVersion", "26.2"}, {"edition", "java"}, {"kind", checkpoint ? "checkpoint" : "circuit"}, {"name", name}};
    // loadedRegionOnly 仍然为真：世界不会自动加载区块，只有显式声明的区域存在。
    // 每个区块的 ticking 状态由下面的 chunkStates 表达。
    data["profile"] = {{"experimentalRedstone", false}, {"naturalRandomTicks", false}, {"loadedRegionOnly", true}};
    // 只有出现非默认区块状态时才写这一段，默认整张图 entityTicking 的工程文件保持原样。
    if (!chunkStates.empty()) data["chunkStates"] = chunkStatesJson();
    data["randomSource"] = {{"algorithm", "javaLegacy48"}, {"seed", std::to_string(randomSeed)}};
    data["nextEntityOrder"] = nextEntityOrder;
    if (checkpoint) {
        data["randomSource"]["state"] = worldRandom.state(); data["randomSource"]["draws"] = std::to_string(worldRandom.drawCount());
        data["nextActionId"] = nextActionId; data["actionsDropped"] = actionsDropped; data["faulted"] = faulted;
        data["tick"] = currentTick; data["nextOrder"] = nextOrder; data["sequence"] = sequence; data["nextProbeId"] = nextProbeId;
        data["blockTickState"] = {{"earliestCollection", blockTicks.earliestTick()}};
        data["traceDropped"] = traceDropped;
        data["loadSettings"] = {{"traceCapacity", traceCapacity}, {"traceAtomicReserve", traceAtomicReserve}, {"updateBudget", updateBudget}};
    }
    std::vector<StateId> additional;
    if (checkpoint) {
        for (const auto& [pos, motion] : motions) { (void)pos; additional.push_back(motion.movedState); }
        for (const auto& [pos, sensor] : sensors) {
            (void)pos;
            for (const auto* value : {&sensor.candidate, &sensor.current})
                if (*value && (**value).context.affectedState != UINT32_MAX) additional.push_back((**value).context.affectedState);
        }
    }
    sink.begin(data, world, additional);
    auto table = [&](const char* name, const auto& emit) { sink.check(); sink.beginTable(name); emit(); sink.endTable(); };
    // Only position indices are sorted, never a whole table of JSON records.
    auto positions = [](const auto& entries, bool spatial = false) {
        std::vector<BlockPos> result; result.reserve(entries.size());
        for (const auto& [pos, value] : entries) { (void)value; result.push_back(pos); }
        auto spatialKey = [](BlockPos pos) { return std::tuple(pos.x >> 4, pos.y >> 4, pos.z >> 4,
            (static_cast<unsigned>(pos.x) & 15u) | ((static_cast<unsigned>(pos.z) & 15u) << 4) | ((static_cast<unsigned>(pos.y) & 15u) << 8)); };
        std::sort(result.begin(), result.end(), [&](BlockPos a, BlockPos b) { return spatial ? spatialKey(a) < spatialKey(b) : a < b; }); return result;
    };
    table("probes", [&] {
        for (const auto& probe : probes) sink.row({{"id", probe.id}, {"pos", probe.pos}, {"name", probe.name}, {"mode", probe.mode}, {"direction", directionNames[static_cast<unsigned>(probe.direction)]}, {"trigger", probe.trigger}, {"triggerValue", probe.triggerValue}, {"lastValue", probe.lastValue}});
    });
    table("blockData", [&] {
        for (auto pos : positions(runtime, true)) {
            const auto& state = runtime.at(pos); Json row{{"pos", pos}, {"values", state.values}};
            if (!checkpoint && at(pos).device == Device::noteBlock) { row["values"].erase("lastPlayed"); row["values"].erase("playCount"); }
            if (!checkpoint && at(pos).device == Device::bell) row["values"] = Json::object();
            if (inventorySize(world.get(pos))) row["inventory"] = inventoryJson(pos, false);
            if (checkpoint) row["output"] = state.output;
            sink.row(std::move(row));
        }
    });
    table("entityOrder", [&] {
        std::vector<std::pair<std::uint64_t, BlockPos>> ordered;
        for (const auto& [pos, rank] : entityOrders) ordered.emplace_back(rank, pos);
        std::sort(ordered.begin(), ordered.end());
        for (const auto& [rank, pos] : ordered) sink.row({{"pos", pos}, {"order", rank}});
    });
    if (checkpoint) {
        table("environmentActions", [&] { for (const auto& action : environmentActions) sink.row(action); });
        table("torchToggles", [&] { for (const auto& toggle : recentTorchToggles) sink.row({{"pos", toggle.pos}, {"tick", toggle.tick}}); });
        table("events", [&] {
            auto events = blockTicks.queuedEvents(); auto queue = scheduled;
            while (!queue.empty()) { auto event = queue.top(); queue.pop(); if (scheduledKeys.contains({event.pos, event.type, event.phase, event.data})) events.push_back(event); }
            std::sort(events.begin(), events.end(), [](const auto& a, const auto& b) { return a.key() < b.key(); });
            for (const auto& event : events) sink.row(eventJson(event));
        });
        table("blockTickBatch", [&] { for (const auto& event : blockTicks.batchEvents()) sink.row(eventJson(event)); });
        table("hoppers", [&] {
            for (auto pos : positions(hoppers)) { const auto& h = hoppers.at(pos); sink.row({{"pos", pos}, {"readyAt", h.readyAt}, {"firstTick", h.firstTick}, {"wakeAt", wakeTimeJson(h.wakeAt)}, {"generation", h.generation}}); }
        });
        table("sensors", [&] {
            for (auto pos : positions(sensors)) {
                const auto& sensor = sensors.at(pos); Json row{{"pos", pos}, {"candidateTick", sensor.candidateTick}, {"wakeAt", wakeTimeJson(sensor.wakeAt)}, {"remaining", sensor.remaining}, {"generation", sensor.generation}};
                if (sensor.candidate) row["candidate"] = vibrationJson(*sensor.candidate, registry);
                if (sensor.current) row["current"] = vibrationJson(*sensor.current, registry);
                sink.row(std::move(row));
            }
        });
        table("jukeboxes", [&] {
            for (auto pos : positions(jukeboxes)) { const auto& player = jukeboxes.at(pos); sink.row({{"pos", pos}, {"song", player.song < 0 ? Json(nullptr) : Json(registry.song(player.song).name)}, {"elapsed", player.elapsed}, {"firstTick", player.firstTick}, {"wakeAt", wakeTimeJson(player.wakeAt)}, {"generation", player.generation}}); }
        });
        table("motions", [&] {
            for (auto pos : positions(motions)) { const auto& m = motions.at(pos); sink.row({{"pos", pos}, {"movedState", m.movedState}, {"facing", static_cast<unsigned>(m.facing)}, {"extending", m.extending}, {"source", m.source}, {"progress", m.progress}, {"previousProgress", m.previousProgress}, {"lastTicked", m.lastTicked}, {"generation", m.generation}}); }
        });
        table("trace", [&] { for (const auto& e : trace) sink.row(Json::array({e.probeId, e.tick, e.sequence, e.value})); });
    }
    sink.finish();
}
namespace {
class JsonProjectSink final : public ProjectSink {
    const BlockRegistry& registry;
    std::string table;
    std::function<void()> checker;
public:
    Json data;
    JsonProjectSink(const BlockRegistry& value, std::function<void()> hook) : registry(value), checker(std::move(hook)) {}
    void check() const override { if (checker) checker(); }
    void begin(const Json& metadata, const World& world, std::span<const StateId>) override {
        data = metadata; data.erase("loadSettings"); data["blocks"] = Json::array();
        for (const auto& cell : world.cells()) { check(); auto block = registry.describe(cell.state); block.erase("stateId"); block["pos"] = cell.pos; data["blocks"].push_back(std::move(block)); }
    }
    void beginTable(const std::string& value) override { table = value; data[table] = Json::array(); }
    void row(Json value) override { check(); data[table].push_back(std::move(value)); }
    void endTable() override {
        if (table == "blockData") std::sort(data[table].begin(), data[table].end(), [](const Json& a, const Json& b) { return a.at("pos") < b.at("pos"); });
    }
    void finish() override { if (data.contains("blockTickBatch")) { data["blockTickState"]["batch"] = std::move(data["blockTickBatch"]); data.erase("blockTickBatch"); } }
};
class JsonProjectSource final : public ProjectSource {
    const Json& data;
    const BlockRegistry& registry;
    std::function<void()> checker;
public:
    JsonProjectSource(const Json& value, const BlockRegistry& blocks, std::function<void()> hook) : data(value), registry(blocks), checker(std::move(hook)) {}
    void check() const override { if (checker) checker(); }
    const Json& metadata() const override { return data; }
    void loadWorld(World& world) override {
        const auto& blocks = data.at("blocks");
        if (!blocks.is_array() || blocks.size() > 2000000) throw std::invalid_argument("工程超过 200 万方块限制或布局不是数组");
        std::unordered_set<BlockPos, PosHash> occupied;
        for (const auto& row : blocks) {
            check();
            auto pos = row.at("pos").get<BlockPos>();
            if (!occupied.insert(pos).second) throw std::invalid_argument("工程包含重复坐标");
            world.set(pos, registry.state(row.at("name"), row.at("properties")));
            if (world.storageBytes() > 1024ULL * 1024 * 1024) throw std::invalid_argument("工程稀疏分区超过内存预算");
        }
    }
    ProjectRows rows(const std::string& table) override {
        const Json* array = nullptr;
        if (table == "blockTickBatch") { if (data.contains("blockTickState")) array = &data.at("blockTickState").at("batch"); }
        else if (data.contains(table)) array = &data.at(table);
        if (array && !array->is_array()) throw std::invalid_argument("工程表必须为数组：" + table);
        return ProjectRows([this, array, index = std::size_t{0}](Json& value) mutable { check(); if (!array || index == array->size()) return false; value = array->at(index++); return true; });
    }
};
}
Json Simulator::saveProject(const std::string& name, bool checkpoint, const std::function<void()>& check) const {
    JsonProjectSink sink(registry, check); writeProject(sink, name, checkpoint); return std::move(sink.data);
}
void Simulator::loadProject(const Json& data, const std::function<void()>& check) {
    JsonProjectSource source(data, registry, check); loadProject(source);
}
void Simulator::loadProject(ProjectSource& source) {
    const auto& data = source.metadata();
    if (data.value("faulted", false)) throw std::invalid_argument("此记录来自中止的执行，仅供检查，不能作为可运行快照加载");
    if (data.at("format") != "verimc.simulator" || data.at("formatVersion") != 1 || data.at("minecraftVersion") != "26.2" || data.at("edition") != "java") throw std::invalid_argument("工程格式或 Minecraft 版本不匹配");
    const auto& profile = data.at("profile");
    if (profile.at("experimentalRedstone") != false || profile.at("naturalRandomTicks") != false || profile.at("loadedRegionOnly") != true) throw std::invalid_argument("工程要求尚未支持的仿真规则");
    bool checkpoint = data.at("kind") == "checkpoint";
    if (!checkpoint && data.at("kind") != "circuit") throw std::invalid_argument("未知工程类型");
    Simulator candidate(registry); candidate.traceCapacity = traceCapacity; candidate.traceAtomicReserve = traceAtomicReserve; candidate.updateBudget = updateBudget;
    if (checkpoint && source.restoreBudgets()) {
        candidate.traceCapacity = data.at("loadSettings").at("traceCapacity");
        candidate.traceAtomicReserve = data.at("loadSettings").at("traceAtomicReserve");
        candidate.updateBudget = data.at("loadSettings").at("updateBudget");
    }
    if (data.contains("randomSource")) {
        const auto& random = data.at("randomSource");
        if (random.at("algorithm") != "javaLegacy48") throw std::invalid_argument("尚未支持此随机算法");
        candidate.setRandomSeed(unsignedDecimal(random.at("seed")));
        if (checkpoint) {
            const auto& state = random.at("state");
            if (!state.is_number_integer() || state < 0 || state > ((1ULL << 48) - 1)) throw std::invalid_argument("无效随机源内部状态");
            candidate.worldRandom.restore(state, unsignedDecimal(random.at("draws")));
        }
    }
    source.loadWorld(candidate.world);
    std::unordered_set<StateId> checkedStates;
    std::size_t checkedCells = 0;
    candidate.world.forEachCell([&](Cell cell) {
        if ((++checkedCells & 4095) == 0) source.check();
        if (!checkedStates.insert(cell.state).second) return;
        const auto id = cell.state;
        if (registry.type(id).supportLevel == "unimplemented") throw std::invalid_argument("工程包含尚未支持的器件：" + registry.type(id).name);
        if (!checkpoint && registry[id].device == Device::movingPiston) throw std::invalid_argument("运动中的活塞需要包含内部状态的运行快照");
        if (!checkpoint && isSensor(registry[id].device) && (registry.property(id,"sculk_sensor_phase") != "inactive" || registry[id].power)) throw std::invalid_argument("非空闲感测体需要运行快照");
    });
    if (data.contains("chunkStates")) {
        const auto& rows = data.at("chunkStates");
        if (!rows.is_array() || rows.size() > 65536) throw std::invalid_argument("无效区块状态表");
        static const std::map<std::string, Simulator::ChunkState> names{
            {"unloaded", Simulator::ChunkState::unloaded}, {"loaded", Simulator::ChunkState::loaded},
            {"blockTicking", Simulator::ChunkState::blockTicking}, {"entityTicking", Simulator::ChunkState::entityTicking}};
        for (const auto& row : rows) {
            if (!row.is_object() || row.size() != 3 || !row.contains("chunk") || !row.contains("state") || !row.at("stalledSince").is_number_unsigned())
                throw std::invalid_argument("无效区块状态记录");
            const auto& chunk = row.at("chunk");
            if (!chunk.is_array() || chunk.size() != 2 || !chunk.at(0).is_number_integer() || !chunk.at(1).is_number_integer())
                throw std::invalid_argument("无效区块坐标");
            auto found = names.find(row.at("state").get<std::string>());
            if (found == names.end()) throw std::invalid_argument("无效区块状态");
            if (chunk.at(0) < INT32_MIN / 16 || chunk.at(0) > INT32_MAX / 16 || chunk.at(1) < INT32_MIN / 16 || chunk.at(1) > INT32_MAX / 16)
                throw std::invalid_argument("区块坐标越界");
            const auto x = chunk.at(0).get<int>(), z = chunk.at(1).get<int>();
            // Restore the table atomically; validating partial rows would treat
            // not-yet-restored neighbours as default entity-ticking chunks.
            if (!candidate.chunkStates.emplace(BlockPos{static_cast<int>(x),0,static_cast<int>(z)},
                    Simulator::ChunkRecord{found->second,row.at("stalledSince").get<Tick>()}).second)
                throw std::invalid_argument("重复区块状态");
        }
        for (const auto& [chunk, record] : candidate.chunkStates) {
            if (record.state != Simulator::ChunkState::unloaded) continue;
            for (int dx=-1;dx<=1;++dx) for (int dz=-1;dz<=1;++dz) {
                const auto neighbor=candidate.chunkStates.find({chunk.x+dx,0,chunk.z+dz});
                if (neighbor==candidate.chunkStates.end() || neighbor->second.state>=Simulator::ChunkState::blockTicking)
                    throw std::invalid_argument("可 ticking 的区块周围八格不能是未加载区块");
            }
        }
    }
    std::unordered_set<std::uint64_t> usedRanks;
    candidate.nextEntityOrder = data.value("nextEntityOrder", std::uint64_t{0});
    for (const auto& row : source.rows("entityOrder")) {
        auto pos = row.at("pos").get<BlockPos>(); auto rank = row.at("order").get<std::uint64_t>();
        auto device = candidate.at(pos).device;
        if ((device != Device::hopper && device != Device::daylight && device != Device::movingPiston && device != Device::bell && device!=Device::jukebox && !isSensor(device)) || rank >= candidate.nextEntityOrder || !usedRanks.insert(rank).second || !candidate.entityOrders.emplace(pos, rank).second) throw std::invalid_argument("无效方块实体执行顺序");
    }
    for (const auto& row : source.rows("blockData")) {
        auto p = row.at("pos").get<BlockPos>();
        // 容器实体（运输/漏斗矿车）是声明在格子里的实体，通常停在空气格上，
        // 因此只含 containerEntities 的记录允许没有方块；其余器件数据仍必须有方块承载。
        bool entitiesOnly = row.at("values").is_object() && row.at("values").contains("containerEntities")
            && row.at("values").size() == 1 && !row.contains("inventory") && (!row.contains("output") || row.at("output") == 0);
        if (candidate.world.get(p) == 0 && !entitiesOnly) throw std::invalid_argument("器件数据对应位置没有方块");
        auto& state = candidate.runtime[p]; state.values = row.at("values");
        if (checkpoint) {
            state.output = row.at("output");
            if (!data.contains("torchToggles")) for (auto tick : row.value("torchToggles", std::vector<Tick>{})) candidate.recentTorchToggles.push_back({p, tick});
        }
        if (row.contains("inventory")) candidate.setInventory(p, row.at("inventory"), false, false);
        if (candidate.at(p).device == Device::detectorRail) state.values["carts"] = candidate.normalizeCarts(state.values.value("carts", Json::array()));
        candidate.validateRuntime(p);
    }
    // 容器实体索引（entityCells / cartCells）完全由 containerEntities 推导，不进文件；
    // 这里在 blockData 读完之后重建，旧工程与旧快照因此原样可读、默认行为不变。
    candidate.rebuildEntityCells();
    if (checkpoint) {
        candidate.currentTick = data.at("tick"); candidate.nextOrder = data.at("nextOrder"); candidate.sequence = data.at("sequence");
        if (candidate.currentTick == UINT64_MAX) throw std::invalid_argument("仿真时间超出范围");
        if (data.contains("torchToggles")) for (const auto& row : source.rows("torchToggles")) candidate.recentTorchToggles.push_back({row.at("pos").get<BlockPos>(), row.at("tick").get<Tick>()});
        std::stable_sort(candidate.recentTorchToggles.begin(), candidate.recentTorchToggles.end(), [](const auto& a, const auto& b) { return a.tick < b.tick; });
        for (const auto& toggle : candidate.recentTorchToggles) {
            if (toggle.tick > candidate.currentTick) throw std::invalid_argument("火把历史包含未来事件");
            ++candidate.torchToggleCounts[toggle.pos];
        }
        std::size_t eventCount = 0;
        std::unordered_set<std::uint64_t> usedOrders;
        for (const auto& row : source.rows("events")) {
            if (++eventCount > 2000000) throw std::invalid_argument("工程计划事件过多");
            auto e = readEvent(row);
            if (e.phase == 2) {
                if (!row.contains("entityOrder")) throw std::invalid_argument("旧版快照缺少方块实体执行顺序，请使用电路工程重新开始运行");
                e.entityOrder = row.at("entityOrder");
                auto rank = candidate.entityOrders.find(e.pos);
                if (rank == candidate.entityOrders.end() || rank->second != e.entityOrder || candidate.at(e.pos).type != e.type) throw std::invalid_argument("运行事件的方块实体顺序不一致");
            }
            if (e.phase == 1 && ((e.data & 3u) > 2 || (e.data >> 2) > 5)) throw std::invalid_argument("无效活塞方块事件");
            if(e.phase==1 && e.type==registry[registry.state("note_block")].type && e.data!=0)throw std::invalid_argument("无效音符盒方块事件");
            if(e.phase==1 && e.type==registry[registry.state("bell")].type && ((e.data&3u)!=1 || (e.data>>2)<2))throw std::invalid_argument("无效钟方块事件");
            // 漏斗矿车的吸取事件挂在矿车所在的格子上，那一格通常是空气，类型固定记 0。
            // 这里**不**要求那一格现在还有矿车：撤走矿车不撤销已排的事件（撤销会让
            // 同一个键被重新插入而排出第二份事件），过期的那一份触发时直接空跑。
            const bool cartSuction = e.phase == 3 && e.data == Simulator::cartSuctionEvent;
            if (cartSuction && (e.type != 0 || e.entityOrder != 0)) throw std::invalid_argument("无效的漏斗矿车吸取事件");
            if (e.phase == 3 && !cartSuction && (candidate.at(e.pos).type != e.type || (candidate.at(e.pos).device != Device::tripwire && candidate.at(e.pos).device != Device::button && candidate.at(e.pos).device != Device::hopper) || e.data != 0 || e.entityOrder != 0)) throw std::invalid_argument("无效的环境接触事件");
            // 区块不可 ticking 时事件合法地停在过去，等区块恢复才执行。
            const bool tickableChunk = e.phase == 3 ? candidate.chunkEntityTicking(e.pos) : candidate.chunkBlockTicking(e.pos);
            if ((e.phase != 0 && e.tick < candidate.currentTick && tickableChunk) || e.type >= registry.typeCount() || e.priority < -3 || e.priority > 3 || e.phase > 3 || e.order >= candidate.nextOrder || !usedOrders.insert(e.order).second) throw std::invalid_argument("无效的运行队列");
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
            for (const auto& row : source.rows("blockTickBatch")) {
                auto e = readEvent(row);
                if (e.type >= registry.typeCount() || e.order >= candidate.nextOrder || !usedOrders.insert(e.order).second) throw std::invalid_argument("本刻计划事件批次顺序无效");
                batch.push_back(e);
            }
        }
        candidate.blockTicks.restoreBatch(earliest, batch);
        for (const auto& row : source.rows("hoppers")) {
            auto pos = row.at("pos").get<BlockPos>();
            HopperState hopper{row.at("readyAt"), row.at("firstTick"), readWakeTime(row.at("wakeAt")), row.at("generation")};
            if (candidate.at(pos).device != Device::hopper || !candidate.entityOrders.contains(pos) || hopper.generation >= candidate.nextOrder || (hopper.wakeAt != UINT64_MAX && hopper.wakeAt < candidate.currentTick && candidate.chunkBlockTicking(pos)) || !candidate.hoppers.emplace(pos, hopper).second) throw std::invalid_argument("无效漏斗运行状态");
            if (hopper.wakeAt != UINT64_MAX && !candidate.scheduledKeys.contains({pos, candidate.at(pos).type, 2, hopper.generation})) throw std::invalid_argument("快照缺少漏斗唤醒事件");
        }
        auto hopperQueue = candidate.scheduled;
        while (!hopperQueue.empty()) {
            const auto event = hopperQueue.top(); hopperQueue.pop();
            if (event.phase != 2 || candidate.at(event.pos).device != Device::hopper) continue;
            auto hopper = candidate.hoppers.find(event.pos);
            if (hopper == candidate.hoppers.end() || event.data != hopper->second.generation || event.tick != hopper->second.wakeAt) throw std::invalid_argument("漏斗冷却与唤醒队列不一致");
        }
        for(const auto& row:source.rows("sensors")) {
            auto pos=row.at("pos").get<BlockPos>();SensorState sensor;
            if(!isSensor(candidate.at(pos).device) || !candidate.entityOrders.contains(pos))throw std::invalid_argument("无效感测体运行位置或顺序");
            for(const auto* field:{"candidateTick","remaining","generation"})if(!row.at(field).is_number_integer() || (!row.at(field).is_number_unsigned() && row.at(field).get<std::int64_t>()<0))throw std::invalid_argument("无效感测体运行字段");
            if(row.at("remaining").get<std::uint64_t>()>18)throw std::invalid_argument("振动传播剩余时间越界");
            sensor.candidateTick=row.at("candidateTick");sensor.wakeAt=readWakeTime(row.at("wakeAt"));sensor.remaining=row.at("remaining");sensor.generation=row.at("generation");
            if(row.contains("candidate"))sensor.candidate=readVibration(row.at("candidate"),registry,pos);
            if(row.contains("current"))sensor.current=readVibration(row.at("current"),registry,pos);
            for(const auto* pending:{&sensor.candidate,&sensor.current})if(*pending) {
                const auto& value=**pending;const auto& info=registry.gameEvent(value.event);
                const double x=std::floor(value.origin[0])-pos.x,y=std::floor(value.origin[1])-pos.y,z=std::floor(value.origin[2])-pos.z;
                const int radius=candidate.at(pos).device==Device::calibratedSensor?16:8;
                if(x*x+y*y+z*z>radius*radius || !info.listenable || value.context.spectator || value.context.dampens || (value.context.sneaking && info.ignoreSneaking)
                    || (value.context.affectedState!=UINT32_MAX && registry.type(value.context.affectedState).dampensVibrations))throw std::invalid_argument("无效感测体待接收事件");
            }
            const bool busy=sensor.candidate.has_value() || sensor.current.has_value();
            // 相邻区块不全 ticking 时原版 receiveVibration 直接返回 false：current 保留，
            // travelTime 已减到 0 并每刻重试。这种「卡住待投递」状态下 remaining==0 合法。
            const bool stuck=sensor.current && sensor.remaining==0 && !candidate.adjacentChunksTicking(pos);
            if(sensor.remaining<0 || sensor.remaining>18 || sensor.candidateTick>candidate.currentTick || (sensor.candidate && sensor.current) || (!sensor.current && sensor.remaining!=0)
                || (sensor.current && !stuck && (sensor.remaining==0 || sensor.remaining>=static_cast<int>(std::floor(sensor.current->distance))))
                // 停摆区块里最后一批事件被放回当刻、随后空闲推进跳到目标刻，wakeAt 因此
                // 落在 currentTick 之前。与漏斗同样为这种状态开口子。
                || (busy && ((sensor.wakeAt<candidate.currentTick && candidate.chunkBlockTicking(pos)) || sensor.wakeAt==UINT64_MAX || sensor.generation>=candidate.nextOrder)) || (!busy && sensor.wakeAt!=UINT64_MAX))throw std::invalid_argument("感测体传播与唤醒状态不一致");
            if(busy && !candidate.scheduledKeys.contains({pos,candidate.at(pos).type,2,sensor.generation}))throw std::invalid_argument("快照缺少感测体唤醒事件");
            if(!candidate.sensors.emplace(pos,sensor).second)throw std::invalid_argument("重复感测体运行数据");
        }
        auto sensorQueue=candidate.scheduled;
        while(!sensorQueue.empty()) {
            const auto event=sensorQueue.top();sensorQueue.pop();if(event.phase!=2 || !isSensor(candidate.at(event.pos).device))continue;
            const auto found=candidate.sensors.find(event.pos);
            if(found==candidate.sensors.end() || found->second.wakeAt!=event.tick || found->second.generation!=event.data)throw std::invalid_argument("感测体与运行事件不一致");
        }
        candidate.rebuildSensorIndex();
        for(const auto& row:source.rows("jukeboxes")) {
            auto pos=row.at("pos").get<BlockPos>();JukeboxState player;
            const bool hasRecord=registry.property(candidate.world.get(pos),"has_record")=="true";
            if(candidate.at(pos).device!=Device::jukebox || candidate.entityOrders.contains(pos)!=hasRecord)throw std::invalid_argument("无效唱片机位置或执行顺序");
            for(const auto* field:{"elapsed","firstTick","generation"})if(!row.at(field).is_number_integer() || (!row.at(field).is_number_unsigned() && row.at(field).get<std::int64_t>()<0))throw std::invalid_argument("无效唱片机运行字段");
            player.song=row.at("song").is_null()?-1:registry.songId(row.at("song"));
            if(row.at("elapsed").get<std::uint64_t>()>(player.song<0?0:registry.song(player.song).lengthTicks+20))throw std::invalid_argument("唱片播放进度超出范围");
            player.elapsed=row.at("elapsed");player.firstTick=row.at("firstTick");player.wakeAt=readWakeTime(row.at("wakeAt"));player.generation=row.at("generation");
            if(player.firstTick>candidate.currentTick+1)throw std::invalid_argument("唱片机首次执行时刻越界");
            const auto stack=candidate.stackAt({pos,0});
            if(player.song>=0 && (!stack.count || registry.item(stack.item).jukeboxSong!=player.song))throw std::invalid_argument("播放曲目与唱片不一致");
            const bool ticking=player.song>=0 && hasRecord;
            // 与漏斗、感测体同理：停摆区块里的唱片机 wakeAt 会落在 currentTick 之前。
            if(ticking!=(player.wakeAt!=UINT64_MAX) || (ticking && ((player.wakeAt<candidate.currentTick && candidate.chunkBlockTicking(pos)) || player.wakeAt<player.firstTick || player.generation>=candidate.nextOrder || !candidate.scheduledKeys.contains({pos,candidate.at(pos).type,2,player.generation}))))throw std::invalid_argument("唱片机播放与队列不一致");
            if(!candidate.jukeboxes.emplace(pos,player).second)throw std::invalid_argument("重复唱片机数据");
        }
        auto jukeboxQueue=candidate.scheduled;
        while(!jukeboxQueue.empty()) {
            const auto event=jukeboxQueue.top();jukeboxQueue.pop();if(event.phase!=2 || candidate.at(event.pos).device!=Device::jukebox)continue;
            auto found=candidate.jukeboxes.find(event.pos);
            if(found==candidate.jukeboxes.end() || found->second.wakeAt!=event.tick || found->second.generation!=event.data)throw std::invalid_argument("唱片机与排期不一致");
        }
        candidate.world.forEachCell([&](Cell cell) { if(registry[cell.state].device==Device::jukebox && !candidate.jukeboxes.contains(cell.pos))throw std::invalid_argument("快照缺少唱片机数据"); });
        auto bellQueue=candidate.scheduled;std::unordered_set<BlockPos,PosHash> queuedBells,pendingBells;
        while(!bellQueue.empty()) {
            const auto event=bellQueue.top();bellQueue.pop();if(candidate.at(event.pos).device!=Device::bell || event.type!=candidate.at(event.pos).type)continue;
            if(event.phase==1)pendingBells.insert(event.pos);
            if(event.phase!=2)continue;
            if(!candidate.runtime.contains(event.pos))throw std::invalid_argument("钟缺少运行数据");
            const auto& values=candidate.runtime.at(event.pos).values;
            // 停摆区块里事件被放回当刻，而 bellWakeAt 保持冻结的结束时刻，两者可以不等；
            // 恢复后 finishBell 会按 bellWakeAt 重排，届时又必须相等。
            // 两种合法的不相等：一是区块停摆，事件被放回当刻而 bellWakeAt 保持冻结的结束时刻；
            // 二是区块刚恢复、shiftBlockEntityTimers 已把 bellWakeAt 后移而那个过期事件还没被
            // finishBell 重排——此时事件在过去、结束时刻在现在或将来。其余不相等一律拒绝。
            const bool bellPending=!candidate.chunkBlockTicking(event.pos)
                || (event.tick<=candidate.currentTick && values.at("bellWakeAt").get<Tick>()>=candidate.currentTick);
            if(!values.value("ringing",false) || (!bellPending && values.at("bellWakeAt")!=event.tick) || values.at("bellGeneration")!=event.data || !queuedBells.insert(event.pos).second)throw std::invalid_argument("钟摆动与队列不一致");
        }
        candidate.world.forEachCell([&](Cell cell) { if(registry[cell.state].device==Device::bell) {
            if(!candidate.entityOrders.contains(cell.pos))throw std::invalid_argument("钟缺少方块实体顺序");
            if(!candidate.runtime.contains(cell.pos))return;
            const auto& values=candidate.runtime.at(cell.pos).values;
            if(values.value("ringing",false) && !queuedBells.contains(cell.pos) && !pendingBells.contains(cell.pos))throw std::invalid_argument("钟缺少停止摆动事件");
            if(values.contains("bellWakeAt") && !queuedBells.contains(cell.pos))throw std::invalid_argument("钟的结束时间缺少事件");
        } });
        for (const auto& row : source.rows("motions")) {
            auto p = row.at("pos").get<BlockPos>(); auto direction = row.at("facing").get<unsigned>(); auto moved = row.at("movedState").get<StateId>();
            if (direction > 5 || moved >= registry.stateCount() || candidate.at(p).device != Device::movingPiston || row.at("progress").get<unsigned>() > 2 || row.at("previousProgress").get<unsigned>() > 2) throw std::invalid_argument("无效活塞运动状态");
            candidate.motions[p] = {moved, static_cast<Direction>(direction), row.at("extending"), row.at("source"), row.at("progress"), row.at("previousProgress"), row.at("lastTicked"), row.at("generation")};
        }
        candidate.world.forEachCell([&](Cell cell) {
            if (registry[cell.state].device == Device::movingPiston && !candidate.motions.contains(cell.pos)) throw std::invalid_argument("运行快照缺少活塞运动数据");
            if (registry[cell.state].device == Device::hopper && !candidate.hoppers.contains(cell.pos)) throw std::invalid_argument("运行快照缺少漏斗数据");
            if (isSensor(registry[cell.state].device) && !candidate.sensors.contains(cell.pos)) throw std::invalid_argument("运行快照缺少感测体数据");
        });
    } else {
        candidate.world.forEachCell([&](Cell cell) { if(registry[cell.state].device==Device::jukebox && ((candidate.stackAt({cell.pos,0}).count>0)!=(registry.property(cell.state,"has_record")=="true")))throw std::invalid_argument("电路中的唱片标志与库存不一致"); });
        auto visitInitial = [&](const auto& visit) {
            // Old JSON initialization snapshots the cells before each pass.
            // Retain that behavior using native sections, without a per-block
            // object array or callbacks observing already-mutated cell states.
            World initial(candidate.world);
            initial.forEachCellXyz([&](Cell cell) { if ((++checkedCells & 1023) == 0) source.check(); visit(cell); });
        };
        visitInitial([&](Cell cell) { candidate.onPlace(cell.pos, cell.state, 0); });
        visitInitial([&](Cell cell) { if(registry[cell.state].device==Device::jukebox && candidate.stackAt({cell.pos,0}).count)candidate.updateJukeboxItem(cell.pos); });
        visitInitial([&](Cell cell) { candidate.neighborChanged(cell.pos); });
        // 电路工程不带运行队列，漏斗矿车停在空气格上、不会被 forEachCell 扫到，
        // 只能在这里显式起跑。排序是为了让 nextOrder 的分配与哈希遍历顺序无关。
        std::vector<BlockPos> carts(candidate.cartCells.begin(), candidate.cartCells.end());
        std::sort(carts.begin(), carts.end());
        for (auto pos : carts) candidate.scheduleCartSuction(pos, candidate.currentTick);
    }
    for (const auto& row : source.rows("probes")) {
        auto newId = candidate.addProbe(row.at("pos").get<BlockPos>(), row.at("name"), row.at("mode"), parseDirection(row.at("direction")));
        candidate.configureProbe(newId, row);
        if (checkpoint) { candidate.probes.back().id = row.at("id"); candidate.probes.back().lastValue = row.at("lastValue"); }
    }
    if (checkpoint) {
        candidate.nextProbeId = data.at("nextProbeId"); candidate.rebuildProbeDependencies(); candidate.trace.clear();
        for (const auto& e : source.rows("trace")) {
            if (candidate.trace.size() >= candidate.traceCapacity + candidate.traceAtomicReserve) throw std::invalid_argument("快照采样量超过当前历史容量与安全余量，请增大容量后重试");
            candidate.trace.push_back({e.at(0), e.at(1), e.at(2), e.at(3)});
        }
        candidate.traceDropped = data.at("traceDropped");
    }
    if (checkpoint) {
        Json actionData{{"nextActionId", data.value("nextActionId", Json(1))}, {"actionsDropped", data.value("actionsDropped", Json(0))}, {"environmentActions", Json::array()}};
        for (const auto& row : source.rows("environmentActions")) {
            if (actionData["environmentActions"].size() >= actionCapacity) throw std::invalid_argument("外部动作历史超出预算");
            actionData["environmentActions"].push_back(row);
        }
        candidate.loadActions(actionData);
    }
    source.check();
    exchangeProject(candidate);
}
void Simulator::exchangeProject(Simulator& other) {
    if (&registry != &other.registry) throw std::invalid_argument("工程注册表不匹配");
    using std::swap;
    swap(world, other.world); swap(runtime, other.runtime); swap(motions, other.motions); swap(chunkStates, other.chunkStates);
    swap(scheduled, other.scheduled); swap(scheduledKeys, other.scheduledKeys); swap(blockTicks, other.blockTicks);
    swap(hoppers, other.hoppers); swap(cartCells, other.cartCells); swap(entityCells, other.entityCells); swap(entityOrders, other.entityOrders); swap(nextEntityOrder, other.nextEntityOrder);
    swap(sensors, other.sensors); swap(sensorSections, other.sensorSections); swap(jukeboxes, other.jukeboxes);
    swap(recentTorchToggles, other.recentTorchToggles); swap(torchToggleCounts, other.torchToggleCounts); swap(probes, other.probes);
    swap(probeDependencies, other.probeDependencies); swap(trace, other.trace); swap(currentTick, other.currentTick);
    swap(nextOrder, other.nextOrder); swap(sequence, other.sequence); swap(nextProbeId, other.nextProbeId);
    swap(traceDropped, other.traceDropped); swap(traceCapacity, other.traceCapacity); swap(traceAtomicReserve, other.traceAtomicReserve);
    swap(updateBudget, other.updateBudget); swap(worldRandom, other.worldRandom); swap(randomSeed, other.randomSeed);
    swap(environmentActions, other.environmentActions); swap(pendingActionIds, other.pendingActionIds); swap(nextActionId, other.nextActionId);
    swap(actionsDropped, other.actionsDropped); swap(statistics, other.statistics); swap(breakRequested, other.breakRequested);
    swap(faulted, other.faulted); swap(pauseReason, other.pauseReason);
    if (retainedTrace) retainedTrace = traceDropped;
    other.retainedTrace.reset(); changes.clear(); other.changes.clear(); ++revision;
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
