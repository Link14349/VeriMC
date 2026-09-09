#pragma once
#include "blockRegistry.hpp"
#include "world.hpp"
#include "blockTicks.hpp"
#include "legacyRandom.hpp"
#include "projectIo.hpp"
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
using Vec3 = std::array<double,3>;
struct VibrationContext { bool spectator{}, sneaking{}, dampens{}; StateId affectedState{UINT32_MAX}; };
// 26.2 Mth.SIN 的等价重建，导出用于与原版逐项对照。
const std::array<float, 65536>& daylightSineTable();
struct VibrationInfo { std::uint16_t event{}; Vec3 origin{}; float distance{}; VibrationContext context; };
struct SensorState {
    std::optional<VibrationInfo> candidate, current;
    Tick candidateTick{}, wakeAt{UINT64_MAX};
    int remaining{};
    std::uint64_t generation{};
};
struct JukeboxState { int song{-1};std::uint32_t elapsed{};Tick firstTick{},wakeAt{UINT64_MAX};std::uint64_t generation{}; };
struct Statistics { std::uint64_t updates{}, scheduledEvents{}, stateChanges{}; std::uint64_t simulationMicros{}; };
class Simulator {
public:
    explicit Simulator(const BlockRegistry& registry) : registry(registry) {}
    const BlockRegistry& registry;
    World world;
    Tick currentTick{};
    void setRandomSeed(std::uint64_t seed) { randomSeed = seed; worldRandom.setSeed(seed); }
    // 世界随机源的 48 位内部状态，用于与原版逐帧对照消耗次数。
    std::uint64_t randomState() const { return worldRandom.state(); }
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
    // 刻内更新轨迹：对应原版 CollectingNeighborUpdater 的 debugListener，
    // 每次从栈顶取出一个更新对象时记录它的受影响坐标。容量为 0 表示关闭。
    // 容量耗尽时置 updateTraceTruncated 并停止记录，截断的轨迹不得判为通过。
    std::size_t updateTraceLimit{};
    bool updateTraceTruncated{};
    Json updateTrace = Json::array();
    void markUpdateTrace(Tick tick) { if (updateTraceLimit) appendUpdateTrace(Json(tick)); }
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
    void interact(BlockPos pos, std::optional<Direction> playerFacing = std::nullopt);
    void stimulate(BlockPos pos, const Json& stimulus);
    int signal(BlockPos emitter, Direction direction, bool includeWire = true) const;
    int directSignal(BlockPos emitter, Direction direction, bool includeWire = true) const;
    int bestSignal(BlockPos receiver, bool includeWire = true) const;
    int analogOutput(BlockPos pos) const;
    int displayValue(BlockPos pos) const;
    int viewerCount(BlockPos pos) const { auto found = runtime.find(pos); return found == runtime.end() ? 0 : found->second.values.value("viewers", 0); }
    bool bellRinging(BlockPos pos) const { auto found=runtime.find(pos);return found!=runtime.end() && found->second.values.value("ringing",false); }
    // 漏斗吸取范围内、按声明顺序排列的掉落物；与原版 getItemsAtAndAbove 的结果对照。
    Json suckableItems(BlockPos pos) const;
    bool jukeboxPlaying(BlockPos pos) const { auto found=jukeboxes.find(pos);return found!=jukeboxes.end() && found->second.song>=0; }
    std::size_t cartCount(BlockPos pos) const {
        auto found = runtime.find(pos);
        if (found == runtime.end()) return 0;
        auto carts = found->second.values.find("carts");
        return carts == found->second.values.end() ? 0 : carts->size();
    }
    // 区块生命周期（issue #14 第五条）。原版的判据是票据等级；本项目**不模拟票据传播**，
    // 区块状态是显式输入。默认整张图 entityTicking，与历史行为完全一致。
    enum class ChunkState : std::uint8_t { unloaded, loaded, blockTicking, entityTicking };
    static BlockPos chunkOf(BlockPos pos);
    ChunkState chunkState(BlockPos pos) const;
    // stalledSince 只在从工程文件恢复时显式给出；正常调用由当前刻自动记录。
    void setChunkState(int chunkX, int chunkZ, ChunkState state, std::optional<Tick> stalledSince = std::nullopt);
    Json chunkStatesJson() const;
    // 待执行的方块计划刻与方块事件，按原版 DRAIN_ORDER / 插入顺序排列，
    // 用于与原版队列逐项对照。坐标是绝对坐标，比较时再换算成相对。
    // 方块实体的执行顺序：按注册序号排列，对应原版 Level.blockEntityTickers 的列表顺序。
    Json blockEntityOrderJson() const;
    Json pendingBlockTicksJson() const;
    Json pendingBlockEventsJson() const;
    // 是否还有可以执行的事件；只剩不可 ticking 区块里的事件时返回 false。
    bool runnable() const;
    // 区块不 ticking 时方块实体根本不执行，它们的倒计时也不会递减。事件被延后一刻时
    // 把「绝对唤醒时刻」一起后移，等价于原版的冷却与计时在停摆期间冻结。
    void shiftBlockEntityTimers(BlockPos chunk, Tick delta);
    TickCheck blockTickCheck() const {
        if (chunkStates.empty()) return {};
        return {[](const void* owner, BlockPos chunk) {
            return static_cast<const Simulator*>(owner)->chunkBlockTicking({chunk.x * 16, 0, chunk.z * 16});
        }, this};  // chunk 是区块坐标，乘 16 回到方块坐标
    }
    bool chunkBlockTicking(BlockPos pos) const { return chunkStates.empty() || chunkState(pos) >= ChunkState::blockTicking; }
    bool chunkEntityTicking(BlockPos pos) const { return chunkStates.empty() || chunkState(pos) == ChunkState::entityTicking; }
    bool chunkLoaded(BlockPos pos) const { return chunkStates.empty() || chunkState(pos) != ChunkState::unloaded; }
    void updateNeighbors(BlockPos pos, int skip = -1, StateId source = UINT32_MAX);
    void neighborChanged(BlockPos pos, StateId source = 0);
    // 原版 FullNeighborUpdate：带入队时的目标状态快照，执行时不再重新读世界。
    void neighborChangedSnapshot(BlockPos pos, StateId snapshot, StateId source, bool movedByPiston = false);
    // type 默认取世界当前方块；活塞落地路径必须显式传入被移动方块的类型。
    void schedule(BlockPos pos, Tick delay, int priority = 0, std::uint16_t type = 0xFFFFu);
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
    Json saveProject(const std::string& name = "未命名电路", bool checkpoint = false, const std::function<void()>& check = {}) const;
    void loadProject(const Json& data, const std::function<void()>& check = {});
    void loadProject(ProjectSource& source);
    void writeProject(ProjectSink& sink, const std::string& name, bool checkpoint) const;
    std::string exportVcd() const;
    std::unique_ptr<Simulator> clone() const;
    void restore(const Simulator& snapshot);
    // Both worlds must be quiescent and owned by the caller. Exchanges native
    // storage so a successful file import can retain the previous world as undo.
    void exchangeProject(Simulator& other);
    std::size_t estimatedBytes() const {
        auto bytes = world.storageBytes() + trace.size() * sizeof(TraceEdge) + runtime.size() * 512 + scheduled.size() * 128 + hoppers.size() * 96 + entityOrders.size() * 64 + blockTicks.estimatedBytes();
        for (const auto& [pos, data] : runtime) { (void)pos; bytes += data.inventory.capacity() * sizeof(ItemStack); }
        return bytes + recentTorchToggles.size() * sizeof(TorchToggle) + torchToggleCounts.size() * 64 + environmentActions.size() * 2048 + sensors.size() * 384 + sensorSections.size() * 96 + jukeboxes.size() * 128;
    }
    const PistonMotion* motionAt(BlockPos pos) const { auto it = motions.find(pos); return it == motions.end() ? nullptr : &it->second; }
private:
    LegacyRandom worldRandom;
    std::uint64_t randomSeed{};
    void dispenseDropper(BlockPos pos);
    bool addCompost(BlockPos pos, std::uint32_t item);
    bool insertCompost(BlockPos from, BlockPos into, ItemStack stack);
    void emptyComposter(BlockPos pos, StateId state);
    bool transferComposter(BlockPos from, BlockPos into, bool pulling);
    StateId noteInstrument(BlockPos pos, StateId state) const;
    void playNote(BlockPos pos, StateId state);
    void noteEvent(BlockPos pos);
    bool stimulateNote(BlockPos pos, const Json& input);
    static std::string soundId(const Json& value);
    void validateNoteRuntime(BlockPos pos) const;
    Direction bellSupport(StateId state) const;
    void ringBell(BlockPos pos, Direction direction);
    bool stimulateBell(BlockPos pos, const Json& input);
    void bellEvent(const ScheduledEvent& event);
    void finishBell(const ScheduledEvent& event);
    void startJukebox(BlockPos pos);
    void removeJukebox(BlockPos pos,std::uint16_t oldType);
    void updateJukeboxItem(BlockPos pos);
    void updateJukeboxTicker(BlockPos pos);
    void tickJukebox(const ScheduledEvent& event);
    std::unordered_map<BlockPos,JukeboxState,PosHash> jukeboxes;
    void startSensor(BlockPos pos);
    void removeSensor(BlockPos pos, std::uint16_t oldType);
    void rebuildSensorIndex();
    void stimulateVibration(BlockPos pos, const Json& input);
    void emitGameEvent(std::uint16_t event, const Vec3& origin, VibrationContext context = {});
    void emitGameEvent(const std::string& event, BlockPos pos, VibrationContext context = {});
    bool vibrationOccluded(const Vec3& origin, BlockPos destination) const;
    void tickVibration(const ScheduledEvent& event);
    void tickSensor(BlockPos pos);
    void activateSensor(BlockPos pos, const VibrationInfo& vibration);
    std::unordered_map<BlockPos, SensorState, PosHash> sensors;
    std::unordered_map<BlockPos, std::vector<BlockPos>, PosHash> sensorSections;
    std::deque<Json> environmentActions;
    std::unordered_set<std::uint64_t> pendingActionIds;
    std::uint64_t nextActionId{1}, actionsDropped{};
    static constexpr std::size_t actionCapacity = 4096;
    void recordAction(Json action);
    void loadActions(const Json& data);
    std::size_t advance(Tick target, std::size_t eventBudget, std::chrono::microseconds wallBudget, bool fillIdle);
    enum class UpdateKind { neighbor, shape, multi };
    // 原版把邻居更新分成两种：SimpleNeighborUpdate 执行时才读目标状态，
    // FullNeighborUpdate 带入队时的状态快照。snapshot == noSnapshot 表示前者。
    static constexpr StateId noSnapshot = static_cast<StateId>(-1);
    struct Update { UpdateKind kind; BlockPos pos; Direction direction{Direction::down}; StateId neighborState{}; int index{}, skip{-1}, depth{512}; unsigned flags{2}; StateId snapshot{noSnapshot}; bool movedByPiston{}; };
    void updateBellShape(const Update& update);
    void appendUpdateTrace(Json entry);
    void recordUpdateTrace(const Update& update);
    std::vector<Update> updateStack, addedUpdates;
    bool updating{};
    std::size_t updateCount{};
    BlockTicks blockTicks;
    std::priority_queue<ScheduledEvent, std::vector<ScheduledEvent>, EventLater> scheduled;
    std::unordered_set<EventKey, EventKeyHash> scheduledKeys;
    // 缺省不建条目：空表示整张图 entityTicking，热路径只多一次 empty() 判断。
    // stalledSince 记录该区块停止执行方块实体的时刻，恢复时用它把冻结的倒计时补回去。
    struct ChunkRecord { ChunkState state{ChunkState::entityTicking}; Tick stalledSince{}; };
    std::unordered_map<BlockPos, ChunkRecord, PosHash> chunkStates;
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
    void executeNeighbor(BlockPos pos, StateId source = 0, StateId snapshot = noSnapshot);
    void executeReactiveNeighbor(BlockPos pos, StateId state, StateId source);
    void executeShape(const Update& update);
    bool supportChecked(StateId state, Direction direction) const;
    StateId shapeUpdated(BlockPos pos, StateId state, Direction direction, StateId neighborState);
    StateId updateFromNeighborShapes(BlockPos pos, StateId state);
    void executeTick(const ScheduledEvent& event);
    void onPlace(BlockPos pos, StateId state, StateId oldState);
    void onRemove(BlockPos pos, StateId oldState, bool movedByPiston = false);
    bool survives(BlockPos pos, StateId state) const;
    void indirectShapes(BlockPos pos, StateId state, unsigned flags, int depth);
    // source 默认取世界当前方块；移除回调里世界已经写入新方块，必须显式传被移除的状态。
    void notifyFront(BlockPos pos, Direction facing, StateId source = UINT32_MAX);
    void notifyAttached(BlockPos pos, Direction connected, StateId source = UINT32_MAX);
    void updateWire(BlockPos pos, StateId state);
    StateId wireConnections(BlockPos pos, StateId state) const;
    StateId stairsShape(BlockPos pos, StateId state) const;
    std::array<std::uint8_t, 4> connectionSides(BlockPos pos, StateId state) const;
    std::uint8_t wireSide(BlockPos pos, Direction direction) const;
    bool connectsWire(StateId state, int direction) const;
    void wireCorners(BlockPos pos);
    int diodeInput(BlockPos pos) const;
    int diodeSideInput(BlockPos pos) const;
    bool prioritizeDiode(BlockPos pos) const;
    bool torchInput(BlockPos pos) const;
    int comparatorInput(BlockPos pos) const;
    // 受限的物品展示框输入：按“挂在哪一格的哪一面”记录，不是完整实体世界。
    std::optional<int> itemFrameSignal(BlockPos mount, Direction facing) const;
    void stimulateItemFrames(BlockPos pos, const Json& input);
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
    void updateComparatorNeighbors(BlockPos pos, StateId source = noSnapshot);
    // 器件层实体物品输入（issue #12 第一阶段）：漏斗吸取声明在其吸取范围内的掉落物。
    // 不模拟掉落物的运动、碰撞或合并，只执行原版 suckInItems 的方块侧逻辑。
    void stimulateGroundItems(BlockPos pos, const Json& input);
    bool itemInSuckRange(BlockPos pos, const Json& entry) const;
    bool itemInsideHopperBlock(BlockPos pos, const Json& entry) const;
    bool suckItemEntities(BlockPos pos);
    void insertStack(BlockPos pos, ItemStack& stack, Tick cooldown);
    // 原版 HopperBlock.entityInside：掉落物停在漏斗自己那一格时，每刻每个实体各触发一次
    // tryMoveItems。这条路径在实体阶段执行，早于方块实体阶段，而且不受上方方块阻挡。
    void hopperEntityContact(BlockPos pos);
    bool hopperHasContact(BlockPos pos) const;
    void runtimeChanged(BlockPos pos, bool notifyComparators = true);
    void updatePressurePlate(BlockPos pos);
    void updateButton(BlockPos pos);
    void buttonContact(BlockPos pos);
    void updateDaylight(BlockPos pos);
    bool interactDevice(BlockPos pos, std::optional<Direction> playerFacing = std::nullopt);
    bool stimulateDevice(BlockPos pos, const Json& stimulus);
    void validateRuntime(BlockPos pos) const;
    // entity >= 0 表示这一格里第 entity 个**声明的容器实体**（运输/漏斗矿车）的槽位，
    // 它的库存存在 runtime[pos].values["containerEntities"][entity]["inventory"] 里；
    // -1 表示方块自身的库存。
    struct InventorySlot { BlockPos pos; std::size_t index; int entity = -1; };
    std::size_t inventorySize(StateId state) const;
    bool isBookshelf(StateId state) const;
    bool isDecoratedPot(StateId state) const;
    bool canInsertStack(const InventorySlot& slot, ItemStack stack) const;
    bool canExtractStack(const InventorySlot& slot, BlockPos into) const;
    void updateBookshelfSlot(const InventorySlot& slot);
    std::vector<InventorySlot> containerSlots(BlockPos pos, bool ignoreBlockage = true) const;
    // 器件层实体容器（issue #12）：声明在某一格里的运输/漏斗矿车。
    // 原版 getEntityContainer 在候选里用 level.random.nextInt(size) 随机选一个，会消耗随机数。
    void stimulateContainerEntities(BlockPos pos, const Json& input);
    std::size_t containerEntityCount(BlockPos pos) const;
    std::vector<InventorySlot> entityContainerSlots(BlockPos pos, int entity) const;
    std::optional<int> chooseContainerEntity(BlockPos pos);
    Json containerEntitiesJson(BlockPos pos) const;
    Direction chestConnection(StateId state) const;
    bool isCopperChest(StateId state) const;
    bool chestsConnect(StateId first, StateId second) const;
    StateId placedChest(BlockPos pos, StateId state) const;
    void updateChestShape(const Update& update);
    ItemStack stackAt(const InventorySlot& slot) const;
    int containerAnalog(BlockPos pos) const;
    void setInventory(BlockPos pos, const Json& slots, bool combined = true, bool notify = true);
    std::vector<std::pair<std::size_t, ItemStack>> parseInventory(const Json& values, std::size_t size, bool preserveOrder = false) const;
    void setViewers(BlockPos pos, int viewers);
    void writeStack(const InventorySlot& slot, ItemStack stack, bool notify = true);
    void containerChanged(BlockPos pos);
    bool inventoryEmpty(BlockPos pos) const;
    bool inventoryFull(BlockPos pos) const;
    bool transferItem(BlockPos from, BlockPos to, bool pulling = false);
    bool transferSlots(const std::vector<InventorySlot>& sourceSlots, const std::vector<InventorySlot>& targetSlots,
                       BlockPos from, BlockPos to, bool pulling);
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
