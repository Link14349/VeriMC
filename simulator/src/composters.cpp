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

bool Simulator::transferComposter(BlockPos from, BlockPos into, bool pulling) {
    if(at(into).device==Device::composter) {
        if(from!=into.relative(Direction::up) || at(into).staticAnalog>=7)return false;
        for(const auto& slot:containerSlots(from)) {
            const auto stack=stackAt(slot);if(!stack.count)continue;
            auto remaining=stack;--remaining.count;writeStack(slot,remaining);
            if(insertCompost(from,into,stack))return true;
            writeStack(slot,stack,stack.count==1);
        }
        return false;
    }
    if(!pulling || into!=from.relative(Direction::down) || at(from).staticAnalog!=8)return false;
    const auto state=world.get(from);const ItemStack bone{registry.itemId("bone_meal"),1};
    // This is a temporary vanilla OutputContainer, not a persistent inventory.
    // SimpleContainer.removeItem invokes its setChanged before insertion. Even
    // a failed insertion empties the block; restoring the temporary slot emits
    // a second empty notification rather than restoring the compost level.
    emptyComposter(from,state);
    for(const auto& slot:containerSlots(into)) {
        if(!canInsertStack(slot,bone))continue;
        const auto existing=stackAt(slot);
        if(existing.count && (existing.item!=bone.item || existing.count>=registry.item(bone.item).maxStack))continue;
        writeStack(slot,{bone.item,static_cast<std::uint16_t>(existing.count+1)},existing.count==0);
        containerChanged(into);emptyComposter(from,state);return true;
    }
    emptyComposter(from,state);return false;
}
}
