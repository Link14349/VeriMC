#include "simulator/simulator.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace simulator {
namespace {
bool isDiode(Device d) { return d == Device::repeater || d == Device::comparator; }
std::size_t sideIndex(Direction d) { for (std::size_t i = 0; i < 4; ++i) if (horizontal[i] == d) return i; return 0; }
unsigned javaPosBucket(BlockPos p) {
    auto hash = (static_cast<std::uint32_t>(p.y) + static_cast<std::uint32_t>(p.z) * 31u) * 31u + static_cast<std::uint32_t>(p.x);
    return (hash ^ (hash >> 16u)) & 15u;
}
}
int Simulator::directSignal(BlockPos pos, Direction dir, bool includeWire) const {
    auto id = world.get(pos); const auto& state = registry[id];
    if (state.device == Device::wire) {
        if (!includeWire || dir == Direction::down || state.power == 0) return 0;
        return dir == Direction::up || connectionSides(pos, id)[sideIndex(opposite(dir))] != 0 ? state.power : 0;
    }
    if (state.device == Device::comparator) return state.powered && dir == state.facing ? analogOutput(pos) : 0;
    if (state.device == Device::container && registry.type(id).name == "minecraft:trapped_chest") {
        auto it = runtime.find(pos); return dir == Direction::up && it != runtime.end() ? std::clamp(it->second.values.value("viewers", 0), 0, 15) : 0;
    }
    return state.strong[static_cast<unsigned>(dir)];
}
int Simulator::signal(BlockPos pos, Direction dir, bool includeWire) const {
    const auto& state = at(pos);
    int result = state.device == Device::wire || state.device == Device::comparator ? directSignal(pos, dir, includeWire) : state.weak[static_cast<unsigned>(dir)];
    if (state.device == Device::container && registry.type(world.get(pos)).name == "minecraft:trapped_chest") { auto it = runtime.find(pos); result = it == runtime.end() ? 0 : std::clamp(it->second.values.value("viewers", 0), 0, 15); }
    if(state.device==Device::jukebox)result=jukeboxPlaying(pos)?15:0;
    if (state.conductor) for (auto d : directions) { result = std::max(result, directSignal(pos.relative(d), d, includeWire)); if (result == 15) break; }
    return result;
}
int Simulator::bestSignal(BlockPos pos, bool includeWire) const {
    int result = 0;
    for (auto dir : directions) { result = std::max(result, signal(pos.relative(dir), dir, includeWire)); if (result == 15) break; }
    return result;
}
int Simulator::analogOutput(BlockPos pos) const {
    auto id = world.get(pos); const auto& state = registry[id];
    auto it = runtime.find(pos);
    if (state.device == Device::comparator) return it == runtime.end() ? 0 : it->second.output;
    if (state.device == Device::bulb) return state.lit ? 15 : 0;
    if (state.device == Device::detectorRail) return state.powered ? cartAnalog(pos) : 0;
    if (isSensor(state.device)) return registry.property(id,"sculk_sensor_phase")=="active" && it!=runtime.end()?it->second.values.value("lastVibrationFrequency",0):0;
    if (isBookshelf(id)) return it==runtime.end()?0:it->second.values.value("lastInteractedSlot",-1)+1;
    if(state.device==Device::jukebox){auto stack=stackAt({pos,0});const int song=stack.count?registry.item(stack.item).jukeboxSong:-1;return song<0?0:registry.song(song).comparatorOutput;}
    if (inventorySize(id)) return containerAnalog(pos);
    if (state.device == Device::analog || state.device==Device::composter) return state.staticAnalog;
    if (state.device == Device::lectern) {
        if (registry.property(id, "has_book") != "true" || it == runtime.end()) return 0;
        int pages = it->second.values.value("pages", 0), page = it->second.values.value("page", 0);
        float progress = pages > 1 ? static_cast<float>(page) / static_cast<float>(pages - 1) : 1.0F;
        return static_cast<int>(std::floor(progress * 14.0F)) + 1;
    }
    return it == runtime.end() ? 0 : it->second.output;
}
int Simulator::displayValue(BlockPos pos) const {
    const auto& state = at(pos);
    if(state.device==Device::jukebox)return jukeboxPlaying(pos)?15:0;
    if(isSensor(state.device)) return state.power;
    if (state.device == Device::wire || state.device == Device::target || state.device == Device::daylight || state.device == Device::weightedPlate) return state.power;
    if (state.device == Device::comparator || state.device == Device::analog || state.device==Device::composter) return analogOutput(pos);
    if (state.device == Device::lamp || state.device == Device::bulb || state.device == Device::torch || state.device == Device::wallTorch) return state.lit ? 15 : 0;
    if (inventorySize(world.get(pos)) && registry.type(world.get(pos)).name != "minecraft:trapped_chest") return containerAnalog(pos);
    if (state.device == Device::piston) return state.extended ? 15 : 0;
    if (state.device == Device::poweredRail || state.device == Device::activatorRail) return state.powered ? 15 : 0;
    if (state.device == Device::tripwire) return state.powered ? 15 : 0;
    if (state.device == Device::door || state.device == Device::trapdoor || state.device == Device::fenceGate) return registry.property(world.get(pos), "open") == "true" ? 15 : 0;
    int value = 0; for (auto d : directions) value = std::max(value, signal(pos, d));
    return value;
}
void Simulator::appendUpdateTrace(Json entry) {
    if (updateTrace.size() >= updateTraceLimit) { updateTraceTruncated = true; return; }
    updateTrace.push_back(std::move(entry));
}
// 每次“取栈顶”记一条，描述原版即将执行的是哪一种更新以及它来自哪里：
// "m" 多向更新（来源格、跳过方向、已推进下标、来源方块），
// "s" 形状更新（目标格、邻居格、方向、标志），
// "n" 简单邻居更新（目标格、来源方块），
// "f" 带状态快照的邻居更新（另记快照 stateId 与 movedByPiston）。
// 坐标写绝对值，checkReference 按种类换算成相对坐标再比较。
void Simulator::recordUpdateTrace(const Update& update) {
    Json entry = Json::array();
    const auto addPos = [&](BlockPos p) { entry.push_back(p.x); entry.push_back(p.y); entry.push_back(p.z); };
    switch (update.kind) {
    case UpdateKind::multi:
        entry.push_back("m"); addPos(update.pos);
        if (update.skip < 0) entry.push_back(nullptr); else entry.push_back(directionNames[static_cast<unsigned>(update.skip)]);
        entry.push_back(update.index);
        entry.push_back(registry.type(update.neighborState).name);
        break;
    case UpdateKind::shape:
        entry.push_back("s"); addPos(update.pos); addPos(update.pos.relative(update.direction));
        entry.push_back(directionNames[static_cast<unsigned>(update.direction)]);
        entry.push_back(update.flags);
        break;
    default:
        if (update.snapshot == noSnapshot) {
            entry.push_back("n"); addPos(update.pos);
            entry.push_back(registry.type(update.neighborState).name);
        } else {
            entry.push_back("f"); addPos(update.pos);
            entry.push_back(registry.type(update.neighborState).name);
            entry.push_back(update.snapshot);
            entry.push_back(update.movedByPiston);
        }
        break;
    }
    appendUpdateTrace(std::move(entry));
}
void Simulator::enqueue(Update update) {
    if (++updateCount > updateBudget) {
        breakRequested = true; faulted = true; pauseReason = "邻居更新超过预算，电路状态已停止；请从快照恢复或撤销本次操作";
        throw std::runtime_error(pauseReason);
    }
    if (updating) { addedUpdates.push_back(update); return; }
    updating = true; updateStack.push_back(update);
    try {
        // 原版在每次“取栈顶”时调用 debugListener；只有换了栈顶对象才算新的一次取出。
        bool peeked = false;
        while (!updateStack.empty() || !addedUpdates.empty()) {
            if (!addedUpdates.empty()) peeked = false;
            for (auto it = addedUpdates.rbegin(); it != addedUpdates.rend(); ++it) updateStack.push_back(*it);
            addedUpdates.clear();
            auto current = updateStack.back();
            if (updateTraceLimit && !peeked) recordUpdateTrace(current);
            peeked = true;
            if (current.kind == UpdateKind::multi) {
                // 与原版 MultiNeighborUpdate.runNext 一致：先取当前方向，再跳过被排除的方向，
                // 用推进后的下标判断是否还有剩余；耗尽时**当场**出栈，不多留一轮。
                auto index = static_cast<std::size_t>(current.index);
                auto d = updateOrder[index++];
                if (index < 6 && static_cast<int>(updateOrder[index]) == current.skip) ++index;
                updateStack.back().index = static_cast<int>(index);
                const bool exhausted = index >= 6;
                executeNeighbor(current.pos.relative(d), current.neighborState);
                if (exhausted) { updateStack.pop_back(); peeked = false; }
            } else {
                updateStack.pop_back(); peeked = false;
                if (current.kind == UpdateKind::shape) executeShape(current); else executeNeighbor(current.pos, current.neighborState, current.snapshot);
            }
            ++statistics.updates;
        }
        updating = false; updateCount = 0;
    } catch (...) {
        // A budget/error poisons this run, rather than silently continuing after dropped updates.
        updating = false; updateStack.clear(); addedUpdates.clear(); updateCount = 0;
        breakRequested = true; faulted = true; throw;
    }
}
void Simulator::updateNeighbors(BlockPos p, int skip, StateId source) {
    Update u{UpdateKind::multi, p}; u.skip = skip; u.neighborState = source == UINT32_MAX ? world.get(p) : source;
    // 原版构造 MultiNeighborUpdate 时就跳过首个被排除的方向。
    if (static_cast<int>(updateOrder[0]) == skip) u.index = 1;
    enqueue(u);
}
void Simulator::neighborChanged(BlockPos p, StateId source) { Update u{UpdateKind::neighbor, p}; u.neighborState = source; enqueue(u); }
// 原版 Level.neighborChanged(BlockState, ...) 走 FullNeighborUpdate：入队时就固定目标状态。
void Simulator::neighborChangedSnapshot(BlockPos p, StateId snapshot, StateId source, bool movedByPiston) {
    Update u{UpdateKind::neighbor, p}; u.neighborState = source; u.snapshot = snapshot; u.movedByPiston = movedByPiston; enqueue(u);
}
void Simulator::notifyFront(BlockPos p, Direction facing, StateId source) {
    auto out = p.relative(opposite(facing)); auto block = source == UINT32_MAX ? world.get(p) : source;
    neighborChanged(out, block); updateNeighbors(out, static_cast<int>(facing), block);
}
void Simulator::notifyAttached(BlockPos p, Direction connected, StateId source) {
    auto block = source == UINT32_MAX ? world.get(p) : source;
    updateNeighbors(p, -1, block); updateNeighbors(p.relative(opposite(connected)), -1, block);
}
void Simulator::setBlock(BlockPos p, StateId id, unsigned flags, int depth) {
    if (faulted) throw std::runtime_error("当前执行已中止，请撤销、加载快照或新建电路");
    const auto old = world.get(p);
    if (old == id) return;
    const auto& state = registry[id]; const auto& oldState = registry[old];
    world.set(p, id); changes[p] = id; ++revision; ++sequence; ++statistics.stateChanges;
    sampleAffected(p);
    if (oldState.type != state.type) {
        if(oldState.device==Device::bell && runtime.contains(p) && runtime.at(p).values.contains("bellGeneration"))scheduledKeys.erase({p,oldState.type,2,runtime.at(p).values.at("bellGeneration")});
        if(oldState.device==Device::jukebox)removeJukebox(p,oldState.type);
        if(isSensor(oldState.device)) removeSensor(p,oldState.type);
        if (!(oldState.device==Device::container && state.device==Device::container && isCopperChest(old) && isCopperChest(id))) runtime.erase(p);
        scheduledKeys.erase({p, oldState.type, 3, 0});
        if (auto motion = motions.find(p); motion != motions.end()) {
            scheduledKeys.erase({p, oldState.type, 2, motion->second.generation});
            motions.erase(motion);
        }
        if (oldState.device == Device::daylight) scheduledKeys.erase({p, oldState.type, 2, 0});
        if (auto hopper = hoppers.find(p); hopper != hoppers.end()) {
            scheduledKeys.erase({p, oldState.type, 2, hopper->second.generation});
            hoppers.erase(hopper);
        }
        entityOrders.erase(p);
    }
    // 原版在 movedByPiston 为真时仍然调用，由各方块自行决定是否跳过。
    if ((oldState.type != state.type || isRail(state.device)) && ((flags & 1u) != 0 || (flags & 64u) != 0)) onRemove(p, old, (flags & 64u) != 0);
    if(oldState.type!=state.type && isSensor(state.device))startSensor(p);
    if(oldState.type!=state.type && state.device==Device::bell)registerEntity(p);
    if(oldState.type!=state.type && state.device==Device::jukebox)startJukebox(p);
    if ((flags & 512u) == 0) onPlace(p, id, old);
    if (world.get(p) != id) return;
    if(state.device==Device::jukebox && oldState.type==state.type && registry.property(old,"has_record")!=registry.property(id,"has_record"))updateJukeboxTicker(p);
    if ((flags & 1u) != 0) { updateNeighbors(p, -1, old); if (state.analogSource) updateComparatorNeighbors(p); }
    if ((flags & 16u) == 0 && depth > 0) {
        const unsigned nextFlags = flags & ~33u;
        indirectShapes(p, old, nextFlags, depth - 1);
        for (auto dir : shapeOrder) enqueue({UpdateKind::shape, p.relative(dir), opposite(dir), id, 0, -1, depth - 1, nextFlags});
        indirectShapes(p, id, nextFlags, depth - 1);
    }
    if (!hoppers.empty()) wakeHoppers(p);
    if (state.device == Device::button && at(p).device == Device::button && !at(p).powered) {
        auto contact = runtime.find(p);
        const auto& name = registry.type(world.get(p)).name;
        if (contact != runtime.end() && contact->second.values.value("arrows", 0) > 0 && name != "minecraft:stone_button" && name != "minecraft:polished_blackstone_button")
            schedulePhase(p, currentTick + (currentPhase < 3 ? 0 : 1), 3, 0);
    }
}
bool Simulator::survives(BlockPos p, StateId id) const {
    const auto& s = registry[id];
    auto below = [&]() -> const BlockState& { return at(p.relative(Direction::down)); };
    if(s.device==Device::solid && registry.type(id).className=="WoolCarpetBlock")return below().device!=Device::air;
    switch (s.device) {
    case Device::bell: {auto support=bellSupport(id);const auto& neighbor=at(p.relative(support));return support==Direction::up?(neighbor.device!=Device::fenceGate && (neighbor.centerMask&1u)!=0):(neighbor.supportMask&(1u<<static_cast<unsigned>(opposite(support))))!=0;}
    case Device::wire: { const auto& support = below(); return (support.supportMask & 2u) != 0 || support.device == Device::hopper; }
    case Device::repeater: case Device::comparator: return (below().rigidMask & 2u) != 0;
    case Device::torch: return (below().centerMask & 2u) != 0;
    case Device::wallTorch: return (at(p.relative(opposite(s.facing))).supportMask & (1u << static_cast<unsigned>(s.facing))) != 0;
    case Device::tripwireHook: return axis(s.facing) != 0 && (at(p.relative(opposite(s.facing))).supportMask & (1u << static_cast<unsigned>(s.facing))) != 0;
    case Device::lever: case Device::button: return (at(p.relative(opposite(s.connectedDirection))).supportMask & (1u << static_cast<unsigned>(s.connectedDirection))) != 0;
    case Device::pressurePlate: case Device::weightedPlate: { const auto& support = below(); return (support.rigidMask & 2u) != 0 || (support.centerMask & 2u) != 0; }
    case Device::rail: case Device::poweredRail: case Device::activatorRail: case Device::detectorRail: return (below().rigidMask & 2u) != 0;
    case Device::door: return registry.property(id, "half") == "lower" ? (below().supportMask & 2u) != 0 : below().type == s.type;
    case Device::pistonHead: { const auto& base = at(p.relative(opposite(s.facing))); return (base.device == Device::piston && base.extended && base.facing == s.facing && base.sticky == (registry.property(id, "type") == "sticky")) || (base.device == Device::movingPiston && base.facing == s.facing); }
    default: return true;
    }
}
void Simulator::place(BlockPos p, StateId id) {
    if (registry.type(id).supportLevel == "unimplemented") throw std::invalid_argument("该器件尚未实现：" + registry.type(id).name);
    if (registry[id].device == Device::movingPiston || registry[id].device == Device::pistonHead) throw std::invalid_argument("活塞运动状态由仿真产生，不能直接放置");
    if (registry[id].device == Device::door && at(p).type != registry[id].type) {
        id = registry.with(id, "half", std::string("lower"));
        auto above = p.relative(Direction::up);
        if (p.y >= 319 || !at(above).replaceable || !survives(p, id)) throw std::invalid_argument("门需要下方支撑及上方一格空间");
        bool powered = bestSignal(p) > 0 || bestSignal(above) > 0;
        id = registry.withBool(registry.withBool(id, "powered", powered), "open", powered);
        setBlock(p, id);
        setBlock(above, registry.with(id, "half", std::string("upper")));
        return;
    }
    if(registry[id].device==Device::noteBlock && at(p).type!=registry[id].type)id=noteInstrument(p,id);
    if (!survives(p, id)) throw std::invalid_argument("这个位置缺少器件所需的支撑面");
    if (registry[id].device == Device::tripwire) {
        for (auto d : horizontal) id = registry.withBool(id, directionNames[static_cast<unsigned>(d)], connectsTripwire(world.get(p.relative(d)), d));
    }
    if (registry[id].device == Device::tripwireHook && at(p).type != registry[id].type)
        id = registry.withBool(registry.withBool(id, "attached", false), "powered", false);
    if (registry[id].device == Device::wire) {
        for (auto d : horizontal) id = registry.with(id, directionNames[static_cast<unsigned>(d)], std::string("side"));
        id = wireConnections(p, id);
    }
    if ((registry[id].device == Device::trapdoor || registry[id].device == Device::fenceGate) && at(p).type != registry[id].type) {
        bool powered = bestSignal(p) > 0;
        id = registry.withBool(registry.withBool(id, "powered", powered), "open", powered);
        // 原版 FenceGateBlock.getStateForPlacement 按垂直于朝向的两侧是否是墙决定 IN_WALL。
        if (registry[id].device == Device::fenceGate) {
            const auto side = clockWise(registry[id].facing);
            id = registry.withBool(id, "in_wall", registry.type(world.get(p.relative(side))).wall || registry.type(world.get(p.relative(opposite(side)))).wall);
        }
    }
    // 原版 StairBlock.getStateForPlacement 在放置时就算出连接形状。
    if (registry.type(id).stairs) id = stairsShape(p, id);
    if (registry[id].device == Device::container && at(p).type != registry[id].type) id = placedChest(p, id);
    if (registry[id].device == Device::lamp) id = registry.withBool(id, "lit", bestSignal(p) != 0);
    setBlock(p, id);
    if (registry[id].device == Device::tripwireHook && at(p).type == registry[id].type) calculateTripwire(p, world.get(p), false, false);
    if (isDiode(registry[id].device) && (registry[id].device == Device::comparator ? comparatorInput(p) : diodeInput(p)) > 0) schedule(p, 1);
}
void Simulator::onPlace(BlockPos p, StateId id, StateId old) {
    const auto& s = registry[id];
    if(s.device==Device::composter && s.staticAnalog==7)schedule(p,20);
    if (isDiode(s.device)) { notifyFront(p, s.facing); return; }
    if (s.device == Device::torch || s.device == Device::wallTorch) { for (auto d : directions) updateNeighbors(p.relative(d), -1, id); return; }
    if (registry[old].type == s.type) return;
    switch (s.device) {
    case Device::jukebox: startJukebox(p);break;
    case Device::bell: registerEntity(p);break;
    case Device::sculkSensor: case Device::calibratedSensor:
        startSensor(p);if(s.power && !hasScheduled(p))setBlock(p,registry.with(id,"power",0),18);break;
    case Device::target: if (s.power && !hasScheduled(p)) setBlock(p, registry.with(id, "power", 0), 18); break;
    case Device::wire: updateWire(p, id); updateNeighbors(p.relative(Direction::up), -1, id); updateNeighbors(p.relative(Direction::down), -1, id); wireCorners(p); break;
    case Device::observer: if (s.powered && !hasScheduled(p)) { setBlock(p, registry.withBool(id, "powered", false), 18); notifyFront(p, s.facing); } break;
    // 原版 LightningRodBlock.onPlace 会为仍处于供电状态的避雷针补排 8 gt 的熄灭刻。
    case Device::lightningRod: if (s.powered && !hasScheduled(p)) schedule(p, 8); break;
    case Device::bulb: executeNeighbor(p); break;
    case Device::daylight: schedulePhase(p, currentTick + 20 - currentTick % 20, 2, 0); break;
    case Device::hopper: executeNeighbor(p); startHopper(p); break;
    case Device::rail: case Device::poweredRail: case Device::activatorRail: case Device::detectorRail: placeRail(p); break;
    case Device::piston: checkPiston(p); break;
    case Device::tripwire:
        updateTripwireSource(p, id);
        if (runtime.contains(p) && runtime.at(p).values.value("entities", 0) > 0) schedulePhase(p, currentTick + 1, 3, 0);
        break;
    default: break;
    }
}
void Simulator::onRemove(BlockPos p, StateId old, bool movedByPiston) {
    const auto& s = registry[old];
    // 原版只有下列方块在 affectNeighborsAfterRemoval 里检查 movedByPiston 并跳过；
    // 侦测器、避雷针、讲台、容器、活塞头等不检查，被活塞移动时同样发出通知。
    switch (s.device) {
    case Device::sculkSensor: case Device::calibratedSensor:
        if(registry.property(old,"sculk_sensor_phase")=="active") {updateNeighbors(p,-1,old);updateNeighbors(p.relative(Direction::down),-1,old);}break;
    case Device::wire: if (movedByPiston) break; for (auto d : directions) updateNeighbors(p.relative(d), -1, old); updateWire(p, old); wireCorners(p); break;
    case Device::torch: case Device::wallTorch: if (movedByPiston) break; for (auto d : directions) updateNeighbors(p.relative(d), -1, old); break;
    case Device::lever: case Device::button: if (!movedByPiston && s.powered) notifyAttached(p, s.connectedDirection, old); break;
    case Device::repeater: case Device::comparator: if (!movedByPiston) notifyFront(p, s.facing, old); break;
    // 原版还要求 hasScheduledTick，且必须按被移除的旧类型查询：此时世界上已经是新方块。
    case Device::observer: if (s.powered && blockTicks.hasScheduled(p, s.type)) notifyFront(p, s.facing, old); break;
    case Device::pressurePlate: case Device::weightedPlate: if (!movedByPiston && (s.powered || s.power > 0)) { updateNeighbors(p, -1, old); updateNeighbors(p.relative(Direction::down), -1, old); } break;
    case Device::lightningRod: if (s.powered) updateNeighbors(p.relative(opposite(s.facing)), -1, old); break;
    case Device::lectern: if (s.powered) updateNeighbors(p.relative(Direction::down), -1, old); break;
    case Device::container: case Device::hopper: case Device::dropper: updateComparatorNeighbors(p); break;
    case Device::analog: if(isBookshelf(old) || isDecoratedPot(old)) updateComparatorNeighbors(p); break;
    case Device::rail: case Device::poweredRail: case Device::activatorRail: case Device::detectorRail: if (!movedByPiston) removeRail(p, old); break;
    case Device::tripwire: if (!movedByPiston) updateTripwireSource(p, registry.withBool(old, "powered", true)); break;
    case Device::tripwireHook: if (!movedByPiston) removeTripwireHook(p, old); break;
    case Device::pistonHead: { auto base = p.relative(opposite(s.facing)); if (at(base).device == Device::piston && at(base).extended && at(base).facing == s.facing) setBlock(base, 0); break; }
    default: break;
    }
}
bool Simulator::connectsWire(StateId id, int direction) const {
    const auto& s = registry[id];
    if (s.device == Device::wire) return true;
    if (direction < 0) return false;
    auto dir = static_cast<Direction>(direction);
    if (s.device == Device::repeater) return s.facing == dir || s.facing == opposite(dir);
    if (s.device == Device::observer) return s.facing == dir;
    return s.signalSource;
}
std::uint8_t Simulator::wireSide(BlockPos p, Direction d) const {
    auto q = p.relative(d); const auto& n = at(q);
    if (!at(p.relative(Direction::up)).conductor && (n.device == Device::trapdoor || (n.supportMask & 2u) != 0 || n.device == Device::hopper) && connectsWire(world.get(q.relative(Direction::up)), -1)) return (n.supportMask & (1u << static_cast<unsigned>(opposite(d)))) != 0 ? 2 : 1;
    return connectsWire(world.get(q), static_cast<int>(d)) || (!n.conductor && connectsWire(world.get(q.relative(Direction::down)), -1)) ? 1 : 0;
}
std::array<std::uint8_t, 4> Simulator::connectionSides(BlockPos p, StateId id) const {
    auto sides = registry[id].wireSides;
    const bool wasDot = std::all_of(sides.begin(), sides.end(), [](auto s) { return s == 0; });
    for (std::size_t i = 0; i < 4; ++i) sides[i] = wireSide(p, horizontal[i]);
    const bool dot = std::all_of(sides.begin(), sides.end(), [](auto s) { return s == 0; });
    if (!(wasDot && dot)) {
        bool northSouth = sides[0] != 0 || sides[2] != 0, eastWest = sides[1] != 0 || sides[3] != 0;
        if (!northSouth) { if (!sides[1]) sides[1] = 1; if (!sides[3]) sides[3] = 1; }
        if (!eastWest) { if (!sides[0]) sides[0] = 1; if (!sides[2]) sides[2] = 1; }
    }
    return sides;
}
StateId Simulator::wireConnections(BlockPos p, StateId id) const {
    const auto sides = connectionSides(p, id);
    for (std::size_t i = 0; i < 4; ++i) id = registry.with(id, directionNames[static_cast<unsigned>(horizontal[i])], std::string(sides[i] == 2 ? "up" : sides[i] == 1 ? "side" : "none"));
    return id;
}
// 对应原版 StairBlock.getStairsShape：先看朝向前方的楼梯给出外角，再看背后的楼梯给出内角。
StateId Simulator::stairsShape(BlockPos p, StateId id) const {
    const auto facing = registry[id].facing;
    const auto half = registry.property(id, "half");
    auto isStairs = [&](StateId other) { return registry.type(other).stairs; };
    auto canTakeShape = [&](Direction neighbor) {
        const auto other = world.get(p.relative(neighbor));
        return !isStairs(other) || registry[other].facing != facing || registry.property(other, "half") != half;
    };
    const auto behind = world.get(p.relative(facing));
    if (isStairs(behind) && registry.property(behind, "half") == half) {
        const auto behindFacing = registry[behind].facing;
        if (axis(behindFacing) != axis(facing) && canTakeShape(opposite(behindFacing)))
            return registry.with(id, "shape", std::string(behindFacing == counterClockWise(facing) ? "outer_left" : "outer_right"));
    }
    const auto front = world.get(p.relative(opposite(facing)));
    if (isStairs(front) && registry.property(front, "half") == half) {
        const auto frontFacing = registry[front].facing;
        if (axis(frontFacing) != axis(facing) && canTakeShape(frontFacing))
            return registry.with(id, "shape", std::string(frontFacing == counterClockWise(facing) ? "inner_left" : "inner_right"));
    }
    return registry.with(id, "shape", std::string("straight"));
}
void Simulator::updateWire(BlockPos p, StateId id) {
    int power = bestSignal(p, false), neighborPower = 0;
    if (power < 15) for (auto d : horizontal) {
        auto q = p.relative(d); const auto& n = at(q);
        if (n.device == Device::wire) neighborPower = std::max(neighborPower, static_cast<int>(n.power));
        if (n.conductor && !at(p.relative(Direction::up)).conductor) q = q.relative(Direction::up);
        else if (!n.conductor) q = q.relative(Direction::down);
        else continue;
        if (at(q).device == Device::wire) neighborPower = std::max(neighborPower, static_cast<int>(at(q).power));
    }
    power = std::max(power, neighborPower - 1);
    if (registry[id].power == power) return;
    if (world.get(p) == id) setBlock(p, registry.with(id, "power", power), 2);
    // Java 26.2's seven-entry HashSet iteration affects locational redstone.
    std::array<BlockPos, 7> affected{p}; for (std::size_t i = 0; i < 6; ++i) affected[i + 1] = p.relative(directions[i]);
    std::stable_sort(affected.begin(), affected.end(), [](BlockPos a, BlockPos b) { return javaPosBucket(a) < javaPosBucket(b); });
    // 原版把红石粉方块本身作为 sourceBlock 传给集合里每个位置的通知。
    for (auto q : affected) updateNeighbors(q, -1, id);
}
void Simulator::wireCorners(BlockPos p) {
    auto check = [&](BlockPos q) { if (at(q).device == Device::wire) { const auto wire = world.get(q); updateNeighbors(q, -1, wire); for (auto d : directions) updateNeighbors(q.relative(d), -1, wire); } };
    for (auto d : horizontal) check(p.relative(d));
    for (auto d : horizontal) { auto q = p.relative(d); check(q.relative(at(q).conductor ? Direction::up : Direction::down)); }
}
void Simulator::indirectShapes(BlockPos p, StateId id, unsigned flags, int depth) {
    if (registry[id].device != Device::wire) return;
    for (std::size_t i = 0; i < 4; ++i) if (registry[id].wireSides[i] != 0) {
        auto d = horizontal[i]; auto q = p.relative(d);
        if (at(q).device == Device::wire) continue;
        for (auto vertical : {Direction::down, Direction::up}) {
            auto diagonal = q.relative(vertical);
            if (at(diagonal).device == Device::wire) enqueue({UpdateKind::shape, diagonal, opposite(d), world.get(p.relative(vertical)), 0, -1, depth, flags});
        }
    }
}
// 原版 BlockBehaviour.updateShape 的纯状态形式：只返回新状态，副作用限于原版同样会做的排刻。
// 钟和箱子带方块实体、不可被活塞移动，仍由 executeShape 用各自的辅助函数处理。
// 支撑丢失并不是所有方向的形状更新都检查：26.2 每个方块类在 updateShape 里
// 各自写死了检查哪一个方向。地毯是唯一没有方向条件的（CarpetBlock 直接判 canSurvive）。
bool Simulator::supportChecked(StateId id, Direction direction) const {
    const auto& s = registry[id];
    switch (s.device) {
    case Device::wire: case Device::repeater: case Device::comparator: case Device::torch:
    case Device::pressurePlate: case Device::weightedPlate:
        return direction == Direction::down;
    case Device::wallTorch: case Device::pistonHead: case Device::tripwireHook:
        return direction == opposite(s.facing);
    case Device::lever: case Device::button:
        return direction == opposite(s.connectedDirection);
    case Device::door:
        return registry.property(id, "half") == "lower" && direction == Direction::down;
    case Device::solid:
        return registry.type(id).className == "WoolCarpetBlock";
    default:
        // 钟由 updateBellShape 单独处理；其余器件的 survives 恒为真。
        return false;
    }
}
StateId Simulator::shapeUpdated(BlockPos p, StateId id, Direction direction, StateId neighborState) {
    const auto& s = registry[id];
    if (s.device == Device::noteBlock) return axis(direction) == 0 ? noteInstrument(p, id) : id;
    if (isRail(s.device)) return id; // Rail support is checked by neighborChanged.
    if (s.device == Device::tripwireHook) return opposite(direction) == s.facing && !survives(p, id) ? 0 : id;
    if (s.device == Device::tripwire)
        return axis(direction) != 0 ? registry.withBool(id, directionNames[static_cast<unsigned>(direction)], connectsTripwire(neighborState, direction)) : id;
    if (s.device == Device::door && axis(direction) == 0 && ((registry.property(id, "half") == "lower") == (direction == Direction::up))) {
        bool fits = registry[neighborState].device == Device::door && registry.property(neighborState, "half") != registry.property(id, "half");
        return fits ? registry.with(neighborState, "half", registry.property(id, "half")) : 0;
    }
    // 原版只在水平方向的形状更新里重算楼梯 SHAPE，竖直方向落到基类的空实现。
    if (registry.type(id).stairs) return axis(direction) != 0 ? stairsShape(p, id) : id;
    // 原版 FenceGateBlock：垂直于朝向的那条轴上的形状更新会重算 IN_WALL。
    if (s.device == Device::fenceGate && axis(direction) == axis(clockWise(s.facing)))
        return registry.withBool(id, "in_wall", registry.type(neighborState).wall || registry.type(world.get(p.relative(opposite(direction)))).wall);
    if (supportChecked(id, direction) && !survives(p, id)) return 0;
    if (s.device == Device::observer && direction == s.facing && !s.powered && !blockTicks.hasScheduled(p, s.type)) schedule(p, 2, 0, s.type);
    // 原版只比较轴：Y 轴与水平朝向轴必然不同，因此竖直形状更新同样刷新 LOCKED。
    if (s.device == Device::repeater && axis(direction) != axis(s.facing)) return registry.withBool(id, "locked", diodeSideInput(p) > 0);
    if (s.device == Device::wire && direction != Direction::down) {
        StateId next = id;
        if (direction == Direction::up) next = wireConnections(p, id);
        else {
            auto side = wireSide(p, direction); auto index = sideIndex(direction);
            const bool cross = std::all_of(s.wireSides.begin(), s.wireSides.end(), [](auto value) { return value != 0; });
            if ((side != 0) == (s.wireSides[index] != 0) && !cross) next = registry.with(id, directionNames[static_cast<unsigned>(direction)], std::string(side == 2 ? "up" : side == 1 ? "side" : "none"));
            else { for (auto d : horizontal) next = registry.with(next, directionNames[static_cast<unsigned>(d)], std::string("side")); next = wireConnections(p, next); }
        }
        return next;
    }
    return id;
}
// 原版 Block.updateFromNeighbourShapes：按形状更新顺序折叠六次 updateShape，中途不写世界。
StateId Simulator::updateFromNeighborShapes(BlockPos p, StateId id) {
    for (auto d : shapeOrder) id = shapeUpdated(p, id, d, world.get(p.relative(d)));
    return id;
}
void Simulator::executeShape(const Update& u) {
    auto id = world.get(u.pos); const auto& s = registry[id];
    if (s.device == Device::bell) { updateBellShape(u); return; }
    if (s.device == Device::container && registry.has(id, "type")) { updateChestShape(u); return; }
    auto next = shapeUpdated(u.pos, id, u.direction, u.neighborState);
    if (next != id) setBlock(u.pos, next, next == 0 ? 3 : u.flags, u.depth);
}
int Simulator::diodeInput(BlockPos p) const { auto d = at(p).facing; auto q = p.relative(d); return std::max(signal(q, d), at(q).device == Device::wire ? static_cast<int>(at(q).power) : 0); }
int Simulator::diodeSideInput(BlockPos p) const {
    const auto& s = at(p); int result = 0;
    for (auto d : horizontal) if (axis(d) != axis(s.facing)) {
        auto q = p.relative(d); const auto& n = at(q);
        if (s.device == Device::repeater && !isDiode(n.device)) continue;
        int value = n.device == Device::source ? 15 : n.device == Device::wire ? n.power : n.signalSource ? directSignal(q, d) : 0;
        result = std::max(result, value);
    }
    return result;
}
bool Simulator::prioritizeDiode(BlockPos p) const { auto d = opposite(at(p).facing); const auto& n = at(p.relative(d)); return isDiode(n.device) && n.facing != d; }
bool Simulator::torchInput(BlockPos p) const { const auto& s = at(p); auto d = s.device == Device::torch ? Direction::down : opposite(s.facing); return signal(p.relative(d), d) != 0; }
int Simulator::comparatorInput(BlockPos p) const {
    auto d = at(p).facing; auto q = p.relative(d); int input = diodeInput(p);
    if (at(q).analogSource) return analogOutput(q);
    if (input < 15 && at(q).conductor) {
        // 原版取展示框读数与第二格模拟量的较大者；两者都不存在时保留直接输入。
        const auto far = q.relative(d);
        int best = std::numeric_limits<int>::min();
        if (auto frame = itemFrameSignal(q, d)) best = *frame;
        if (at(far).analogSource) best = std::max(best, analogOutput(far));
        if (best != std::numeric_limits<int>::min()) return best;
    }
    return input;
}
void Simulator::refreshComparator(BlockPos p) {
    auto id = world.get(p); const auto& s = registry[id]; int input = comparatorInput(p), side = diodeSideInput(p);
    int output = input == 0 || side > input ? 0 : s.subtract ? input - side : input;
    bool shouldOn = input != 0 && (input > side || (input == side && !s.subtract));
    auto& data = runtime[p]; int old = data.output; data.output = output;
    if (old != output || !s.subtract) {
        if (s.powered != shouldOn) setBlock(p, registry.withBool(id, "powered", shouldOn), 2);
        ++sequence; sampleAffected(p); changes[p] = world.get(p); notifyFront(p, s.facing);
    }
}
void Simulator::executeNeighbor(BlockPos p, StateId source, StateId snapshot) {
    // 快照形式使用入队时记下的状态；简单形式在这里才读世界。
    const auto id = snapshot == noSnapshot ? world.get(p) : snapshot;
    // These classes have no neighborChanged behavior. Their shape updates
    // still run separately, and enqueue still counts every notification.
    // Keep this common path outside the large reactive handler's stack frame.
    switch (registry[id].device) {
    case Device::air: case Device::solid: case Device::source:
    case Device::lever: case Device::button: return;
    default: executeReactiveNeighbor(p, id, source); break;
    }
}
void Simulator::executeReactiveNeighbor(BlockPos p, StateId id, StateId source) {
    const auto& s = registry[id];
    if (isDiode(s.device) && !survives(p, id)) {
        // 原版 DiodeBlock.neighborChanged 在邻居通知阶段就掉落并移除二极管，
        // 随后对六个邻居各发一次 updateNeighborsAt(pos.relative(d), this)。
        setBlock(p, 0, 3);
        for (auto d : directions) updateNeighbors(p.relative(d), -1, id);
        return;
    }
    switch (s.device) {
    case Device::dropper: {
        bool powered = bestSignal(p) > 0 || bestSignal(p.relative(Direction::up)) > 0;
        bool triggered = registry.property(id, "triggered") == "true";
        if (powered && !triggered) { schedule(p, 4); setBlock(p, registry.withBool(id, "triggered", true), 2); }
        else if (!powered && triggered) setBlock(p, registry.withBool(id, "triggered", false), 2);
        break;
    }
    case Device::wire: if (survives(p, id)) updateWire(p, id); else setBlock(p, 0); break;
    case Device::torch: case Device::wallTorch: if (s.lit == torchInput(p) && !blockTicks.willTick(p, s.type)) schedule(p, 2); break;
    case Device::repeater: if (diodeSideInput(p) == 0 && s.powered != (diodeInput(p) > 0) && !blockTicks.willTick(p, s.type)) schedule(p, static_cast<Tick>(s.delay) * 2, prioritizeDiode(p) ? -3 : s.powered ? -2 : -1); break;
    case Device::comparator: {
        int input = comparatorInput(p), side = diodeSideInput(p); int output = input == 0 || side > input ? 0 : s.subtract ? input - side : input;
        bool shouldOn = input > 0 && (input > side || (input == side && !s.subtract));
        if ((output != analogOutput(p) || s.powered != shouldOn) && !blockTicks.willTick(p, s.type)) schedule(p, 2, prioritizeDiode(p) ? -1 : 0);
        break;
    }
    case Device::lamp: if (s.lit != (bestSignal(p) > 0)) { if (s.lit) schedule(p, 4); else setBlock(p, registry.withBool(id, "lit", true), 2); } break;
    case Device::bulb: { bool powered = bestSignal(p) > 0; if (powered != s.powered) { auto next = registry.withBool(id, "powered", powered); if (!s.powered) next = registry.withBool(next, "lit", !s.lit); setBlock(p, next); } break; }
    case Device::door: case Device::trapdoor: case Device::fenceGate: {
        bool powered = bestSignal(p) > 0;
        if (s.device == Device::door) {
            if (registry[source].type == s.type) break;
            powered = powered || bestSignal(p.relative(registry.property(id, "half") == "lower" ? Direction::up : Direction::down)) > 0;
        }
        if (powered != s.powered) {
            const bool changedOpen=(registry.property(id,"open")=="true")!=powered;
            // Door/trapdoor emit before writing state; fence gates emit after.
            auto emit=[&]{(void)worldRandom.nextFloat();emitGameEvent(powered?"block_open":"block_close",p);};
            if(changedOpen && s.device!=Device::fenceGate)emit();
            setBlock(p, registry.withBool(registry.withBool(id, "powered", powered), "open", powered), 2);
            if(changedOpen && s.device==Device::fenceGate)emit();
        }
        break;
    }
    case Device::bell: {bool powered=bestSignal(p)>0;if(powered!=s.powered){if(powered)ringBell(p,s.facing);setBlock(p,registry.withBool(id,"powered",powered));}break;}
    case Device::noteBlock: {bool powered=bestSignal(p)>0;if(powered!=s.powered){if(powered)playNote(p,id);setBlock(p,registry.withBool(id,"powered",powered));}break;}
    case Device::piston: checkPiston(p); break;
    // 原版 PistonHeadBlock.neighborChanged 把**收到的来源方块**原样转发给活塞本体，
    // 不是用空气或活塞头自己。
    case Device::pistonHead: if (survives(p, id)) neighborChanged(p.relative(opposite(s.facing)), source); break;
    case Device::rail: case Device::poweredRail: case Device::activatorRail: case Device::detectorRail: updateRail(p, source); break;
    case Device::hopper: {
        bool enabled = bestSignal(p) == 0;
        if ((registry.property(id, "enabled") == "true") != enabled) setBlock(p, registry.withBool(id, "enabled", enabled), 2);
        break;
    }
    default: break;
    }
}
void Simulator::schedule(BlockPos p, Tick delay, int priority, std::uint16_t type) {
    auto blockType = type == 0xFFFFu ? at(p).type : type;
    if (delay > std::numeric_limits<Tick>::max() - currentTick) throw std::invalid_argument("计划时间超出范围");
    // Vanilla allocates subTickOrder even when the chunk rejects a duplicate.
    blockTicks.schedule({currentTick + delay, priority, nextOrder++, p, blockType});
}
bool Simulator::hasScheduled(BlockPos p) const { return blockTicks.hasScheduled(p, at(p).type); }
void Simulator::executeTick(const ScheduledEvent& event) {
    auto p = event.pos; auto id = world.get(p); const auto& s = registry[id];
    switch (s.device) {
    case Device::sculkSensor: case Device::calibratedSensor: tickSensor(p);break;
    case Device::repeater:
        if (diodeSideInput(p) == 0) {
            bool input = diodeInput(p) > 0;
            if (s.powered && !input) setBlock(p, registry.withBool(id, "powered", false), 2);
            else if (!s.powered) { setBlock(p, registry.withBool(id, "powered", true), 2); if (!input) schedule(p, static_cast<Tick>(s.delay) * 2, -2); }
        }
        break;
    case Device::comparator: refreshComparator(p); break;
    case Device::composter: if(s.staticAnalog==7)setBlock(p,registry.with(id,"level",8));break;
    case Device::observer: setBlock(p, registry.withBool(id, "powered", !s.powered), 2); if (!s.powered) schedule(p, 2); notifyFront(p, s.facing); break;
    case Device::lamp: if (s.lit && bestSignal(p) == 0) setBlock(p, registry.withBool(id, "lit", false), 2); break;
    case Device::torch: case Device::wallTorch: {
        while (!recentTorchToggles.empty() && currentTick - recentTorchToggles.front().tick > 60) {
            auto found = torchToggleCounts.find(recentTorchToggles.front().pos);
            if (--found->second == 0) torchToggleCounts.erase(found);
            recentTorchToggles.pop_front();
        }
        bool input = torchInput(p);
        if (s.lit && input) {
            setBlock(p, registry.withBool(id, "lit", false));
            recentTorchToggles.push_back({p, currentTick});
            if (++torchToggleCounts[p] >= 8) schedule(p, 160);
        } else if (!s.lit && !input) {
            auto found = torchToggleCounts.find(p);
            if (found == torchToggleCounts.end() || found->second < 8) setBlock(p, registry.withBool(id, "lit", true));
        }
        break;
    }
    case Device::button: if (s.powered) updateButton(p); break;
    case Device::dropper: dispenseDropper(p); break;
    case Device::target: setBlock(p, registry.with(id, "power", 0)); break;
    case Device::pressurePlate: case Device::weightedPlate: if (s.powered || s.power > 0) updatePressurePlate(p); break;
    case Device::lightningRod: setBlock(p, registry.withBool(id, "powered", false)); updateNeighbors(p.relative(opposite(s.facing)), -1, id); break;
    case Device::lectern: setBlock(p, registry.withBool(id, "powered", false)); updateNeighbors(p.relative(Direction::down), -1, id); break;
    case Device::detectorRail: if (s.powered) updateDetectorRail(p); break;
    case Device::tripwire: if (s.powered) updateTripwire(p); break;
    case Device::tripwireHook: calculateTripwire(p, id, false, true); break;
    default: break;
    }
}
bool Simulator::stepEvent() {
    if (faulted) throw std::runtime_error("当前执行已中止，请从有效快照恢复");
    if (hasPendingActions()) { breakRequested = true; pauseReason = "存在待处理的外部动作，请检查输出并提供环境反馈，或确认本次不反馈"; return false; }
    if (traceBlocked()) { breakRequested = true; pauseReason = "探针缓冲等待浏览器确认，仿真已暂停以保留全部边沿"; }
    pruneEvents();
    if (pendingEvents() == 0 || breakRequested) return false;
    ScheduledEvent event;
    auto nextBlockTick = blockTicks.nextTick();
    if (nextBlockTick && (scheduled.empty() || *nextBlockTick <= scheduled.top().tick)) {
        if (!blockTicks.hasBatch()) blockTicks.collect(std::max(currentTick, *nextBlockTick));
        currentTick = blockTicks.batchTick();
        event = blockTicks.pop();
    } else {
        event = scheduled.top(); scheduled.pop(); scheduledKeys.erase({event.pos, event.type, event.phase, event.data});
        currentTick = std::max(currentTick, event.tick);
        blockTicks.finishThrough(currentTick);
    }
    ++sequence;
    currentPhase = event.phase;
    currentEntityOrder = event.entityOrder;
    try {
        if (at(event.pos).type == event.type) {
            if (event.phase == 1) {if(at(event.pos).device==Device::noteBlock)noteEvent(event.pos);else if(at(event.pos).device==Device::bell)bellEvent(event);else pistonEvent(event);}
            else if (event.phase == 3) {
                if (at(event.pos).device == Device::button) buttonContact(event.pos);
                else if (at(event.pos).device == Device::hopper) hopperEntityContact(event.pos);
                else tripwireContact(event.pos);
            }
            else if (event.phase == 2) {
                if (at(event.pos).device == Device::daylight) updateDaylight(event.pos);
                else if (at(event.pos).device == Device::hopper) tickHopper(event);
                else if (isSensor(at(event.pos).device)) tickVibration(event);
                else if(at(event.pos).device==Device::bell)finishBell(event);
                else if(at(event.pos).device==Device::jukebox)tickJukebox(event);
                else tickMotion(event);
            } else executeTick(event);
            ++statistics.scheduledEvents;
        }
    } catch (...) { currentPhase = 4; throw; }
    currentPhase = 4;
    return true;
}
Tick Simulator::nextTick() {
    pruneEvents();
    auto blockTick = blockTicks.nextTick();
    if (scheduled.empty()) return blockTick.value_or(currentTick);
    return std::max(currentTick, blockTick ? std::min(*blockTick, scheduled.top().tick) : scheduled.top().tick);
}
void Simulator::pruneEvents() {
    while (!scheduled.empty()) {
        const auto& e = scheduled.top();
        if (scheduledKeys.contains({e.pos, e.type, e.phase, e.data})) break;
        scheduled.pop();
    }
}
std::size_t Simulator::advanceTo(Tick target, std::size_t eventBudget, std::chrono::microseconds wallBudget) {
    return advance(target, eventBudget, wallBudget, true);
}
std::size_t Simulator::advanceActive(std::size_t eventBudget, std::chrono::microseconds wallBudget) {
    return advance(std::numeric_limits<Tick>::max() - 1, eventBudget, wallBudget, false);
}
std::size_t Simulator::advance(Tick target, std::size_t eventBudget, std::chrono::microseconds wallBudget, bool fillIdle) {
    if (faulted) throw std::runtime_error("当前执行已中止，请从有效快照恢复");
    if (hasPendingActions()) { breakRequested = true; pauseReason = "存在待处理的外部动作，请检查输出并提供环境反馈，或确认本次不反馈"; return 0; }
    if (target < currentTick) throw std::invalid_argument("不能倒退时间，请加载运行快照");
    if (traceBlocked()) { breakRequested = true; pauseReason = "探针缓冲等待浏览器确认，仿真已暂停以保留全部边沿"; return 0; }
    auto start = std::chrono::steady_clock::now(); std::size_t count = 0;
    pruneEvents();
    while (pendingEvents() != 0 && nextTick() <= target && !breakRequested && count < eventBudget) {
        if ((count & 63u) == 0 && std::chrono::steady_clock::now() - start >= wallBudget) break;
        stepEvent(); ++count; pruneEvents();
    }
    if (fillIdle && !breakRequested && (pendingEvents() == 0 || nextTick() > target)) { currentTick = target; blockTicks.finishThrough(target); }
    statistics.simulationMicros += static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
    return count;
}
void Simulator::interact(BlockPos p, std::optional<Direction> playerFacing) {
    if (faulted) throw std::runtime_error("当前执行已中止，请从有效快照恢复");
    auto id = world.get(p); const auto& s = registry[id];
    switch (s.device) {
    case Device::bell: {auto direction=s.facing;const auto attachment=registry.property(id,"attachment");if(attachment=="single_wall" || attachment=="double_wall")direction=axis(direction)==1?Direction::east:Direction::north;ringBell(p,direction);break;}
    case Device::noteBlock: {auto next=registry.with(id,"note",(std::stoi(registry.property(id,"note"))+1)%25);setBlock(p,next);playNote(p,next);break;}
    case Device::lever: setBlock(p, registry.withBool(id, "powered", !s.powered)); notifyAttached(p, s.connectedDirection); emitGameEvent(s.powered?"block_deactivate":"block_activate",p);break;
    case Device::button: if (!s.powered) { setBlock(p, registry.withBool(id, "powered", true)); notifyAttached(p, s.connectedDirection); const auto& name = registry.type(id).name; schedule(p, name == "minecraft:stone_button" || name == "minecraft:polished_blackstone_button" ? 20 : 30); emitGameEvent("block_activate",p); } break;
    case Device::repeater: setBlock(p, registry.with(id, "delay", s.delay % 4 + 1)); break;
    case Device::comparator: setBlock(p, registry.with(id, "mode", std::string(s.subtract ? "compare" : "subtract")), 2); refreshComparator(p); break;
    case Device::wire: {
        bool dot = std::all_of(s.wireSides.begin(), s.wireSides.end(), [](auto x) { return x == 0; });
        bool cross = std::all_of(s.wireSides.begin(), s.wireSides.end(), [](auto x) { return x != 0; });
        if (dot || cross) {
            auto base = id;
            for (auto d : horizontal) base = registry.with(base, directionNames[static_cast<unsigned>(d)], std::string(dot ? "side" : "none"));
            const auto next = wireConnections(p, base);
            // 原版在新旧状态相同时直接 PASS，且只通知“连接性确实变化”且邻居是导体的方向。
            if (next != id) {
                setBlock(p, next);
                for (auto d : horizontal) {
                    const auto index = sideIndex(d);
                    if ((registry[id].wireSides[index] != 0) != (registry[next].wireSides[index] != 0) && at(p.relative(d)).conductor)
                        updateNeighbors(p.relative(d), static_cast<int>(opposite(d)), next);
                }
            }
        }
        break;
    }
    default: if (!interactDevice(p, playerFacing)) throw std::invalid_argument("该器件没有直接点击操作；请编辑属性或使用环境刺激");
    }
}
void Simulator::stimulate(BlockPos p, const Json& input) {
    if (faulted) throw std::runtime_error("当前执行已中止，请从有效快照恢复");
    if(input.contains("gameEvent")) {stimulateVibration(p,input);return;}
    if (!input.is_object())throw std::invalid_argument("环境输入必须是对象");
    if (input.contains("itemFrames")) { stimulateItemFrames(p, input); return; }
    if(stimulateNote(p,input) || stimulateBell(p,input))return;
    if (stimulateDevice(p, input)) return;
    throw std::invalid_argument("该器件尚不支持这类环境刺激");
}
void Simulator::clear() {
    setRandomSeed(0);
    environmentActions.clear(); pendingActionIds.clear(); nextActionId = 1; actionsDropped = 0;
    recentTorchToggles.clear(); torchToggleCounts.clear();
    sensors.clear();sensorSections.clear();jukeboxes.clear();
    world.clear(); runtime.clear(); motions.clear(); hoppers.clear(); entityOrders.clear(); nextEntityOrder = 0; scheduled = {}; scheduledKeys.clear(); blockTicks = {}; changes.clear(); currentTick = 0; nextOrder = 0; sequence = 0; currentPhase = 4;
    probes.clear(); probeDependencies.clear(); nextProbeId = 1; trace.clear(); traceDropped = 0; statistics = {}; breakRequested = false; faulted = false; pauseReason.clear(); ++revision;
    updateTrace = Json::array(); updateTraceTruncated = false;
    if (retainedTrace) retainedTrace = 0;
}
std::vector<Cell> Simulator::takeChanges() { std::vector<Cell> result; result.reserve(changes.size()); for (const auto& [p, id] : changes) result.push_back({p, id}); changes.clear(); return result; }
void Simulator::rebuildProbeDependencies() {
    probeDependencies.clear();
    for (std::size_t index = 0; index < probes.size(); ++index) {
        const auto& probe = probes[index];
        std::unordered_set<BlockPos, PosHash> affected{probe.pos};
        for (auto a : directions) { auto q = probe.pos.relative(a); affected.insert(q); for (auto b : directions) affected.insert(q.relative(b)); }
        for (auto p : affected) probeDependencies[p].push_back(static_cast<std::uint32_t>(index));
    }
}
void Simulator::trimTrace() {
    while (trace.size() > traceCapacity && (!retainedTrace || traceDropped < *retainedTrace)) { trace.pop_front(); ++traceDropped; }
}
void Simulator::retainTraceFrom(std::optional<std::uint64_t> firstUnacknowledged) {
    if (firstUnacknowledged && (*firstUnacknowledged < traceDropped || *firstUnacknowledged > traceDropped + trace.size()))
        throw std::invalid_argument("探针确认游标超出保留历史范围");
    retainedTrace = firstUnacknowledged;
    trimTrace();
}
bool Simulator::traceBlocked() const {
    return retainedTrace && traceDropped + trace.size() - *retainedTrace >= traceCapacity;
}
void Simulator::sampleProbe(Probe& probe) {
    int value = probe.mode == "input" ? bestSignal(probe.pos) : probe.mode == "direction" ? signal(probe.pos, probe.direction) : probe.mode=="analog"?analogOutput(probe.pos):displayValue(probe.pos);
    if (value == probe.lastValue) return;
    bool trigger = (probe.trigger == "rising" && probe.lastValue == 0 && value > 0) || (probe.trigger == "falling" && probe.lastValue > 0 && value == 0) || (probe.trigger == "value" && value == probe.triggerValue && probe.lastValue >= 0);
    trace.push_back({probe.id, currentTick, sequence, static_cast<std::uint8_t>(value)}); probe.lastValue = value;
    trimTrace();
    if (traceBlocked()) {
        breakRequested = true;
        pauseReason = "探针缓冲等待浏览器确认，仿真已暂停以保留全部边沿";
        if (trace.size() - traceCapacity > traceAtomicReserve) {
            faulted = true;
            pauseReason = "单次器件更新的探针边沿超过安全余量，本次执行中止；请减少探针或恢复快照";
            throw std::runtime_error(pauseReason);
        }
    }
    if (trigger) { breakRequested = true; pauseReason = "探针「" + probe.name + "」触发断点"; }
}
void Simulator::sampleAffected(BlockPos p) {
    auto found = probeDependencies.find(p); if (found == probeDependencies.end()) return;
    for (auto index : found->second) sampleProbe(probes[index]);
}
std::uint32_t Simulator::addProbe(BlockPos p, const std::string& name, const std::string& mode, Direction direction) {
    if (probes.size() >= 256) throw std::invalid_argument("最多支持 256 个探针");
    if (mode != "output" && mode != "input" && mode != "direction" && mode!="analog") throw std::invalid_argument("未知探针模式");
    auto id = nextProbeId++; probes.push_back({id, p, name.empty() ? "P" + std::to_string(id) : name, mode, direction}); rebuildProbeDependencies(); sampleProbe(probes.back()); return id;
}
void Simulator::removeProbe(std::uint32_t id) { std::erase_if(probes, [id](const Probe& p) { return p.id == id; }); rebuildProbeDependencies(); }
void Simulator::configureProbe(std::uint32_t id, const Json& config) {
    for (auto& p : probes) if (p.id == id) { p.name = config.value("name", p.name); p.trigger = config.value("trigger", p.trigger); p.triggerValue = config.value("triggerValue", p.triggerValue); return; }
    throw std::invalid_argument("探针不存在");
}
void Simulator::clearTrace() { trace.clear(); traceDropped = 0; if (retainedTrace) retainedTrace = 0; for (auto& probe : probes) { probe.lastValue = -1; sampleProbe(probe); } }
Json Simulator::inspect(BlockPos p) const {
    auto id = world.get(p); auto result = registry.describe(id);
    result["pos"] = p; result["value"] = displayValue(p); result["input"] = bestSignal(p); result["analog"] = analogOutput(p);
    result["supportLevel"] = registry.type(id).supportLevel;
    if (inventorySize(id)) { result["inventory"] = inventoryJson(p); result["inventorySize"] = containerSlots(p).size(); }
    if (at(p).device == Device::detectorRail) {
        auto cart = firstContainerCart(p);
        result["inventory"] = cart ? cart->at("inventory") : Json::array();
        result["inventorySize"] = cart ? (cart->at("type") == "chest_minecart" ? 27 : 5) : 0;
    }
    auto it = runtime.find(p); result["runtime"] = it == runtime.end() ? Json::object() : it->second.values;
    if(auto player=jukeboxes.find(p);player!=jukeboxes.end()) {
        result["jukebox"]={{"playing",player->second.song>=0},{"elapsed",player->second.elapsed}};
        if(player->second.song>=0) {const auto& song=registry.song(player->second.song);result["jukebox"]["song"]=song.name;result["jukebox"]["lengthTicks"]=song.lengthTicks;result["jukebox"]["durationTicks"]=song.lengthTicks+20;}
    }
    if(auto sensor=sensors.find(p);sensor!=sensors.end()) {
        const auto& data=sensor->second;
        result["vibration"]={{"state",data.current?"travelling":data.candidate?"selecting":"idle"},{"remaining",data.remaining}};
        const auto& pending=data.current?data.current:data.candidate;
        if(pending) {result["vibration"]["event"]=registry.gameEvent(pending->event).name;result["vibration"]["origin"]=pending->origin;result["vibration"]["distance"]=pending->distance;}
    }
    if (auto motion = motionAt(p)) result["motion"] = {{"movedBlock", registry.describe(motion->movedState)}, {"progress", motion->progress * 0.5}, {"extending", motion->extending}, {"source", motion->source}};
    return result;
}
}
