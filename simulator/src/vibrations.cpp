#include "simulator/simulator.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace simulator {
namespace {
BlockPos containing(const Vec3& p) { return {static_cast<int>(std::floor(p[0])),static_cast<int>(std::floor(p[1])),static_cast<int>(std::floor(p[2]))}; }
Vec3 center(BlockPos p) { return {p.x+.5,p.y+.5,p.z+.5}; }
int section(int n) { return n>=0?n/16:(n+1)/16-1; }
BlockPos sectionOf(BlockPos p) { return {section(p.x),section(p.y),section(p.z)}; }
double distanceSquared(const Vec3& a,const Vec3& b) {
    const double x=a[0]-b[0],y=a[1]-b[1],z=a[2]-b[2];return x*x+y*y+z*z;
}
}

void Simulator::startSensor(BlockPos pos) {
    if(sensors.contains(pos)) return;
    registerEntity(pos);sensors.emplace(pos,SensorState{});
    auto& listeners=sensorSections[sectionOf(pos)];
    auto before=std::lower_bound(listeners.begin(),listeners.end(),entityOrders.at(pos),[&](BlockPos p,std::uint64_t rank){return entityOrders.at(p)<rank;});
    listeners.insert(before,pos);
}
void Simulator::removeSensor(BlockPos pos,std::uint16_t oldType) {
    auto found=sensors.find(pos);if(found==sensors.end())return;
    scheduledKeys.erase({pos,oldType,2,found->second.generation});sensors.erase(found);
    auto sectionFound=sensorSections.find(sectionOf(pos));
    if(sectionFound!=sensorSections.end()) {
        std::erase(sectionFound->second,pos);
        if(sectionFound->second.empty())sensorSections.erase(sectionFound);
    }
}
void Simulator::rebuildSensorIndex() {
    sensorSections.clear();
    for(const auto& [pos,state]:sensors) {(void)state;sensorSections[sectionOf(pos)].push_back(pos);}
    for(auto& [pos,list]:sensorSections) {(void)pos;std::sort(list.begin(),list.end(),[&](BlockPos a,BlockPos b){return entityOrders.at(a)<entityOrders.at(b);});}
}

bool Simulator::vibrationOccluded(const Vec3& origin,BlockPos destination) const {
    const auto from=center(containing(origin)),to=center(destination);
    // Original traversal tests whole cells against the wool tag. All six
    // float-nudged rays must hit wool; opacity and visual shapes are irrelevant.
    for(auto direction:directions) {
        const auto unit=BlockPos{}.relative(direction);const std::array<int,3> steps{unit.x,unit.y,unit.z};
        Vec3 start=from,end=to;
        for(std::size_t i=0;i<3;++i) start[i]+=steps[i]*static_cast<double>(1.0e-5F);
        const auto nudged=start;
        for(std::size_t i=0;i<3;++i) {
            end[i]=to[i]+(-1.0e-7)*(nudged[i]-to[i]);
            start[i]=nudged[i]+(-1.0e-7)*(to[i]-nudged[i]);
        }
        auto block=containing(start);std::array<int,3> cell{block.x,block.y,block.z},sign{};Vec3 delta{},crossing{};
        for(std::size_t i=0;i<3;++i) {
            const double span=end[i]-start[i];sign[i]=(span>0)-(span<0);
            delta[i]=sign[i]==0?std::numeric_limits<double>::max():sign[i]/span;
            const double fraction=start[i]-std::floor(start[i]);crossing[i]=delta[i]*(sign[i]>0?1.0-fraction:fraction);
        }
        bool blocked=registry.type(world.get(block)).occludesVibrations;
        while(!blocked && (crossing[0]<=1.0 || crossing[1]<=1.0 || crossing[2]<=1.0)) {
            const std::size_t next=crossing[0]<crossing[1]?(crossing[0]<crossing[2]?0:2):(crossing[1]<crossing[2]?1:2);
            cell[next]+=sign[next];crossing[next]+=delta[next];
            blocked=registry.type(world.get({cell[0],cell[1],cell[2]})).occludesVibrations;
        }
        if(!blocked)return false;
    }
    return true;
}

