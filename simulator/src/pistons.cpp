#include "simulator/simulator.hpp"
#include <algorithm>
#include <functional>

namespace simulator {
bool Simulator::pistonPowered(BlockPos pos) const {
    auto facing = at(pos).facing;
    for (auto d : directions) if (d != facing && signal(pos.relative(d), d) > 0) return true;
    if (signal(pos, Direction::down) > 0) return true;
    auto above = pos.relative(Direction::up);
    for (auto d : directions) if (d != Direction::down && signal(above.relative(d), d) > 0) return true;
    return false;
}
bool Simulator::pushable(BlockPos pos, Direction movement, bool allowDestroy, Direction connection) const {
    if (pos.y < -64 || pos.y > 319 || pos.x <= -29999984 || pos.x >= 29999984 || pos.z <= -29999984 || pos.z >= 29999984) return false;
    auto id = world.get(pos); const auto& state = registry[id];
    if (id == 0) return true;
    const auto& name = registry.type(id).name;
    if (name == "minecraft:bedrock" || name == "minecraft:obsidian" || name == "minecraft:crying_obsidian" || name == "minecraft:respawn_anchor" || name == "minecraft:reinforced_deepslate") return false;
    if ((movement == Direction::down && pos.y == -64) || (movement == Direction::up && pos.y == 319)) return false;
    if (state.device == Device::piston) { if (state.extended) return false; }
    else {
        if (state.pushReaction == 2) return false;
        if (state.pushReaction == 1) return allowDestroy;
        if (state.pushReaction == 3 && movement != connection) return false;
    }
    return !state.blockEntity;
}
Simulator::PistonPlan Simulator::resolvePiston(BlockPos pos, Direction facing, bool extending) const {
    PistonPlan plan; auto movement = extending ? facing : opposite(facing); auto start = pos.relative(facing, extending ? 1 : 2);
    auto adhesion = [&](BlockPos p) { const auto& name = registry.type(world.get(p)).name; return name == "minecraft:slime_block" ? 1 : name == "minecraft:honey_block" ? 2 : 0; };
    auto canStick = [&](BlockPos a, BlockPos b) { int first = adhesion(a), second = adhesion(b); return (first || second) && !(first && second && first != second); };
    if (!pushable(start, movement, false, facing)) { if (extending && at(start).pushReaction == 1) { plan.destroy.push_back(start); plan.valid = true; } return plan; }
    std::function<bool(BlockPos, Direction)> addLine;
    std::function<bool(BlockPos)> branch = [&](BlockPos p) {
        for (auto d : directions) if (axis(d) != axis(movement)) { auto neighbor = p.relative(d); if (canStick(neighbor, p) && !addLine(neighbor, d)) return false; }
        return true;
    };
    addLine = [&](BlockPos first, Direction connection) {
        if (world.get(first) == 0 || !pushable(first, movement, false, connection) || first == pos || std::find(plan.push.begin(), plan.push.end(), first) != plan.push.end()) return true;
        int behindCount = 1;
        if (plan.push.size() + 1 > 12) return false;
        auto previous = first;
        while (adhesion(previous) != 0) {
            auto behind = first.relative(opposite(movement), behindCount);
            if (world.get(behind) == 0 || !canStick(previous, behind) || !pushable(behind, movement, false, opposite(movement)) || behind == pos) break;
            ++behindCount; if (plan.push.size() + static_cast<std::size_t>(behindCount) > 12) return false; previous = behind;
        }
        int added = 0;
        for (int offset = behindCount - 1; offset >= 0; --offset) { plan.push.push_back(first.relative(opposite(movement), offset)); ++added; }
        for (int distance = 1;; ++distance) {
            auto next = first.relative(movement, distance);
            auto collision = std::find(plan.push.begin(), plan.push.end(), next);
            if (collision != plan.push.end()) {
                auto index = static_cast<std::size_t>(std::distance(plan.push.begin(), collision));
                auto lineStart = plan.push.end() - added;
                std::rotate(plan.push.begin() + static_cast<std::ptrdiff_t>(index), lineStart, plan.push.end());
                for (std::size_t i = 0; i <= index + static_cast<std::size_t>(added); ++i) if (adhesion(plan.push[i]) && !branch(plan.push[i])) return false;
                return true;
            }
            if (world.get(next) == 0) return true;
            if (!pushable(next, movement, true, movement) || next == pos) return false;
            if (at(next).pushReaction == 1) { plan.destroy.push_back(next); return true; }
            if (plan.push.size() >= 12) return false;
            plan.push.push_back(next); ++added;
        }
    };
    if (!addLine(start, movement)) return plan;
    for (std::size_t i = 0; i < plan.push.size(); ++i) if (adhesion(plan.push[i]) && !branch(plan.push[i])) return plan;
    plan.valid = true; return plan;
}
void Simulator::schedulePhase(BlockPos pos, Tick when, std::uint8_t phase, std::uint64_t data) {
    auto type = at(pos).type;
    if (scheduledKeys.insert({pos, type, phase, data}).second) scheduled.push({when, 0, nextOrder++, pos, type, phase, data, phase == 2 ? registerEntity(pos) : 0});
}
std::uint64_t Simulator::registerEntity(BlockPos pos) {
    auto [found, added] = entityOrders.try_emplace(pos, nextEntityOrder);
    if (added) ++nextEntityOrder;
    return found->second;
}
void Simulator::checkPiston(BlockPos pos) {
    const auto& state = at(pos); if (state.device != Device::piston) return;
    bool powered = pistonPowered(pos);
    Tick when = currentTick + (currentPhase > 1 ? 1 : 0);
    if (powered && !state.extended) {
        if (resolvePiston(pos, state.facing, true).valid) schedulePhase(pos, when, 1, static_cast<unsigned>(state.facing) << 2);
    } else if (!powered && state.extended) {
        std::uint64_t code = 1; auto motion = motionAt(pos.relative(state.facing, 2));
        if (motion && motion->facing == state.facing && motion->extending && (motion->previousProgress < 1 || motion->lastTicked == currentTick || currentPhase == 0)) code = 2;
        schedulePhase(pos, when, 1, code | (static_cast<std::uint64_t>(state.facing) << 2));
    }
}
void Simulator::addMotion(BlockPos pos, StateId state, Direction facing, bool extending, bool source) {
    if (auto old = motions.find(pos); old != motions.end()) scheduledKeys.erase({pos, at(pos).type, 2, old->second.generation});
    PistonMotion motion{state, facing, extending, source, 0, 0, 0, nextOrder++};
    motions[pos] = motion;
    schedulePhase(pos, currentTick + (beforeBlockEntities() ? 0 : 1), 2, motion.generation);
    changes[pos] = world.get(pos);
}
bool Simulator::movePistonBlocks(BlockPos pos, Direction facing, bool extending) {
    const auto arm = pos.relative(facing);
    if (!extending && at(arm).device == Device::pistonHead) setBlock(arm, 0, 276);
    auto plan = resolvePiston(pos, facing, extending); if (!plan.valid) return false;
    const bool sticky = registry.type(world.get(pos)).name == "minecraft:sticky_piston" || (motionAt(pos) && registry[motionAt(pos)->movedState].sticky);
    const auto moving = registry.state("moving_piston", {{"facing", directionNames[static_cast<unsigned>(facing)]}});
    auto movement = extending ? facing : opposite(facing);
    std::vector<Cell> originals, removed;
    for (auto p : plan.push) originals.push_back({p, world.get(p)});
    for (auto it = plan.destroy.rbegin(); it != plan.destroy.rend(); ++it) { const auto old=world.get(*it);removed.push_back({*it,old}); setBlock(*it, 0, 18);emitGameEvent("block_destroy",*it,{false,false,false,old}); }
    std::vector<Cell> toClear = originals;
    for (auto it = originals.rbegin(); it != originals.rend(); ++it) {
        auto destination = it->pos.relative(movement);
        std::erase_if(toClear, [&](const Cell& cell) { return cell.pos == destination; });
        setBlock(destination, moving, 324); addMotion(destination, it->state, facing, extending, false);
    }
    if (extending) {
        auto head = registry.state("piston_head", {{"facing", directionNames[static_cast<unsigned>(facing)]}, {"type", sticky ? "sticky" : "normal"}});
        auto movingHead = registry.with(moving, "type", std::string(sticky ? "sticky" : "normal"));
        std::erase_if(toClear, [&](const Cell& cell) { return cell.pos == arm; });
        setBlock(arm, movingHead, 324); addMotion(arm, head, facing, true, true);
    }
    auto bucket = [](BlockPos p) { auto hash = (static_cast<std::uint32_t>(p.y) + static_cast<std::uint32_t>(p.z) * 31u) * 31u + static_cast<std::uint32_t>(p.x); return (hash ^ (hash >> 16)) & 15u; };
    std::stable_sort(toClear.begin(), toClear.end(), [&](const Cell& a, const Cell& b) { return bucket(a.pos) < bucket(b.pos); });
    for (const auto& cell : toClear) setBlock(cell.pos, 0, 82);
    for (const auto& cell : toClear) {
        indirectShapes(cell.pos, cell.state, 2, 512);
        for (auto d : shapeOrder) enqueue({UpdateKind::shape, cell.pos.relative(d), opposite(d), 0});
    }
    for (const auto& cell : removed) { onRemove(cell.pos, cell.state); indirectShapes(cell.pos, cell.state, 2, 512); updateNeighbors(cell.pos); }
    for (auto it = originals.rbegin(); it != originals.rend(); ++it) updateNeighbors(it->pos);
    if (extending) updateNeighbors(arm);
    return true;
}
void Simulator::pistonEvent(const ScheduledEvent& event) {
    auto pos = event.pos; auto id = world.get(pos); const auto& state = registry[id]; if (state.device != Device::piston) return;
    auto facing = state.facing; const bool sticky = state.sticky; auto extended = registry.withBool(id, "extended", true);
    const auto code = event.data & 3u;
    bool powered = pistonPowered(pos);
    if (powered && code != 0) { setBlock(pos, extended, 2); return; }
    if (!powered && code == 0) return;
    if (code == 0) {
        if (movePistonBlocks(pos, facing, true)) {setBlock(pos, extended, 67);(void)worldRandom.nextFloat();emitGameEvent("block_activate",pos,{false,false,false,extended});}
        return;
    }
    const auto arm = pos.relative(facing); finishMotion(arm, true);
    auto moving = registry.state("moving_piston", {{"facing", directionNames[static_cast<unsigned>(facing)]}, {"type", sticky ? "sticky" : "normal"}});
    auto retracted = registry.withBool(id, "extended", false);
    retracted = registry.with(retracted, "facing", std::string(directionNames[static_cast<unsigned>((event.data >> 2) & 7u)]));
    setBlock(pos, moving, 276); addMotion(pos, retracted, facing, false, true);
    updateNeighbors(pos); for (auto d : shapeOrder) enqueue({UpdateKind::shape, pos.relative(d), opposite(d), moving});
    if (!sticky) setBlock(arm, 0);
    else {
        auto twoAhead = pos.relative(facing, 2); auto motion = motionAt(twoAhead);
        if (motion && motion->facing == facing && motion->extending) finishMotion(twoAhead, true);
        else {
            const auto& next = at(twoAhead);
            if (code == 1 && world.get(twoAhead) != 0 && pushable(twoAhead, opposite(facing), false, facing) && (next.pushReaction == 0 || next.device == Device::piston)) movePistonBlocks(pos, facing, false);
            else setBlock(arm, 0);
        }
    }
    (void)worldRandom.nextFloat();emitGameEvent("block_deactivate",pos,{false,false,false,moving});
}
void Simulator::finishMotion(BlockPos pos, bool force) {
    auto found = motions.find(pos); if (found == motions.end()) return;
    const auto motion = found->second;
    if (force && motion.previousProgress >= 2) return;
    scheduledKeys.erase({pos, at(pos).type, 2, motion.generation});
    motions.erase(pos);
    if (at(pos).device != Device::movingPiston) return;
    auto next = force && motion.source ? 0 : motion.movedState;
    if (next && !survives(pos, next)) next = 0;
    if (registry[next].device == Device::wire) next = wireConnections(pos, next);
    if (next && registry.has(next, "waterlogged")) next = registry.withBool(next, "waterlogged", false);
    if (next == 0 && !force) { setBlock(pos, motion.movedState, 340); setBlock(pos, 0); }
    else { setBlock(pos, next, force ? 3 : 67); neighborChanged(pos); }
}
void Simulator::tickMotion(const ScheduledEvent& event) {
    auto found = motions.find(event.pos); if (found == motions.end() || found->second.generation != event.data) return;
    auto& motion = found->second; motion.lastTicked = currentTick; motion.previousProgress = motion.progress;
    if (motion.previousProgress >= 2) { finishMotion(event.pos, false); return; }
    ++motion.progress; changes[event.pos] = world.get(event.pos); ++revision;
    schedulePhase(event.pos, currentTick + 1, 2, motion.generation);
}
}
