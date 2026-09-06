#include "simulator/simulator.hpp"
#include <algorithm>
#include <set>

namespace simulator {

void Simulator::startHopper(BlockPos pos) {
    registerEntity(pos);
    auto& hopper = hoppers[pos];
    hopper.firstTick = currentTick + (beforeBlockEntities() ? 0 : 1);
    wakeHopper(pos);
}

void Simulator::wakeHopper(BlockPos pos) {
    auto found = hoppers.find(pos);
    if (found == hoppers.end()) return;
    auto& hopper = found->second;
    auto rank = entityOrders.at(pos);
    // The current tick will schedule its own continuation after push and pull.
    if (currentPhase == 2 && rank == currentEntityOrder) return;
    bool canRunThisTick = beforeBlockEntities() || (currentPhase == 2 && rank > currentEntityOrder);
    auto when = std::max({hopper.readyAt, hopper.firstTick, currentTick + (canRunThisTick ? 0 : 1)});
    if (hopper.wakeAt <= when) return;
    if (hopper.wakeAt != UINT64_MAX) scheduledKeys.erase({pos, at(pos).type, 2, hopper.generation});
    hopper.wakeAt = when;
    hopper.generation = nextOrder++;
    schedulePhase(pos, when, 2, hopper.generation);
}

void Simulator::wakeHoppers(BlockPos changed) {
    wakeHopper(changed);
    wakeHopper(changed.relative(Direction::down));
    for (auto direction : directions) {
        auto pos = changed.relative(direction);
        if (at(pos).device == Device::hopper && at(pos).facing == opposite(direction)) wakeHopper(pos);
    }
}

bool Simulator::inventoryEmpty(BlockPos pos) const {
    for (const auto& slot : containerSlots(pos)) if (stackAt(slot).count) return false;
    return true;
}

bool Simulator::inventoryFull(BlockPos pos) const {
    for (const auto& slot : containerSlots(pos)) {
        auto stack = stackAt(slot);
        if (!stack.count || stack.count < registry.item(stack.item).maxStack) return false;
    }
    return true;
}

void Simulator::writeStack(const InventorySlot& slot, ItemStack stack, bool notify) {
    const auto id=world.get(slot.pos);
    const bool bookshelf=isBookshelf(id), occupied=bookshelf && stackAt(slot).count>0;
    auto& inventory = runtime[slot.pos].inventory;
    inventory.resize(inventorySize(id));
    inventory[slot.index] = stack.count ? stack : ItemStack{};
    if(bookshelf) {
        if(occupied || stack.count) updateBookshelfSlot(slot);
        return;
    }
    // Base containers call setChanged from setItem/removeItem. Hopper overrides
    // deliberately omit it; the transfer code notifies at its original points.
    if (notify && registry[id].device != Device::hopper) runtimeChanged(slot.pos);
}

void Simulator::containerChanged(BlockPos pos) {
    std::set<BlockPos> visited;
    for (const auto& slot : containerSlots(pos))
        if (visited.insert(slot.pos).second) runtimeChanged(slot.pos);
}

bool Simulator::transferItem(BlockPos from, BlockPos to, bool pulling) {
    auto sourceSlots = containerSlots(from), targetSlots = containerSlots(to);
    if (sourceSlots.empty() || targetSlots.empty() || (!pulling && inventoryFull(to))) return false;
    for (const auto& source : sourceSlots) {
        auto original = stackAt(source);
        if (!original.count) continue;
        if(pulling && !canExtractStack(source,to)) continue;
        auto remaining = original;
        --remaining.count;
        writeStack(source, remaining);
        bool targetWasEmpty = inventoryEmpty(to);
        for (const auto& target : targetSlots) {
            if(!canInsertStack(target,original)) continue;
            auto stack = stackAt(target);
            if (stack.count && (stack.item != original.item || stack.count >= registry.item(stack.item).maxStack)) continue;
            writeStack(target, {original.item, static_cast<std::uint16_t>(stack.count + 1)}, stack.count == 0);
            if (targetWasEmpty && at(to).device == Device::hopper) {
                // All transfers here run during the block entity phase. An
                // empty recipient becomes eligible seven ticks later whether
                // it already ticked (cooldown 7) or ticks later today (8 -> 7).
                auto& hopper = hoppers.at(to);
                hopper.readyAt = currentTick + (currentPhase == 2 ? 7 : 8);
            }
            containerChanged(to);
            if (pulling) containerChanged(from);
            return true;
        }
        // Failed extraction restores the original stack, including vanilla's
        // container notification on both removal and restoration.
        writeStack(source, original, original.count == 1);
    }
    return false;
}

void Simulator::tickHopper(const ScheduledEvent& event) {
    auto found = hoppers.find(event.pos);
    if (found == hoppers.end() || found->second.generation != event.data) return;
    auto& hopper = found->second;
    hopper.wakeAt = UINT64_MAX;
    if (hopper.readyAt > currentTick) {
        hopper.wakeAt = hopper.readyAt;
        schedulePhase(event.pos, hopper.readyAt, 2, hopper.generation);
        return;
    }
    if (registry.property(world.get(event.pos), "enabled") != "true") return;
    bool moved = false;
    if (!inventoryEmpty(event.pos)) moved = transferItem(event.pos, event.pos.relative(at(event.pos).facing));
    bool retryExtraction = false;
    if (!inventoryFull(event.pos)) {
        auto source = event.pos.relative(Direction::up);
        bool pulled = transferItem(source, event.pos, true);
        // Failed extraction from ordinary containers can still issue comparator
        // updates on remove/restore. Preserve those observable repeated calls.
        retryExtraction = !pulled && at(source).device != Device::hopper && !inventoryEmpty(source);
        if(retryExtraction && isBookshelf(world.get(source))) {
            retryExtraction=false;
            for(const auto& slot:containerSlots(source)) if(stackAt(slot).count && canExtractStack(slot,event.pos)) {retryExtraction=true;break;}
        }
        moved = pulled || moved;
    }
    if (moved) {
        hopper.readyAt = currentTick + 8;
        containerChanged(event.pos);
        hopper.wakeAt = hopper.readyAt;
        schedulePhase(event.pos, hopper.readyAt, 2, hopper.generation);
    } else if (retryExtraction) {
        hopper.wakeAt = currentTick + 1;
        schedulePhase(event.pos, hopper.wakeAt, 2, hopper.generation);
    }
    // A failed transfer has no future work until inventory, topology or power
    // changes. Those mutations wake only adjacent dependent hoppers.
}
}