void Simulator::emitGameEvent(const std::string& event,BlockPos pos,VibrationContext context) {
    if(sensorSections.empty())return;
    emitGameEvent(registry.gameEventId(event),center(pos),context);
}
void Simulator::emitGameEvent(std::uint16_t event,const Vec3& origin,VibrationContext context) {
    const auto& info=registry.gameEvent(event);
    if(sensorSections.empty() || !info.listenable || !info.frequency || context.spectator || context.dampens || (context.sneaking && info.ignoreSneaking))return;
    if(context.affectedState!=UINT32_MAX && registry.type(context.affectedState).dampensVibrations)return;
    const auto source=containing(origin);const int radius=info.radius;
    const auto minimum=sectionOf({source.x-radius,source.y-radius,source.z-radius}),maximum=sectionOf({source.x+radius,source.y+radius,source.z+radius});
    for(int x=minimum.x;x<=maximum.x;++x) for(int z=minimum.z;z<=maximum.z;++z) for(int y=minimum.y;y<=maximum.y;++y) {
        const auto group=sensorSections.find({x,y,z});if(group==sensorSections.end())continue;
        for(const auto pos:group->second) {
            auto& sensor=sensors.at(pos);const auto id=world.get(pos);const bool calibrated=at(pos).device==Device::calibratedSensor;
            const int range=calibrated?16:8;
            if(sensor.current || distanceSquared(center(source),center(pos))>range*range || registry.property(id,"sculk_sensor_phase")!="inactive")continue;
            if(pos==source && (info.name=="minecraft:block_place" || info.name=="minecraft:block_destroy"))continue;
            if(calibrated) {auto back=opposite(at(pos).facing);int filter=signal(pos.relative(back),back);if(filter && filter!=info.frequency)continue;}
            if(vibrationOccluded(origin,pos))continue;
            const auto distance=static_cast<float>(std::sqrt(distanceSquared(origin,center(pos))));
            if(sensor.candidate) {
                const auto& previous=*sensor.candidate;
                if(sensor.candidateTick!=currentTick || distance>previous.distance || (distance==previous.distance && info.frequency<=registry.gameEvent(previous.event).frequency))continue;
            }
            sensor.candidate=VibrationInfo{event,origin,distance,context};sensor.candidateTick=currentTick;
            changes[pos]=id;++revision; // Inspector update, without a block notification.
            if(sensor.wakeAt==UINT64_MAX) {
                if(currentTick>=UINT64_MAX-1)throw std::invalid_argument("振动计划时间超出范围");
                sensor.wakeAt=currentTick+1;sensor.generation=nextOrder++;
                schedulePhase(pos,sensor.wakeAt,2,sensor.generation);
            }
        }
    }
}

void Simulator::stimulateVibration(BlockPos pos,const Json& input) {
    if(!input.is_object())throw std::invalid_argument("振动输入必须为对象");
    for(const auto& [key,value]:input.items()) {(void)value;if(key!="gameEvent" && key!="offset" && key!="source" && key!="affectedBlock")throw std::invalid_argument("未知振动输入字段："+key);}
    const auto event=registry.gameEventId(input.at("gameEvent"));
    const auto offset=input.value("offset",Json::array({.5,.5,.5}));
    if(!offset.is_array() || offset.size()!=3)throw std::invalid_argument("事件格内位置需要三个坐标");
    auto origin=center(pos);const std::array<int,3> base{pos.x,pos.y,pos.z};
    for(std::size_t i=0;i<3;++i) {
        if(!offset[i].is_number())throw std::invalid_argument("事件格内坐标必须为数值");
        const double value=offset[i];if(!std::isfinite(value) || value<0 || value>1)throw std::invalid_argument("事件格内坐标必须在 0–1 之间");
        origin[i]=base[i]+value;
    }
    VibrationContext context;
    if(input.contains("source")) {
        const auto& source=input.at("source");if(!source.is_object())throw std::invalid_argument("振动来源必须为对象");
        for(const auto& [key,value]:source.items()) if(!value.is_boolean() || (key!="spectator" && key!="sneaking" && key!="dampensVibrations"))throw std::invalid_argument("无效振动来源属性");
        context.spectator=source.value("spectator",false);context.sneaking=source.value("sneaking",false);context.dampens=source.value("dampensVibrations",false);
    }
    if(input.contains("affectedBlock")) {
        const auto& affected=input.at("affectedBlock");context.affectedState=registry.state(affected.at("name"),affected.value("properties",Json::object()));
    }
    emitGameEvent(event,origin,context);++revision;
}

