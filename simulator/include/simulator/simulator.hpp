#pragma once
#include "blockRegistry.hpp"
#include "world.hpp"
#include <deque>
#include <queue>
#include <unordered_set>
#include <chrono>

namespace simulator {
struct ScheduledEvent {
    Tick tick{}; int priority{}; std::uint64_t order{}; BlockPos pos{}; std::uint16_t type{};
    auto key() const { return std::tuple(tick, priority, order); }
};
struct EventLater { bool operator()(const ScheduledEvent& a, const ScheduledEvent& b) const { return a.key() > b.key(); } };
struct EventKey { BlockPos pos; std::uint16_t type; bool operator==(const EventKey&) const = default; };
struct EventKeyHash { std::size_t operator()(const EventKey& k) const { return PosHash{}(k.pos) ^ (static_cast<std::size_t>(k.type) * 65537); } };
struct TraceEdge { std::uint32_t probeId{}; Tick tick{}; std::uint64_t sequence{}; std::uint8_t value{}; };
struct Probe { std::uint32_t id{}; BlockPos pos{}; std::string name; std::string mode{"output"}; Direction direction{Direction::up}; int lastValue{-1}; std::string trigger{"none"}; int triggerValue{15}; };
struct RuntimeData { int output{}; std::deque<Tick> torchToggles; Json values = Json::object(); };
struct Statistics { std::uint64_t updates{}, scheduledEvents{}, stateChanges{}; std::uint64_t simulationMicros{}; };
class Simulator {
public:
    explicit Simulator(const BlockRegistry& registry) : registry(registry) {}
    const BlockRegistry& registry;
    World world;
    Tick currentTick{};
    Statistics statistics;
    bool breakRequested{};
    std::string pauseReason;
    std::size_t updateBudget{1000000};
    std::size_t traceCapacity{500000};
    std::uint64_t traceDropped{};
    std::uint64_t revision{};
    const BlockState& at(BlockPos p) const { return registry[world.get(p)]; }
    void setBlock(BlockPos pos, StateId state, unsigned flags = 3, int depth = 512);
    void place(BlockPos pos, StateId state);
    void interact(BlockPos pos);
    void stimulate(BlockPos pos, const Json& stimulus);
    int signal(BlockPos emitter, Direction direction, bool includeWire = true) const;
    int directSignal(BlockPos emitter, Direction direction, bool includeWire = true) const;
    int bestSignal(BlockPos receiver, bool includeWire = true) const;
    int analogOutput(BlockPos pos) const;
    int displayValue(BlockPos pos) const;
    void updateNeighbors(BlockPos pos, int skip = -1);
    void neighborChanged(BlockPos pos);
    void schedule(BlockPos pos, Tick delay, int priority = 0);
    bool hasScheduled(BlockPos pos) const;
    bool stepEvent();
    std::size_t advanceTo(Tick target, std::size_t eventBudget = 1000000, std::chrono::microseconds wallBudget = std::chrono::seconds(10));
    std::size_t pendingEvents() const { return scheduled.size(); }
    Tick nextTick() const { return scheduled.empty() ? currentTick : scheduled.top().tick; }
    void clear();
    std::uint32_t addProbe(BlockPos pos, const std::string& name = "", const std::string& mode = "output", Direction direction = Direction::up);
    void removeProbe(std::uint32_t id);
    void configureProbe(std::uint32_t id, const Json& config);
    const std::vector<Probe>& getProbes() const { return probes; }
    const std::deque<TraceEdge>& getTrace() const { return trace; }
    void clearTrace();
    std::vector<Cell> takeChanges();
    Json inspect(BlockPos pos) const;
    Json saveProject(const std::string& name = "未命名电路", bool checkpoint = false) const;
    void loadProject(const Json& data);
    std::string exportVcd() const;
    std::unique_ptr<Simulator> clone() const;
    void restore(const Simulator& snapshot);
    std::size_t estimatedBytes() const { return world.storageBytes() + trace.size() * sizeof(TraceEdge) + runtime.size() * 512 + scheduled.size() * 96; }
private:
    enum class UpdateKind { neighbor, shape, multi };
    struct Update { UpdateKind kind; BlockPos pos; Direction direction{Direction::down}; StateId neighborState{}; int index{}, skip{-1}, depth{512}; unsigned flags{2}; };
    std::vector<Update> updateStack, addedUpdates;
    bool updating{};
    std::size_t updateCount{};
    std::priority_queue<ScheduledEvent, std::vector<ScheduledEvent>, EventLater> scheduled;
    std::unordered_set<EventKey, EventKeyHash> scheduledKeys;
    std::unordered_map<BlockPos, RuntimeData, PosHash> runtime;
    std::unordered_map<BlockPos, StateId, PosHash> changes;
    std::vector<Probe> probes;
    std::unordered_map<BlockPos, std::vector<std::uint32_t>, PosHash> probeDependencies;
    std::deque<TraceEdge> trace;
    std::uint32_t nextProbeId{1};
    std::uint64_t nextOrder{}, sequence{};
    void enqueue(Update update);
    void executeNeighbor(BlockPos pos);
    void executeShape(const Update& update);
    void executeTick(const ScheduledEvent& event);
    void onPlace(BlockPos pos, StateId state, StateId oldState);
    void onRemove(BlockPos pos, StateId oldState);
    bool survives(BlockPos pos, StateId state) const;
    void indirectShapes(BlockPos pos, StateId state, unsigned flags, int depth);
    void notifyFront(BlockPos pos, Direction facing);
    void notifyAttached(BlockPos pos, Direction connected);
    void updateWire(BlockPos pos, StateId state);
    StateId wireConnections(BlockPos pos, StateId state) const;
    std::array<std::uint8_t, 4> connectionSides(BlockPos pos, StateId state) const;
    std::uint8_t wireSide(BlockPos pos, Direction direction) const;
    bool connectsWire(StateId state, int direction) const;
    void wireCorners(BlockPos pos);
    int diodeInput(BlockPos pos) const;
    int diodeSideInput(BlockPos pos) const;
    bool prioritizeDiode(BlockPos pos) const;
    bool torchInput(BlockPos pos) const;
    int comparatorInput(BlockPos pos) const;
    void refreshComparator(BlockPos pos);
    void sampleAffected(BlockPos pos);
    void sampleProbe(Probe& probe);
    void rebuildProbeDependencies();
};
}
