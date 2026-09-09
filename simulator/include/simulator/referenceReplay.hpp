#pragma once
// 原版差分重放：checkReference 工具与 coreTests 单测**共用这一份实现**。
// 两条入口以前各写了一遍循环，导致有些字段只在其中一边被比较；这里只保留一套语义。
#include "simulator/simulator.hpp"
#include <algorithm>
#include <bit>
#include <fstream>
#include <string>

namespace simulator {

inline Simulator::ChunkState parseChunkState(const std::string& name) {
    if (name == "unloaded") return Simulator::ChunkState::unloaded;
    if (name == "loaded") return Simulator::ChunkState::loaded;
    if (name == "blockTicking") return Simulator::ChunkState::blockTicking;
    if (name == "entityTicking") return Simulator::ChunkState::entityTicking;
    throw std::invalid_argument("unknown chunk state " + name);
}

// fixture 里**显式声明**的外部动作序列（可选字段 expectedActions）。
// 语义：重放因外部动作暂停时，先把实际记录的动作与声明逐项比对（序号、游戏刻、同刻顺序、
// 源坐标、物品、数量、初始位置与速度），**全部相符才**按记录顺序逐条确认——
// 等价于界面上人工逐条「确认已处理」。任何一步不符（出现未声明的动作、序列/数量/初值不匹配、
// 声明了却没有发生）都直接失败；确认动作**不会**顺带清掉其他原因的暂停。
//
// 声明字段：
//   tick        必填，抛出发生的游戏刻
//   source      必填，相对 fixture origin 的方块坐标
//   item/count  必填
//   position/velocity            必填其一形式：十进制双精度（**绝对**坐标，与原版实体坐标一致）
//   positionBits/velocityBits    可选，十六进制 IEEE754 位模式，用于逐位对照
//   index       可选，全局序号（0 起）
//   order       可选，同一刻内的序号（0 起）
//   kind        可选，默认 itemEjected
//
// 注意：确认之后物品飞到哪里**不是**本内核算出来的。抛出点到落点之间的运动是**外部输入**，
// 由 groundItems 外部刺激显式声明；这里既不模拟也不推断物品运动。
class DeclaredActions {
public:
    DeclaredActions(const Json& fixture, BlockPos origin)
        : origin(origin), declared(fixture.contains("expectedActions")),
          list(declared ? fixture.at("expectedActions") : Json::array()) {
        if (declared && !list.is_array()) throw std::runtime_error("expectedActions must be an array");
    }
    // 处理当前的暂停。确认了至少一条动作时返回 true（调用方应当继续推进）。
    bool settle(Simulator& simulation) {
        if (!simulation.hasPendingActions()) return false;
        for (const auto& action : simulation.pendingActionsJson()) confirm(simulation, action);
        if (simulation.hasPendingActions()) throw std::runtime_error("External action feedback left an unresolved action");
        // 其他原因的暂停不得被这套机制吞掉：确认外部动作只清除外部动作那一个理由。
        if (simulation.faulted || !simulation.pauseReason.empty())
            throw std::runtime_error("Replay paused for a reason other than external actions: " + simulation.pauseReason);
        simulation.breakRequested = false;
        return true;
    }
    void finish() const {
        if (next != list.size())
            throw std::runtime_error("Declared external action " + std::to_string(next) + " never happened: " + list.at(next).dump());
    }

private:
    BlockPos origin;
    bool declared;
    Json list;
    std::size_t next{}, sameTick{};
    Tick lastTick{};
    std::uint64_t lastSequence{};
    bool started{};

