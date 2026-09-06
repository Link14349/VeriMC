#include "simulator/blockTicks.hpp"
#include <algorithm>
#include <limits>

namespace simulator {
BlockPos BlockTicks::chunkAt(BlockPos pos) {
    auto floor16 = [](int value) { return value / 16 - (value % 16 < 0 ? 1 : 0); };
    return {floor16(pos.x), 0, floor16(pos.z)};
}
bool BlockTicks::schedule(const ScheduledEvent& event) {
    if (event.phase != 0 || event.data != 0 || event.priority < -3 || event.priority > 3)
        throw std::invalid_argument("无效的方块计划刻");
    if (!queuedKeys.insert({event.pos, event.type}).second) return false;
    auto& queue = chunks[chunkAt(event.pos)];
    bool changesHead = queue.empty() || event.blockKey() < queue.top().blockKey();
    if (changesHead && !queue.empty()) heads.erase(queue.top());
    queue.push(event);
    if (changesHead) heads.insert(event);
    return true;
}
bool BlockTicks::willTick(BlockPos pos, std::uint16_t type) const {
    // Most worlds never query this, so avoid building the set until needed.
    if (batchKeys.empty() && !batch.empty())
        for (const auto& event : batch) batchKeys.insert({event.pos, event.type});
    return batchKeys.contains({pos, type});
}
std::optional<Tick> BlockTicks::nextTick() const {
    if (!batch.empty()) return batchTick();
    if (heads.empty()) return std::nullopt;
    return std::max(earliestCollection, heads.begin()->tick);
}
void BlockTicks::collect(Tick tick, std::size_t limit) {
    if (!batch.empty() || tick < earliestCollection || tick == std::numeric_limits<Tick>::max())
        throw std::invalid_argument("方块计划刻收集阶段无效");
    earliestCollection = tick + 1;
    auto later = [](const ScheduledEvent& a, const ScheduledEvent& b) { return a.drainKey() > b.drainKey(); };
    std::priority_queue<ScheduledEvent, std::vector<ScheduledEvent>, decltype(later)> ready(later);
    while (!heads.empty() && heads.begin()->tick <= tick) {
        ready.push(*heads.begin());
        heads.erase(heads.begin());
    }
    // Each chunk keeps trigger-time ordering. Among due chunk heads, the game
    // compares only priority and insertion order, including overdue ticks.
    while (!ready.empty() && batch.size() < limit) {
        auto event = ready.top(); ready.pop();
        auto chunk = chunks.find(chunkAt(event.pos));
        auto& queue = chunk->second;
        queue.pop();
        queuedKeys.erase({event.pos, event.type});
        batch.push_back(event);
        if (queue.empty()) chunks.erase(chunk);
        else if (queue.top().tick <= tick) ready.push(queue.top());
        else heads.insert(queue.top());
    }
    while (!ready.empty()) { heads.insert(ready.top()); ready.pop(); }
}
ScheduledEvent BlockTicks::pop() {
    if (batch.empty()) throw std::logic_error("方块计划刻批次为空");
    auto event = batch.front(); batch.pop_front();
    batchKeys.erase({event.pos, event.type});
    return event;
}
void BlockTicks::finishThrough(Tick tick) {
    if (!batch.empty() || (nextTick() && *nextTick() <= tick))
        throw std::logic_error("尚有待执行的方块计划刻");
    if (tick == std::numeric_limits<Tick>::max()) throw std::invalid_argument("仿真时间超出范围");
    earliestCollection = std::max(earliestCollection, tick + 1);
}
std::vector<ScheduledEvent> BlockTicks::queuedEvents() const {
    std::vector<ScheduledEvent> result;
    result.reserve(queuedKeys.size());
    for (const auto& [pos, original] : chunks) {
        (void)pos;
        auto queue = original;
        while (!queue.empty()) { result.push_back(queue.top()); queue.pop(); }
    }
    std::sort(result.begin(), result.end(), Earlier{});
    return result;
}
void BlockTicks::restoreBatch(Tick earliest, const std::deque<ScheduledEvent>& events) {
    if (earliest == 0 || events.size() > vanillaBatchLimit) throw std::invalid_argument("无效的本刻计划事件批次");
    std::unordered_set<EventKey, EventKeyHash> keys;
    for (const auto& event : events) {
        if (event.phase != 0 || event.data != 0 || event.tick >= earliest || event.priority < -3 || event.priority > 3 || !keys.insert({event.pos, event.type}).second)
            throw std::invalid_argument("无效的本刻计划事件批次");
    }
    earliestCollection = earliest; batch = events; batchKeys.clear();
}
}
