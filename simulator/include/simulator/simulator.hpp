#pragma once
#include "blockRegistry.hpp"
#include "world.hpp"
#include "blockTicks.hpp"
#include "legacyRandom.hpp"
#include <deque>
#include <queue>
#include <unordered_set>
#include <chrono>

namespace simulator {
struct TraceEdge { std::uint32_t probeId{}; Tick tick{}; std::uint64_t sequence{}; std::uint8_t value{}; };
struct Probe { std::uint32_t id{}; BlockPos pos{}; std::string name; std::string mode{"output"}; Direction direction{Direction::up}; int lastValue{-1}; std::string trigger{"none"}; int triggerValue{15}; };
struct ItemStack { std::uint32_t item{}; std::uint16_t count{}; bool operator==(const ItemStack&) const = default; };
struct RuntimeData { int output{}; Json values = Json::object(); std::vector<ItemStack> inventory; };
struct TorchToggle { BlockPos pos; Tick tick; };
struct PistonMotion { StateId movedState{}; Direction facing{}; bool extending{}, source{}; unsigned progress{}, previousProgress{}; Tick lastTicked{}; std::uint64_t generation{}; };
struct HopperState {
    Tick readyAt{}, firstTick{}, wakeAt{UINT64_MAX};
    std::uint64_t generation{};
};
struct Statistics { std::uint64_t updates{}, scheduledEvents{}, stateChanges{}; std::uint64_t simulationMicros{}; };
class Simulator {
public:
    explicit Simulator(const BlockRegistry& registry) : registry(registry) {}
    const BlockRegistry& registry;
    World world;
    Tick currentTick{};
    void setRandomSeed(std::uint64_t seed) { randomSeed = seed; worldRandom.setSeed(seed); }
    bool hasPendingActions() const { return !pendingActionIds.empty(); }
    Json pendingActionsJson() const;
    const std::deque<Json>& actionHistory() const { return environmentActions; }
    std::uint64_t actionHistoryDropped() const { return actionsDropped; }
    void resolveAction(std::uint64_t id);
    Statistics statistics;
    bool breakRequested{};
    bool faulted{};
    std::string pauseReason;
    std::size_t updateBudget{1000000};
    std::size_t traceCapacity{500000};
    std::uint64_t traceDropped{};
    // History is trimmed only after consumers acknowledge it. A single atomic
    // event may use the reserve; exceeding it faults explicitly instead of
    // silently overwriting undelivered edges.
    std::size_t traceAtomicReserve{65536};
    void retainTraceFrom(std::optional<std::uint64_t> firstUnacknowledged);
    bool traceBlocked() const;
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
    int viewerCount(BlockPos pos) const { auto found = runtime.find(pos); return found == runtime.end() ? 0 : found->second.values.value("viewers", 0); }
    std::size_t cartCount(BlockPos pos) const {
        auto found = runtime.find(pos);
        if (found == runtime.end()) return 0;
        auto carts = found->second.values.find("carts");
        return carts == found->second.values.end() ? 0 : carts->size();
    }
    void updateNeighbors(BlockPos pos, int skip = -1, StateId source = UINT32_MAX);
    void neighborChanged(BlockPos pos, StateId source = 0);
    void schedule(BlockPos pos, Tick delay, int priority = 0);
    bool hasScheduled(BlockPos pos) const;
    bool stepEvent();
    std::size_t advanceTo(Tick target, std::size_t eventBudget = 1000000, std::chrono::microseconds wallBudget = std::chrono::seconds(10));
    std::size_t advanceActive(std::size_t eventBudget = 1000000, std::chrono::microseconds wallBudget = std::chrono::seconds(10));
    std::size_t pendingEvents() const { return scheduledKeys.size() + blockTicks.size(); }
    Tick nextTick();
    void clear();
    std::uint32_t addProbe(BlockPos pos, const std::string& name = "", const std::string& mode = "output", Direction direction = Direction::up);
    void removeProbe(std::uint32_t id);
    void configureProbe(std::uint32_t id, const Json& config);
    const std::vector<Probe>& getProbes() const { return probes; }
    const std::deque<TraceEdge>& getTrace() const { return trace; }
    void clearTrace();
    std::vector<Cell> takeChanges();
    Json inspect(BlockPos pos) const;
    Json inventoryJson(BlockPos pos, bool combined = true) const;
    Json saveProject(const std::string& name = "未命名电路", bool checkpoint = false) const;
    void loadProject(const Json& data);
    std::string exportVcd() const;
    std::unique_ptr<Simulator> clone() const;
    void restore(const Simulator& snapshot);
    std::size_t estimatedBytes() const {
        auto bytes = world.storageBytes() + trace.size() * sizeof(TraceEdge) + runtime.size() * 512 + scheduled.size() * 128 + hoppers.size() * 96 + entityOrders.size() * 64 + blockTicks.estimatedBytes();
        for (const auto& [pos, data] : runtime) { (void)pos; bytes += data.inventory.capacity() * sizeof(ItemStack); }
        return bytes + recentTorchToggles.size() * sizeof(TorchToggle) + torchToggleCounts.size() * 64 + environmentActions.size() * 2048;
    }
    const PistonMotion* motionAt(BlockPos pos) const { auto it = motions.find(pos); return it == motions.end() ? nullptr : &it->second; }
private:
    LegacyRandom worldRandom;
    std::uint64_t randomSeed{};
    void dispenseDropper(BlockPos pos);
    std::deque<Json> environmentActions;
    std::unordered_set<std::uint64_t> pendingActionIds;
    std::uint64_t nextActionId{1}, actionsDropped{};
    static constexpr std::size_t actionCapacity = 4096;
    void recordAction(Json action);
    void loadActions(const Json& data);
    std::size_t advance(Tick target, std::size_t eventBudget, std::chrono::microseconds wallBudget, bool fillIdle);
    enum class UpdateKind { neighbor, shape, multi };
    struct Update { UpdateKind kind; BlockPos pos; Direction direction{Direction::down}; StateId neighborState{}; int index{}, skip{-1}, depth{512}; unsigned flags{2}; };
    std::vector<Update> updateStack, addedUpdates;
    bool updating{};
    std::size_t updateCount{};
    BlockTicks blockTicks;
    std::priority_queue<ScheduledEvent, std::vector<ScheduledEvent>, EventLater> scheduled;
    std::unordered_set<EventKey, EventKeyHash> scheduledKeys;
    std::unordered_map<BlockPos, RuntimeData, PosHash> runtime;
    std::deque<TorchToggle> recentTorchToggles;
    std::unordered_map<BlockPos, unsigned, PosHash> torchToggleCounts;
    std::unordered_map<BlockPos, PistonMotion, PosHash> motions;
    std::unordered_map<BlockPos, HopperState, PosHash> hoppers;
    std::unordered_map<BlockPos, std::uint64_t, PosHash> entityOrders;
    std::unordered_map<BlockPos, StateId, PosHash> changes;
    std::vector<Probe> probes;
    std::unordered_map<BlockPos, std::vector<std::uint32_t>, PosHash> probeDependencies;
    std::deque<TraceEdge> trace;
    std::optional<std::uint64_t> retainedTrace;
    std::uint32_t nextProbeId{1};
    std::uint64_t nextOrder{}, sequence{};
    std::uint64_t nextEntityOrder{}, currentEntityOrder{};
    // 0: block ticks, 1: block events, 2: block entities, 3: external contacts,
    // 4: paused/manual input. Contacts run between block events and entities.
    std::uint8_t currentPhase{4};
    bool beforeBlockEntities() const { return currentPhase < 2 || currentPhase == 3; }
    void enqueue(Update update);
    void executeNeighbor(BlockPos pos, StateId source = 0);
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
    void trimTrace();
    void rebuildProbeDependencies();
    struct PistonPlan { bool valid{}; std::vector<BlockPos> push, destroy; };
    bool pistonPowered(BlockPos pos) const;
    bool pushable(BlockPos pos, Direction movement, bool allowDestroy, Direction connection) const;
    PistonPlan resolvePiston(BlockPos pos, Direction facing, bool extending) const;
    void checkPiston(BlockPos pos);
    void pistonEvent(const ScheduledEvent& event);
    bool movePistonBlocks(BlockPos pos, Direction facing, bool extending);
    void addMotion(BlockPos pos, StateId state, Direction facing, bool extending, bool source);
    void tickMotion(const ScheduledEvent& event);
    void finishMotion(BlockPos pos, bool force);
    void schedulePhase(BlockPos pos, Tick when, std::uint8_t phase, std::uint64_t data);
    std::uint64_t registerEntity(BlockPos pos);
    void pruneEvents();
    void updateComparatorNeighbors(BlockPos pos);
    void runtimeChanged(BlockPos pos, bool notifyComparators = true);
    void updatePressurePlate(BlockPos pos);
    void updateButton(BlockPos pos);
    void buttonContact(BlockPos pos);
    void updateDaylight(BlockPos pos);
    bool interactDevice(BlockPos pos);
    bool stimulateDevice(BlockPos pos, const Json& stimulus);
    void validateRuntime(BlockPos pos) const;
    struct InventorySlot { BlockPos pos; std::size_t index; };
    std::size_t inventorySize(StateId state) const;
    std::vector<InventorySlot> containerSlots(BlockPos pos, bool ignoreBlockage = true) const;
    Direction chestConnection(StateId state) const;
    bool isCopperChest(StateId state) const;
    bool chestsConnect(StateId first, StateId second) const;
    StateId placedChest(BlockPos pos, StateId state) const;
    void updateChestShape(const Update& update);
    ItemStack stackAt(const InventorySlot& slot) const;
    int containerAnalog(BlockPos pos) const;
    void setInventory(BlockPos pos, const Json& slots, bool combined = true, bool notify = true);
    std::vector<std::pair<std::size_t, ItemStack>> parseInventory(const Json& values, std::size_t size) const;
    void setViewers(BlockPos pos, int viewers);
    void writeStack(const InventorySlot& slot, ItemStack stack, bool notify = true);
    void containerChanged(BlockPos pos);
    bool inventoryEmpty(BlockPos pos) const;
    bool inventoryFull(BlockPos pos) const;
    bool transferItem(BlockPos from, BlockPos to, bool pulling = false);
    void wakeHopper(BlockPos pos);
    void wakeHoppers(BlockPos changed);
    void tickHopper(const ScheduledEvent& event);
    void startHopper(BlockPos pos);
    void placeRail(BlockPos pos);
    void updateRail(BlockPos pos, StateId source);
    void removeRail(BlockPos pos, StateId state);
    bool poweredRailPath(BlockPos pos, StateId state, bool forward, int depth) const;
    void updateDetectorRail(BlockPos pos);
    void setCartInput(BlockPos pos, const Json& input);
    Json normalizeCarts(const Json& carts) const;
    const Json* firstContainerCart(BlockPos pos) const;
    int cartAnalog(BlockPos pos) const;
    bool connectsTripwire(StateId neighbor, Direction direction) const;
    void updateTripwireSource(BlockPos pos, StateId state);
    void calculateTripwire(BlockPos pos, StateId state, bool destroying, bool notify, int wireSource = -1, StateId sourceState = 0);
    void removeTripwireHook(BlockPos pos, StateId state);
    void updateTripwire(BlockPos pos);
    void tripwireContact(BlockPos pos);
};
}
