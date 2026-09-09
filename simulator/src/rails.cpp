#include "simulator/simulator.hpp"
#include <algorithm>
#include <cmath>
#include <optional>

namespace simulator {
namespace {
enum class RailShape { northSouth, eastWest, ascendingEast, ascendingWest, ascendingNorth, ascendingSouth, southEast, southWest, northWest, northEast };
constexpr std::array<const char*, 10> railNames{"north_south", "east_west", "ascending_east", "ascending_west", "ascending_north", "ascending_south", "south_east", "south_west", "north_west", "north_east"};
RailShape railShape(const Simulator& sim, StateId state) {
    auto name = sim.registry.property(state, "shape");
    auto found = std::find(railNames.begin(), railNames.end(), name);
    if (found == railNames.end()) throw std::logic_error("无效铁轨形状");
    return static_cast<RailShape>(found - railNames.begin());
}
std::optional<Direction> slopeDirection(RailShape shape) {
    switch (shape) {
    case RailShape::ascendingEast: return Direction::east;
    case RailShape::ascendingWest: return Direction::west;
    case RailShape::ascendingNorth: return Direction::north;
    case RailShape::ascendingSouth: return Direction::south;
    default: return {};
    }
}
std::array<BlockPos, 2> railConnections(BlockPos pos, RailShape shape) {
    const auto n = pos.relative(Direction::north), s = pos.relative(Direction::south);
    const auto w = pos.relative(Direction::west), e = pos.relative(Direction::east);
    switch (shape) {
    case RailShape::northSouth: return {n, s};
    case RailShape::eastWest: return {w, e};
    case RailShape::ascendingEast: return {w, e.relative(Direction::up)};
    case RailShape::ascendingWest: return {w.relative(Direction::up), e};
    case RailShape::ascendingNorth: return {n.relative(Direction::up), s};
    case RailShape::ascendingSouth: return {n, s.relative(Direction::up)};
    case RailShape::southEast: return {e, s};
    case RailShape::southWest: return {w, s};
    case RailShape::northWest: return {w, n};
    case RailShape::northEast: return {e, n};
    }
    return {n, s};
}
std::optional<BlockPos> railAt(const Simulator& sim, BlockPos pos) {
    for (auto candidate : {pos, pos.relative(Direction::up), pos.relative(Direction::down)})
        if (isRail(sim.at(candidate).device)) return candidate;
    return {};
}

// A short-lived placement calculation, never a per-block runtime object.
struct RailConnection {
    Simulator& sim;
    BlockPos pos;
    StateId state;
    bool straight;
    std::vector<BlockPos> connections;
    // 原版 `RailState(level, pos, state)` 的第三个参数是**调用方给的状态**，不是世界里的方块：
    // `this.state`、`this.block`、`this.isStraight` 与初始 `updateConnections` 全部由它决定
    // （RailState.java:20-28）。`getRail()` 那条路径传的确实是 `level.getBlockState(pos)`，
    // 但 `updateDir(level, pos, state, …)` 传的是 `neighborChanged` 收到的快照。
    RailConnection(Simulator& simulator, BlockPos position, StateId seed)
        : sim(simulator), pos(position), state(seed), straight(sim.registry[seed].device != Device::rail) {
        auto ends = railConnections(pos, railShape(sim, state));
        connections.assign(ends.begin(), ends.end());
    }
    RailConnection(Simulator& simulator, BlockPos position) : RailConnection(simulator, position, simulator.world.get(position)) {}
    bool connects(BlockPos other) const {
        return std::any_of(connections.begin(), connections.end(), [other](BlockPos p) { return p.x == other.x && p.z == other.z; });
    }
    void removeSoft() {
        std::vector<BlockPos> linked;
        for (auto connection : connections) if (auto neighbor = railAt(sim, connection)) {
            RailConnection rail(sim, *neighbor);
            if (rail.connects(pos)) linked.push_back(*neighbor);
        }
        connections = std::move(linked);
    }
    bool accepts(BlockPos other) const { return connects(other) || connections.size() != 2; }
    bool neighborAccepts(Direction direction) {
        if (auto neighbor = railAt(sim, pos.relative(direction))) {
            RailConnection rail(sim, *neighbor); rail.removeSoft(); return rail.accepts(pos);
        }
        return false;
    }
    RailShape withSlope(RailShape shape) const {
        if (shape == RailShape::northSouth) {
            if (isRail(sim.at(pos.relative(Direction::north).relative(Direction::up)).device)) shape = RailShape::ascendingNorth;
            if (isRail(sim.at(pos.relative(Direction::south).relative(Direction::up)).device)) shape = RailShape::ascendingSouth;
        } else if (shape == RailShape::eastWest) {
            if (isRail(sim.at(pos.relative(Direction::east).relative(Direction::up)).device)) shape = RailShape::ascendingEast;
            if (isRail(sim.at(pos.relative(Direction::west).relative(Direction::up)).device)) shape = RailShape::ascendingWest;
        }
        return shape;
    }
    void connect(BlockPos other) {
        connections.push_back(other);
        bool n = connects(pos.relative(Direction::north)), s = connects(pos.relative(Direction::south));
        bool w = connects(pos.relative(Direction::west)), e = connects(pos.relative(Direction::east));
        auto shape = RailShape::northSouth;
        if (w || e) shape = RailShape::eastWest;
        if (!straight) {
            if (s && e && !n && !w) shape = RailShape::southEast;
            if (s && w && !n && !e) shape = RailShape::southWest;
            if (n && w && !s && !e) shape = RailShape::northWest;
            if (n && e && !s && !w) shape = RailShape::northEast;
        }
        state = sim.registry.with(state, "shape", std::string(railNames[static_cast<std::size_t>(withSlope(shape))]));
        sim.setBlock(pos, state);
    }
    void place(bool powered, bool first) {
        bool n = neighborAccepts(Direction::north), s = neighborAccepts(Direction::south);
        bool w = neighborAccepts(Direction::west), e = neighborAccepts(Direction::east);
        auto shape = railShape(sim, state);
        bool chosen = false;
        if ((n || s) && !(w || e)) { shape = RailShape::northSouth; chosen = true; }
        if ((w || e) && !(n || s)) { shape = RailShape::eastWest; chosen = true; }
        if (!straight) {
            if (s && e && !n && !w) { shape = RailShape::southEast; chosen = true; }
            if (s && w && !n && !e) { shape = RailShape::southWest; chosen = true; }
            if (n && w && !s && !e) { shape = RailShape::northWest; chosen = true; }
            if (n && e && !s && !w) { shape = RailShape::northEast; chosen = true; }
        }
        if (!chosen && !straight) {
            if (powered) {
                if (s && e) shape = RailShape::southEast;
                if (s && w) shape = RailShape::southWest;
                if (n && e) shape = RailShape::northEast;
                if (n && w) shape = RailShape::northWest;
            } else {
                if (n && w) shape = RailShape::northWest;
                if (n && e) shape = RailShape::northEast;
                if (s && w) shape = RailShape::southWest;
                if (s && e) shape = RailShape::southEast;
            }
        }
        shape = withSlope(shape);
        auto ends = railConnections(pos, shape);
        state = sim.registry.with(state, "shape", std::string(railNames[static_cast<std::size_t>(shape)]));
        if (first || sim.world.get(pos) != state) {
            sim.setBlock(pos, state);
            for (auto end : ends) if (auto neighbor = railAt(sim, end)) {
                RailConnection rail(sim, *neighbor); rail.removeSoft();
                if (rail.accepts(pos)) rail.connect(pos);
            }
        }
    }
};
}

void Simulator::placeRail(BlockPos pos) {
    RailConnection rail(*this, pos);
    rail.place(bestSignal(pos) > 0, true);
    // 原版 BaseRailBlock.updateState 只为“直线型”铁轨发通知，而且走 FullNeighborUpdate：
    //   state = this.updateDir(level, pos, state, true);
    //   if (this.isStraight) level.neighborChanged(state, pos, this, null, movedByPiston);
    // 带的快照是 `updateDir` 的**返回值**，也就是 `RailState.getState()`——RailState 自己
    // 在 `place()` 里算出来并写进世界的那一份（RailState.java:331-333、:349）。
    // RailState 之后**不再重读世界**：`place()` 里对相邻铁轨的 `connectTo` 级联会继续改世界，
    // 但 `this.state` 保持不变。所以这里必须用 `rail.state`，不能用 `world.get(pos)`：
    // 级联可能已经把本格改成别的形状/通电状态，那样快照就提前变“新”了。
    if (isRail(at(pos).device) && at(pos).device != Device::rail)
        neighborChangedSnapshot(pos, rail.state, world.get(pos));
    if (at(pos).device == Device::detectorRail) updateDetectorRail(pos);
}

void Simulator::removeRail(BlockPos pos, StateId state) {
    if (slopeDirection(railShape(*this, state))) updateNeighbors(pos.relative(Direction::up), -1, state);
    if (registry[state].device != Device::rail) { updateNeighbors(pos, -1, state); updateNeighbors(pos.relative(Direction::down), -1, state); }
}

bool Simulator::poweredRailPath(BlockPos pos, StateId state, bool forward, int depth) const {
    if (depth >= 8) return false;
    auto shape = railShape(*this, state);
    auto slope = slopeDirection(shape);
    bool eastWest = shape == RailShape::eastWest || shape == RailShape::ascendingEast || shape == RailShape::ascendingWest;
    auto direction = eastWest ? (forward ? Direction::west : Direction::east) : (forward ? Direction::south : Direction::north);
    auto next = pos.relative(direction);
    bool climbing = slope && *slope == direction;
    if (climbing) next = next.relative(Direction::up);
    auto poweredAt = [&](BlockPos candidate) {
        auto id = world.get(candidate);
        if (registry[id].type != registry[state].type || !registry[id].powered) return false;
        auto candidateShape = railShape(*this, id);
        bool candidateEastWest = candidateShape == RailShape::eastWest || candidateShape == RailShape::ascendingEast || candidateShape == RailShape::ascendingWest;
        return candidateEastWest == eastWest && (bestSignal(candidate) > 0 || poweredRailPath(candidate, id, forward, depth + 1));
    };
    return poweredAt(next) || (!climbing && poweredAt(next.relative(Direction::down)));
}

// state 是 FullNeighborUpdate 入队时的**快照**，不是当前世界里的方块。原版
// BaseRailBlock.neighborChanged(state, ...) 全程用这个参数，PoweredRailBlock 更是把
// 整个快照写回：`setBlock(pos, state.setValue(POWERED, shouldPower), 3)`。
// 因此同一层里铁轨形状被改掉时，这条更新会把**旧形状**连同新的通电状态一起写回去。
void Simulator::updateRail(BlockPos pos, StateId state, StateId source) {
    auto shape = railShape(*this, state);
    auto slope = slopeDirection(shape);
    if (!survives(pos, state) || (slope && (at(pos.relative(*slope)).rigidMask & 2u) == 0)) { setBlock(pos, 0); return; }
    if (registry[state].device == Device::rail) {
        if (!registry[registry.type(source).defaultState].signalSource) return;
        int count = 0; for (auto d : horizontal) if (railAt(*this, pos.relative(d))) ++count;
        // 原版 RailBlock.updateState 走 `this.updateDir(level, pos, state, false)`，
        // 里面是 `new RailState(level, pos, state)`——同样以**快照**为准，而不是重读世界。
        if (count == 3) RailConnection(*this, pos, state).place(bestSignal(pos) > 0, false);
    } else if (registry[state].device == Device::poweredRail || registry[state].device == Device::activatorRail) {
        bool powered = bestSignal(pos) > 0 || poweredRailPath(pos, state, true, 0) || poweredRailPath(pos, state, false, 0);
        if (powered != registry[state].powered) {
            setBlock(pos, registry.withBool(state, "powered", powered));
            updateNeighbors(pos.relative(Direction::down), -1, state);
            if (slope) updateNeighbors(pos.relative(Direction::up), -1, state);
        }
    }
}

void Simulator::updateDetectorRail(BlockPos pos) {
    auto state = world.get(pos);
    if (at(pos).device != Device::detectorRail || !survives(pos, state)) return;
    auto found = runtime.find(pos);
    bool occupied = found != runtime.end() && !found->second.values.value("carts", Json::array()).empty();
    if (occupied != registry[state].powered) {
        auto next = registry.withBool(state, "powered", occupied);
        setBlock(pos, next);
        // 原版 DetectorRailBlock.updatePowerToConnected 同样是带快照的通知。
        for (auto connection : railConnections(pos, railShape(*this, next)))
            neighborChangedSnapshot(connection, world.get(connection), world.get(connection));
        updateNeighbors(pos, -1, state); updateNeighbors(pos.relative(Direction::down), -1, state);
    }
    if (occupied) schedule(pos, 20);
    updateComparatorNeighbors(pos);
}

Json Simulator::normalizeCarts(const Json& carts) const {
    if (!carts.is_array() || carts.size() > 1024) throw std::invalid_argument("矿车接触输入必须是最多 1024 项的数组");
    Json normalized = Json::array();
    for (const auto& cart : carts) {
        auto type = cart.at("type").get<std::string>();
        std::size_t size = type == "chest_minecart" ? 27 : type == "hopper_minecart" ? 5 : 0;
        if (size == 0 && type != "minecart" && type != "furnace_minecart" && type != "tnt_minecart" && type != "command_block_minecart")
            throw std::invalid_argument("不支持此矿车接触类型");
        Json row{{"type", type}};
        // 命令方块矿车的比较器读数是它内部命令方块的 successCount。本项目不实现命令解释器，
        // 该计数作为显式外部输入给出，与讲台页数、标靶命中同属受限实体输入。
        if (type == "command_block_minecart") {
            if (!cart.contains("successCount")) throw std::invalid_argument("命令方块矿车需要显式的 successCount");
            const auto& value = cart.at("successCount");
            if (!value.is_number_integer() || value.get<std::int64_t>() < 0 || value.get<std::int64_t>() > 15)
                throw std::invalid_argument("successCount 必须是 0–15 的整数");
            row["successCount"] = value.get<int>();
        } else if (cart.contains("successCount")) throw std::invalid_argument("只有命令方块矿车接受 successCount");
        auto edits = parseInventory(cart.value("inventory", Json::array()), size);
        if (size) {
            row["inventory"] = Json::array();
            for (const auto& [slot, stack] : edits) if (stack.count) row["inventory"].push_back({{"slot", slot}, {"item", registry.item(stack.item).name}, {"count", stack.count}});
        }
        normalized.push_back(std::move(row));
    }
    return normalized;
}

const Json* Simulator::firstContainerCart(BlockPos pos) const {
    auto found = runtime.find(pos);
    if (found == runtime.end() || !found->second.values.contains("carts")) return nullptr;
    for (const auto& cart : found->second.values.at("carts"))
        if (cart.at("type") == "chest_minecart" || cart.at("type") == "hopper_minecart") return &cart;
    return nullptr;
}

int Simulator::cartAnalog(BlockPos pos) const {
    // 原版 DetectorRailBlock.getAnalogOutputSignal 先找命令方块矿车，找到就直接返回它的
    // successCount，容器矿车根本不参与比较。
    auto found = runtime.find(pos);
    if (found != runtime.end() && found->second.values.contains("carts"))
        for (const auto& cart : found->second.values.at("carts"))
            if (cart.at("type") == "command_block_minecart") return cart.at("successCount").get<int>();
    auto cart = firstContainerCart(pos);
    if (!cart) return 0;
    float fullness = 0;
    for (const auto& stack : cart->at("inventory")) fullness += stack.at("count").get<float>() / static_cast<float>(registry.item(registry.itemId(stack.at("item"))).maxStack);
    fullness /= cart->at("type") == "chest_minecart" ? 27.0F : 5.0F;
    return static_cast<int>(std::floor(fullness * 14.0F)) + (fullness > 0 ? 1 : 0);
}

void Simulator::setCartInput(BlockPos pos, const Json& input) {
    Json carts;
    if (input.contains("carts")) carts = normalizeCarts(input.at("carts"));
    else if (input.contains("cartInventory")) {
        auto current = firstContainerCart(pos);
        if (!current) throw std::invalid_argument("检测区内没有容器矿车");
        auto edits = parseInventory(input.at("cartInventory"), current->at("type") == "chest_minecart" ? 27 : 5);
        carts = runtime.at(pos).values.at("carts");
        for (auto& cart : carts) if (cart.at("type") == "chest_minecart" || cart.at("type") == "hopper_minecart") {
            auto& inventory = cart.at("inventory");
            for (const auto& [slot, stack] : edits) {
                for (auto it = inventory.begin(); it != inventory.end();) { if (it->at("slot") == slot) it = inventory.erase(it); else ++it; }
                if (stack.count) inventory.push_back({{"slot", slot}, {"item", registry.item(stack.item).name}, {"count", stack.count}});
            }
            break;
        }
        carts = normalizeCarts(carts);
    } else throw std::invalid_argument("探测铁轨需要 carts 或 cartInventory 输入");
    runtime[pos].values["carts"] = std::move(carts);
    // Cart inventories do not notify adjacent block comparators themselves.
    // The detector's next 20-gt poll performs the original output notification.
    runtimeChanged(pos, false);
    if (!at(pos).powered) updateDetectorRail(pos);
}
}
