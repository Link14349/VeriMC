#pragma once
#include "types.hpp"
#include <deque>
#include <optional>
#include <queue>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace simulator {
struct ScheduledEvent {
    Tick tick{};
    int priority{};
    std::uint64_t order{};
    BlockPos pos{};
    std::uint16_t type{};
    std::uint8_t phase{};
    std::uint64_t data{}, entityOrder{};
    auto key() const { return std::tuple(tick, phase == 3 ? 2 : phase == 2 ? 3 : phase, priority, phase == 2 ? entityOrder : order, order); }
    auto blockKey() const { return std::tuple(tick, priority, order); }
    auto drainKey() const { return std::pair(priority, order); }
};
struct EventLater {
    bool operator()(const ScheduledEvent& a, const ScheduledEvent& b) const { return a.key() > b.key(); }
};
struct EventKey {
    BlockPos pos;
    std::uint16_t type;
    std::uint8_t phase{};
    std::uint64_t data{};
    bool operator==(const EventKey&) const = default;
};
struct EventKeyHash {
    std::size_t operator()(const EventKey& k) const {
        return PosHash{}(k.pos) ^ (static_cast<std::size_t>(k.type) * 65537)
            ^ (static_cast<std::size_t>(k.phase) * 31) ^ static_cast<std::size_t>(k.data * 16777619);
    }
};

// 原版 LevelTicks 的 tickCheck：区块不可 ticking 时，它的计划刻既不执行也不丢弃，
// 而是留在容器里，每刻重试到区块恢复为止。这里用同一个谓词表达，
// 但直接跳过不可 ticking 的容器头，避免每刻空转（可观测行为相同）。
using ChunkCheck = bool (*)(const void*, BlockPos);
struct TickCheck {
    ChunkCheck check{};
    const void* owner{};
    bool operator()(BlockPos chunk) const { return !check || check(owner, chunk); }
};

// Java 26.2 LevelTicks / LevelChunkTicks. A collected batch is separate from
// future ticks: hasScheduledTick and willTickThisTick observe different sets.
class BlockTicks {
public:
    static constexpr std::size_t vanillaBatchLimit = 65536;
    bool schedule(const ScheduledEvent& event);
    bool hasScheduled(BlockPos pos, std::uint16_t type) const { return queuedKeys.contains({pos, type}); }
    bool willTick(BlockPos pos, std::uint16_t type) const;
    std::optional<Tick> nextTick(TickCheck tickable = {}) const;
    void collect(Tick tick, TickCheck tickable = {}, std::size_t limit = vanillaBatchLimit);
    ScheduledEvent pop();
    void finishThrough(Tick tick, TickCheck tickable = {});
    std::size_t size() const { return queuedKeys.size() + batch.size(); }
    bool hasBatch() const { return !batch.empty(); }
    Tick batchTick() const { return earliestCollection - 1; }
    Tick earliestTick() const { return earliestCollection; }
    const std::deque<ScheduledEvent>& batchEvents() const { return batch; }
    std::vector<ScheduledEvent> queuedEvents() const;
    void restoreBatch(Tick earliest, const std::deque<ScheduledEvent>& events);
    std::size_t estimatedBytes() const { return size() * 128 + chunks.size() * 256; }
private:
    struct Earlier {
        bool operator()(const ScheduledEvent& a, const ScheduledEvent& b) const { return a.blockKey() < b.blockKey(); }
    };
    struct Later {
        bool operator()(const ScheduledEvent& a, const ScheduledEvent& b) const { return a.blockKey() > b.blockKey(); }
    };
    using Queue = std::priority_queue<ScheduledEvent, std::vector<ScheduledEvent>, Later>;
    std::unordered_map<BlockPos, Queue, PosHash> chunks;
    std::set<ScheduledEvent, Earlier> heads;
    std::unordered_set<EventKey, EventKeyHash> queuedKeys;
    std::deque<ScheduledEvent> batch;
    mutable std::unordered_set<EventKey, EventKeyHash> batchKeys;
    // Commands at tick zero occur after its block-tick phase, like GameTest.
    Tick earliestCollection{1};
public:
    static BlockPos chunkAt(BlockPos pos);
};
}
