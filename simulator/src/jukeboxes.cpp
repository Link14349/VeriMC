#include "simulator/simulator.hpp"

namespace simulator {
void Simulator::startJukebox(BlockPos pos) {
    if(jukeboxes.contains(pos))return;
    jukeboxes.emplace(pos,JukeboxState{});updateJukeboxTicker(pos);
}
void Simulator::removeJukebox(BlockPos pos,std::uint16_t oldType) {
    auto found=jukeboxes.find(pos);if(found==jukeboxes.end())return;
    scheduledKeys.erase({pos,oldType,2,found->second.generation});jukeboxes.erase(found);
    emitGameEvent("jukebox_stop_play",pos);
}
void Simulator::updateJukeboxTicker(BlockPos pos) {
    auto found=jukeboxes.find(pos);if(found==jukeboxes.end())return;
    auto& player=found->second;
    if(player.wakeAt!=UINT64_MAX)scheduledKeys.erase({pos,at(pos).type,2,player.generation});
    player.wakeAt=UINT64_MAX;
    // Vanilla removes its ticker wrapper when has_record becomes false. A new
    // wrapper joins at the end, even though the block entity itself survives.
    if(registry.property(world.get(pos),"has_record")!="true") {entityOrders.erase(pos);return;}
    if(!entityOrders.contains(pos)) {
        registerEntity(pos);player.firstTick=currentTick+(beforeBlockEntities()?0:1);
    }
    if(player.song<0)return;
    const auto rank=entityOrders.at(pos);
    const bool beforeTick=beforeBlockEntities() || (currentPhase==2 && rank>currentEntityOrder);
    player.wakeAt=std::max(player.firstTick,currentTick+(beforeTick?0:1));player.generation=nextOrder++;
    schedulePhase(pos,player.wakeAt,2,player.generation);
}
void Simulator::updateJukeboxItem(BlockPos pos) {
    const auto stack=stackAt({pos,0});auto& player=jukeboxes.at(pos);
    const bool wasPlaying=player.song>=0;
    setBlock(pos,registry.withBool(world.get(pos),"has_record",stack.count>0),2);
    emitGameEvent("block_change",pos,{false,false,false,world.get(pos)});
    player.song=stack.count?registry.item(stack.item).jukeboxSong:-1;player.elapsed=0;
    updateJukeboxTicker(pos);
    if(player.song>=0 || wasPlaying) {
        if(player.song<0)emitGameEvent("jukebox_stop_play",pos,{false,false,false,world.get(pos)});
        updateNeighbors(pos,-1,world.get(pos));runtimeChanged(pos);
    }
}
void Simulator::tickJukebox(const ScheduledEvent& event) {
    auto found=jukeboxes.find(event.pos);if(found==jukeboxes.end() || found->second.generation!=event.data)return;
    auto& player=found->second;player.wakeAt=UINT64_MAX;
    if(player.song<0 || registry.property(world.get(event.pos),"has_record")!="true")return;
    if(player.elapsed>=registry.song(player.song).lengthTicks+20) {
        player.song=-1;player.elapsed=0;emitGameEvent("jukebox_stop_play",event.pos,{false,false,false,world.get(event.pos)});
        updateNeighbors(event.pos,-1,world.get(event.pos));runtimeChanged(event.pos);return;
    }
    if(player.elapsed%20==0) {
        emitGameEvent("jukebox_play",event.pos,{false,false,false,world.get(event.pos)});
        (void)worldRandom.nextInt(4); // Original server-side note particle color.
        changes[event.pos]=world.get(event.pos);++revision;
    }
    ++player.elapsed;player.wakeAt=currentTick+1;schedulePhase(event.pos,player.wakeAt,2,event.data);
}
}