void Simulator::tickVibration(const ScheduledEvent& event) {
    auto found=sensors.find(event.pos);if(found==sensors.end() || found->second.generation!=event.data)return;
    auto& sensor=found->second;sensor.wakeAt=UINT64_MAX;
    if(!sensor.current && sensor.candidate && sensor.candidateTick<currentTick) {
        sensor.current=sensor.candidate;sensor.candidate.reset();sensor.remaining=static_cast<int>(std::floor(sensor.current->distance));
        runtimeChanged(event.pos);
    }
    found=sensors.find(event.pos);if(found==sensors.end() || found->second.generation!=event.data || !found->second.current)return;
    const auto vibration=*found->second.current;found->second.remaining=std::max(0,found->second.remaining-1);
    if(found->second.remaining==0) {
        activateSensor(event.pos,vibration);
        found=sensors.find(event.pos);if(found==sensors.end() || found->second.generation!=event.data)return;
        found->second.current.reset();
    } else {
        found->second.wakeAt=currentTick+1;schedulePhase(event.pos,currentTick+1,2,event.data);
    }
    // Original onDataChanged/setChanged notifies comparator neighbors on every
    // travelling tick. Preserve this; only idle listeners have zero tick work.
    runtimeChanged(event.pos);
}

void Simulator::activateSensor(BlockPos pos,const VibrationInfo& vibration) {
    const auto id=world.get(pos);if(registry.property(id,"sculk_sensor_phase")!="inactive")return;
    const int frequency=registry.gameEvent(vibration.event).frequency,range=at(pos).device==Device::calibratedSensor?16:8;
    const auto distance=static_cast<float>(std::sqrt(distanceSquared(center(containing(vibration.origin)),center(pos))));
    const int power=std::max(1,15-static_cast<int>(std::floor((15.0/range)*distance)));
    runtime[pos].values["lastVibrationFrequency"]=frequency;
    setBlock(pos,registry.with(registry.with(id,"sculk_sensor_phase",std::string("active")),"power",power));
    schedule(pos,range==16?10:30);updateNeighbors(pos,-1,id);updateNeighbors(pos.relative(Direction::down),-1,id);
    for(auto direction:directions) {
        const auto adjacent=pos.relative(direction);const auto state=world.get(adjacent);
        if(registry.type(state).vibrationResonator) {
            auto context=vibration.context;context.affectedState=state;
            emitGameEvent("resonate_"+std::to_string(frequency),adjacent,context);
        }
    }
    auto context=vibration.context;context.affectedState=UINT32_MAX;emitGameEvent("sculk_sensor_tendrils_clicking",pos,context);
    if(registry.property(id,"waterlogged")!="true") (void)worldRandom.nextFloat();
}
void Simulator::tickSensor(BlockPos pos) {
    const auto id=world.get(pos);const auto phase=registry.property(id,"sculk_sensor_phase");
    if(phase=="active") {
        setBlock(pos,registry.with(registry.with(id,"sculk_sensor_phase",std::string("cooldown")),"power",0));
        schedule(pos,10);updateNeighbors(pos,-1,id);updateNeighbors(pos.relative(Direction::down),-1,id);
    } else if(phase=="cooldown") {
        setBlock(pos,registry.with(id,"sculk_sensor_phase",std::string("inactive")));
        if(registry.property(id,"waterlogged")!="true") (void)worldRandom.nextFloat();
    }
}
}
