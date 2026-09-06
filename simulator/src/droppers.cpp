#include "simulator/simulator.hpp"

namespace simulator {
void Simulator::dispenseDropper(BlockPos pos) {
    const auto slots = containerSlots(pos);
    int chosen = -1, odds = 1;
    for (std::size_t i = 0; i < slots.size(); ++i) {
        if (stackAt(slots[i]).count && worldRandom.nextInt(odds++) == 0) chosen = static_cast<int>(i);
    }
    if (chosen < 0) return;
    const auto source = slots[static_cast<std::size_t>(chosen)];
    auto stack = stackAt(source);
    const auto targetPos = pos.relative(at(pos).facing);
    const auto targets = containerSlots(targetPos);
    if (targets.empty()) {
        const auto direction = at(pos).facing;
        const auto unit = BlockPos{}.relative(direction);
        const double speed = worldRandom.nextDouble() * .1 + .2, spread = .0172275 * 6;
        const double velocityX = worldRandom.triangle(unit.x * speed, spread);
        const double velocityY = worldRandom.triangle(.2, spread);
        const double velocityZ = worldRandom.triangle(unit.z * speed, spread);
        recordAction({{"kind", "itemEjected"}, {"source", pos}, {"item", registry.item(stack.item).name}, {"count", 1},
            {"position", Json::array({pos.x + .5 + unit.x * .7, pos.y + .5 + unit.y * .7 - (axis(direction) == 0 ? .125 : .15625), pos.z + .5 + unit.z * .7})},
            {"velocity", Json::array({velocityX, velocityY, velocityZ})}});
        --stack.count; writeStack(source, stack); return;
    }
    const bool wasEmpty = inventoryEmpty(targetPos);
    for (const auto& target : targets) {
        if(!canInsertStack(target,stack)) continue;
        auto existing = stackAt(target);
        if (existing.count && (existing.item != stack.item || existing.count >= registry.item(existing.item).maxStack)) continue;
        writeStack(target, {stack.item, static_cast<std::uint16_t>(existing.count + 1)}, existing.count == 0);
        if (wasEmpty && at(targetPos).device == Device::hopper) {
            auto& hopper = hoppers.at(targetPos);
            // Dropper block ticks precede this tick's hopper decrement.
            hopper.readyAt = currentTick + (hopper.firstTick <= currentTick ? 7 : 8);
        }
        containerChanged(targetPos);
        --stack.count;
        break;
    }
    // Vanilla calls setItem even when the selected stack could not be inserted.
    writeStack(source, stack);
}
}
