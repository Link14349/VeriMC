#include "simulator/simulator.hpp"
#include <cmath>

namespace simulator {
Direction Simulator::bellSupport(StateId state) const {
    const auto attachment=registry.property(state,"attachment");
    return attachment=="floor"?Direction::down:attachment=="ceiling"?Direction::up:registry[state].facing;
}
void Simulator::updateBellShape(const Update& update) {
    auto id=world.get(update.pos);const auto attachment=registry.property(id,"attachment");
    const auto support=bellSupport(id),direction=update.direction;
    if(support==direction && attachment!="double_wall" && !survives(update.pos,id)) {setBlock(update.pos,0,3,update.depth);return;}
    if(axis(direction)!=axis(registry[id].facing))return;
    if(attachment=="double_wall" && !(registry[update.neighborState].supportMask & (1u<<static_cast<unsigned>(direction))))
        id=registry.with(registry.with(id,"attachment",std::string("single_wall")),"facing",std::string(directionNames[static_cast<unsigned>(opposite(direction))]));
    else if(attachment=="single_wall" && opposite(support)==direction && (registry[update.neighborState].supportMask & (1u<<static_cast<unsigned>(registry[id].facing))))
        id=registry.with(id,"attachment",std::string("double_wall"));
    setBlock(update.pos,id,update.flags,update.depth);
}
void Simulator::ringBell(BlockPos pos,Direction direction) {
    auto& values=runtime[pos].values;
    values["ringCount"]=values.value("ringCount",std::uint64_t{0})+1;
    values["lastRingTick"]=currentTick;values["ringDirection"]=directionNames[static_cast<unsigned>(direction)];values["ringing"]=true;
    schedulePhase(pos,currentTick+(currentPhase<=1?0:1),1,1u|(static_cast<std::uint64_t>(direction)<<2));
    emitGameEvent("block_change",pos);
    changes[pos]=world.get(pos);++revision;
}
bool Simulator::stimulateBell(BlockPos pos,const Json& input) {
    if(at(pos).device!=Device::bell)return false;
    if(input.size()==1 && input.contains("ring") && input.at("ring").is_boolean() && input.at("ring").get<bool>()) {ringBell(pos,at(pos).facing);return true;}
    if(input.size()!=2 || !input.contains("face") || !input.contains("height") || !input.at("height").is_number())throw std::invalid_argument("钟输入需要 face 和 height，或 ring: true");
    const auto direction=parseDirection(input.at("face"));const double height=input.at("height");
    if(!std::isfinite(height) || height<0 || height>1)throw std::invalid_argument("敲钟高度必须在格内 0–1 范围");
    // Match BlockHitResult world addition and the original widened float bound.
    if(axis(direction)==0 || (pos.y+height)-pos.y>static_cast<double>(.8124F))return true;
    const auto attachment=registry.property(world.get(pos),"attachment");
    const bool sameAxis=axis(direction)==axis(at(pos).facing);
    if((attachment=="floor" && !sameAxis) || ((attachment=="single_wall" || attachment=="double_wall") && sameAxis))return true;
    ringBell(pos,direction);return true;
}
void Simulator::bellEvent(const ScheduledEvent& event) {
    auto& values=runtime[event.pos].values;
    if(values.contains("bellGeneration"))scheduledKeys.erase({event.pos,event.type,2,values.at("bellGeneration")});
    if(currentTick>UINT64_MAX-50)throw std::invalid_argument("钟摆动结束时间超出范围");
    const auto generation=nextOrder++;values["bellGeneration"]=generation;values["bellWakeAt"]=currentTick+49;
    values["ringDirection"]=directionNames[event.data>>2];values["ringing"]=true;
    // No entity AI is present. The intermediate shake ticks neither notify
    // blocks nor consume RNG; wake at the original fiftieth entity tick.
    schedulePhase(event.pos,currentTick+49,2,generation);
    changes[event.pos]=world.get(event.pos);++revision;
}
void Simulator::finishBell(const ScheduledEvent& event) {
    auto found=runtime.find(event.pos);if(found==runtime.end() || found->second.values.value("bellGeneration",UINT64_MAX)!=event.data)return;
    auto& values=found->second.values;
    // 停摆期间事件被放回当前刻，恢复后会提前触发一次；而 bellWakeAt 已被
    // shiftBlockEntityTimers 按停摆时长后移。这里按新的结束时刻重排，等价于
    // 原版「区块不 ticking 时钟根本不 tick」——与漏斗 readyAt 的处理方式一致。
    if(values.value("bellWakeAt",Tick{})>currentTick) {
        schedulePhase(event.pos,values.at("bellWakeAt").get<Tick>(),2,event.data);return;
    }
    values["ringing"]=false;values.erase("bellWakeAt");values.erase("bellGeneration");
    changes[event.pos]=world.get(event.pos);++revision;
}
}
