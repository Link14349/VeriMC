#include "simulator/simulator.hpp"

namespace simulator {
bool Simulator::addCompost(BlockPos pos, std::uint32_t item) {
    const auto state=world.get(pos);const int level=at(pos).staticAnalog;
    const float chance=registry.item(item).compostChance;
    if(level>=7 || chance<0)return false;
    // The first positive-probability item succeeds without drawing random bits.
    // Even guaranteed items at later levels still consume nextDouble.
    if((level==0 && chance>0) || worldRandom.nextDouble()<chance) {
        const auto next=registry.with(state,"level",level+1);setBlock(pos,next);
        emitGameEvent("block_change",pos,{false,false,false,next});
        if(level+1==7)schedule(pos,20);
    } else ++revision; // Random state changed even when the visible level did not.
    return true; // Valid input is consumed even when the level did not increase.
}

bool Simulator::insertCompost(BlockPos from, BlockPos into, ItemStack stack) {
    return from==into.relative(Direction::up) && stack.count && addCompost(into,stack.item);
}

void Simulator::emptyComposter(BlockPos pos, StateId state) {
    const auto next=registry.with(state,"level",0);setBlock(pos,next);
    emitGameEvent("block_change",pos,{false,false,false,next});
}

bool Simulator::transferComposter(BlockPos from, BlockPos into, bool pulling, const std::vector<InventorySlot>* targetSlots) {
    // targetSlots 非空 = 目标是那一格里的实体容器（漏斗矿车），推入分支对它不可达：
    // 矿车没有 ejectItems，只会吸取。
    if(!targetSlots && at(into).device==Device::composter) {
        if(from!=into.relative(Direction::up) || at(into).staticAnalog>=7)return false;
        for(const auto& slot:containerSlots(from)) {
            const auto stack=stackAt(slot);if(!stack.count)continue;
            auto remaining=stack;--remaining.count;writeStack(slot,remaining);
            if(insertCompost(from,into,stack))return true;
            writeStack(slot,stack,stack.count==1);
        }
        return false;
    }
    // 方块漏斗只会从正上方拉取，所以保留那条几何断言；漏斗矿车拉的是**上面第二格**，
    // 而原版 OutputContainer.canTakeItemThroughFace（ComposterBlock.java:471-473）只检查
    // 方向是 DOWN、物品是骨粉，与取用者离得多远无关，因此实体目标不受这条限制。
    if(!pulling || (!targetSlots && into!=from.relative(Direction::down)) || at(from).staticAnalog!=8)return false;
    const auto state=world.get(from);const ItemStack bone{registry.itemId("bone_meal"),1};
    // This is a temporary vanilla OutputContainer, not a persistent inventory.
    // SimpleContainer.removeItem invokes its setChanged before insertion. Even
    // a failed insertion empties the block; restoring the temporary slot emits
    // a second empty notification rather than restoring the compost level.
    emptyComposter(from,state);
    for(const auto& slot:targetSlots?*targetSlots:containerSlots(into)) {
        if(!canInsertStack(slot,bone))continue;
        const auto existing=stackAt(slot);
        if(existing.count && (existing.item!=bone.item || existing.count>=registry.item(bone.item).maxStack))continue;
        writeStack(slot,{bone.item,static_cast<std::uint16_t>(existing.count+1)},existing.count==0);
        // 矿车不是方块实体，AbstractMinecartContainer.setChanged 是空实现，不通知比较器。
        if(slot.entity<0)containerChanged(into);
        emptyComposter(from,state);return true;
    }
    emptyComposter(from,state);return false;
}
}