    void confirm(Simulator& simulation, const Json& action) {
        if (!declared)
            throw std::runtime_error("Replay requires explicit external-action feedback: " + action.dump());
        if (next >= list.size())
            throw std::runtime_error("Undeclared external action: " + action.dump());
        const Json& want = list.at(next);
        auto fail = [&](const std::string& field, const Json& expected, const Json& actual) {
            throw std::runtime_error("External action " + std::to_string(next) + " differs in " + field
                                     + ": expected " + expected.dump() + " got " + actual.dump());
        };
        const Tick tick = action.at("tick").get<Tick>();
        const std::uint64_t sequence = action.at("sequence").get<std::uint64_t>();
        if (started && sequence <= lastSequence)
            fail("record order", Json(lastSequence), Json(sequence));
        const std::size_t order = started && tick == lastTick ? sameTick + 1 : 0;
        if (want.contains("index") && want.at("index").get<std::size_t>() != next)
            fail("index", want.at("index"), Json(next));
        if (want.at("tick") != action.at("tick")) fail("tick", want.at("tick"), action.at("tick"));
        if (want.contains("order") && want.at("order").get<std::size_t>() != order)
            fail("same-tick order", want.at("order"), Json(order));
        const Json kind = want.contains("kind") ? want.at("kind") : Json("itemEjected");
        if (kind != action.at("kind")) fail("kind", kind, action.at("kind"));
        const auto relative = want.at("source").get<BlockPos>();
        const Json source = BlockPos{origin.x + relative.x, origin.y + relative.y, origin.z + relative.z};
        if (source != action.at("source")) fail("source", source, action.at("source"));
        const auto itemId = simulation.registry.itemId(want.at("item"));
        if (!itemId) throw std::runtime_error("Declared external action names an unknown item: " + want.at("item").dump());
        const Json item = simulation.registry.item(itemId).name;
        if (item != action.at("item")) fail("item", item, action.at("item"));
        if (want.at("count") != action.at("count")) fail("count", want.at("count"), action.at("count"));
        // 初始位置与速度必须被声明，否则这条闭环就没有验证抛出的初值。
        for (const char* field : {"position", "velocity"}) {
            const std::string bits = std::string(field) + "Bits";
            if (!want.contains(field) && !want.contains(bits))
                throw std::runtime_error(std::string("Declared external action must state its ") + field);
            if (want.contains(field) && want.at(field) != action.at(field))
                fail(field, want.at(field), action.at(field));
            if (!want.contains(bits)) continue;
            for (std::size_t i = 0; i < 3; ++i) {
                const auto actual = std::bit_cast<std::uint64_t>(action.at(field).at(i).get<double>());
                const auto expected = std::stoull(want.at(bits).at(i).get<std::string>(), nullptr, 16);
                if (expected != actual) fail(bits + "[" + std::to_string(i) + "]", want.at(bits).at(i), Json(action.at(field).at(i)));
            }
        }
        // 比对通过之后才确认，顺序与记录顺序一致。
        simulation.resolveAction(action.at("id").get<std::uint64_t>());
        started = true; lastTick = tick; lastSequence = sequence; sameTick = order; ++next;
    }
};

// 用外部捕获的原版观测逐刻对照本内核。返回 {"status": "match"|"difference", ...}；
// 结构性问题（重放没能走到观测刻、缺少外部反馈声明等）以异常报出。
inline Json replayReferenceFixture(Simulator& simulation, const Json& fixture, const std::string& traceOut = {}) {
    Json result{{"status", "match"}};
    if (!fixture.at("frames").is_array() || fixture.at("frames").empty()
        || !fixture.at("watch").is_array() || fixture.at("watch").empty())
        throw std::runtime_error("Fixture must contain observations");
    const auto& registry = simulation.registry;
    const auto origin = fixture.at("origin").get<BlockPos>();
    const bool lenientInteract = fixture.value("lenientInteract", false);
    // 刻内更新轨迹：只有捕获里带轨迹时才开启，容量与原版侧一致。
    const bool tracing = fixture.contains("updateTrace");
    if (tracing) simulation.updateTraceLimit = fixture.at("updateTraceLimit").get<std::size_t>();
    auto absolute = [&](const Json& value) {
        const auto pos = value.get<BlockPos>();
        return BlockPos{origin.x + pos.x, origin.y + pos.y, origin.z + pos.z};
    };
    DeclaredActions actions(fixture, origin);
    bool differs = false;
    Tick expectedTick = 0;
    const auto& frames = fixture.at("frames");
    for (std::size_t frameIndex = 0; frameIndex < frames.size(); ++frameIndex) {
        const auto& frame = frames.at(frameIndex);
        const Tick tick = frame.at("tick");
        if (tick != expectedTick++) throw std::runtime_error("Capture frames must be contiguous from tick zero");
        for (const char* field : {"states", "analogs", "inventories", "bells", "jukeboxes", "containerEntities"}) {
            if (frame.contains(field) && (!frame.at(field).is_array() || frame.at(field).size() != fixture.at("watch").size()))
                throw std::runtime_error("Observation array length differs from watch list");
        }
        // 随机源对照：两侧在第 0 刻设同一个种子，之后逐帧比较 48 位内部状态。
        if (fixture.contains("randomSeed") && tick == 0) simulation.setRandomSeed(fixture.at("randomSeed").get<std::uint64_t>());
        // 推进到观测刻。中途因外部动作暂停时先比对再确认，然后接着推进；
        // 仿真必须真正到达观测刻且不带任何暂停，静态观察不得掩盖未处理的外部动作。
        while (true) {
            simulation.advanceTo(tick);
            if (!actions.settle(simulation)) break;
        }
        if (simulation.currentTick != tick || simulation.breakRequested || simulation.hasPendingActions())
            throw std::runtime_error("Replay stopped before completing observation tick " + std::to_string(tick));
        simulation.markUpdateTrace(tick);
        for (const auto& command : fixture.at("commands")) if (command.at("tick") == tick) {
            const auto pos = absolute(command.at("pos"));
            // 区块状态是显式输入；原版侧对应的是真实票据，捕获里逐帧记录了它的派生标志。
            if (command.contains("chunkState")) simulation.setChunkState(pos.x >> 4, pos.z >> 4, parseChunkState(command.at("chunkState")));
            else if (command.contains("chunkForced")) {}
            else if (command.contains("placedBy") || command.contains("playerPlace")) simulation.place(pos, command.at("stateId"));
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
        while (actions.settle(simulation)) {}
        if (simulation.breakRequested || simulation.hasPendingActions())
            throw std::runtime_error("Replay requires explicit external-action feedback");
        if (frame.contains("randomState") && !differs) {
            const auto actual = static_cast<std::int64_t>(simulation.randomState());
            if (frame.at("randomState").get<std::int64_t>() != actual) {
                differs = true;
                result["status"] = "difference";
                result["firstDifference"] = {{"tick", tick}, {"field", "randomState"},
                                             {"expected", frame.at("randomState")}, {"actual", actual}};
            }
        }
        if (frame.contains("blockEntityOrder") && !differs) {
            Json actual = simulation.blockEntityOrderJson();
            for (auto& row : actual) { row[0] = row[0].get<int>() - origin.x; row[1] = row[1].get<int>() - origin.y; row[2] = row[2].get<int>() - origin.z; }
            if (frame.at("blockEntityOrder") != actual) {
                differs = true;
                result["status"] = "difference";
                result["firstDifference"] = {{"tick", tick}, {"field", "blockEntityOrder"},
                                             {"expected", frame.at("blockEntityOrder")}, {"actual", actual}};
            }
        }
        // 待执行队列是整帧一份，不按观测位置分组。坐标换算成相对原点后逐项比较。
        if (frame.contains("blockTicks") && !differs) {
            const auto relative = [&](Json rows) {
                for (auto& row : rows) { row[0] = row[0].get<int>() - origin.x; row[1] = row[1].get<int>() - origin.y; row[2] = row[2].get<int>() - origin.z; }
                return rows;
            };
            for (const char* field : {"blockTicks", "blockEvents"}) {
                const Json actual = relative(std::string(field) == "blockTicks" ? simulation.pendingBlockTicksJson() : simulation.pendingBlockEventsJson());
                if (frame.at(field) != actual) {
                    differs = true;
                    result["status"] = "difference";
                    result["firstDifference"] = {{"tick", tick}, {"field", field}, {"expected", frame.at(field)}, {"actual", actual}};
                    break;
                }
            }
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
            // 区块状态是输入：本帧命令生效后的状态，对应原版**下一帧**报告的派生标志。
            if (frame.contains("chunkStates") && frameIndex + 1 < frames.size())
                compare("chunkStates", frames.at(frameIndex + 1).at("chunkStates").at(index),
                        Json{{"blockTicking", simulation.chunkBlockTicking(pos)}, {"entityTicking", simulation.chunkEntityTicking(pos)}, {"loaded", simulation.chunkLoaded(pos)}});
            if (frame.contains("groundItems") && !frame.at("groundItems").at(index).is_null())
                compare("groundItems", frame.at("groundItems").at(index), simulation.suckableItems(pos));
            // 容器实体：原版按 getContainerAt 的查询顺序报告这一格里的矿车与它们的库存。
            if (frame.contains("containerEntities"))
                compare("containerEntities", frame.at("containerEntities").at(index), simulation.containerEntitiesJson(pos));
            if (frame.contains("bells")) compare("bells", frame.at("bells").at(index), simulation.inspect(pos)["runtime"].value("ringing", false));
            if (frame.contains("jukeboxes") && !frame.at("jukeboxes").at(index).is_null()) {
                const auto actual = simulation.inspect(pos).at("jukebox");
                compare("jukeboxes", frame.at("jukeboxes").at(index), Json{{"playing", actual.at("playing")}, {"elapsed", actual.at("elapsed")}});
            }
            if (differs) break;
        }
        if (differs) break;
    }
    if (!differs) {
        if (expectedTick - 1 != fixture.at("endTick").get<Tick>()) throw std::runtime_error("Capture does not reach endTick");
        // 声明了却始终没有发生的外部动作同样是失败。
        actions.finish();
    }
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
    return result;
}
}
