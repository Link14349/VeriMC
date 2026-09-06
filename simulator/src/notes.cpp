#include "simulator/simulator.hpp"
#include <algorithm>

namespace simulator {
std::string Simulator::soundId(const Json& value) {
    if(!value.is_string())throw std::invalid_argument("自定义声音需要资源标识符或 null");
    auto sound=value.get<std::string>();
    if(sound.find(':')==std::string::npos)sound="minecraft:"+sound;
    const auto split=sound.find(':');
    auto valid=[](char c){return (c>='a'&&c<='z') || (c>='0'&&c<='9') || c=='_' || c=='-' || c=='.';};
    if(split==0 || split==sound.size()-1 || sound.size()>256 || !std::all_of(sound.begin(),sound.begin()+static_cast<std::ptrdiff_t>(split),valid)
        || !std::all_of(sound.begin()+static_cast<std::ptrdiff_t>(split+1),sound.end(),[&](char c){return valid(c)||c=='/';}))throw std::invalid_argument("无效声音资源标识符");
    return sound;
}
void Simulator::validateNoteRuntime(BlockPos pos) const {
    const auto& values=runtime.at(pos).values;
    if(values.contains("customSound"))soundId(values.at("customSound"));
    if(at(pos).device!=Device::noteBlock)return;
    if(values.contains("playCount") && (!values.at("playCount").is_number_integer() || values.at("playCount")<0 || values.at("playCount")==UINT64_MAX))throw std::invalid_argument("无效音符盒演奏次数");
    if(values.contains("lastPlayed")) {
        const auto& last=values.at("lastPlayed");const auto& instrument=registry.instrument(registry.instrumentId(last.at("instrument")));
        if(!last.at("tick").is_number_integer() || last.at("tick")<0 || !last.at("note").is_number_integer() || last.at("note")<0 || last.at("note")>24)throw std::invalid_argument("无效演奏时间或音高");
        const float expected=instrument.tunable?registry.notePitch(last.at("note")):1.F;
        if(last.at("pitch").get<float>()!=expected || (!instrument.custom && last.at("sound")!=instrument.sound))throw std::invalid_argument("演奏数据不一致");
        soundId(last.at("sound"));
    }
}
StateId Simulator::noteInstrument(BlockPos pos,StateId state) const {
    auto instrument=registry.type(world.get(pos.relative(Direction::up))).instrument;
    if(!registry.instrument(instrument).above) {
        instrument=registry.type(world.get(pos.relative(Direction::down))).instrument;
        if(registry.instrument(instrument).above)instrument=registry.instrumentId("harp");
    }
    return registry.with(state,"instrument",registry.instrument(instrument).name);
}
void Simulator::playNote(BlockPos pos,StateId state) {
    const auto& instrument=registry.instrument(registry.instrumentId(registry.property(state,"instrument")));
    if(!instrument.above && at(pos.relative(Direction::up)).device!=Device::air)return;
    // Block events deduplicate identical position/type/parameters, but every
    // play attempt emits its game event immediately, before powered is written.
    schedulePhase(pos,currentTick+(currentPhase<=1?0:1),1,0);
    emitGameEvent("note_block_play",pos);
}
void Simulator::noteEvent(BlockPos pos) {
    const auto id=world.get(pos);const auto& instrument=registry.instrument(registry.instrumentId(registry.property(id,"instrument")));
    std::string sound=instrument.sound;
    if(instrument.custom) {
        const auto above=pos.relative(Direction::up);const auto& name=registry.type(world.get(above)).name;
        if(name!="minecraft:player_head" && name!="minecraft:player_wall_head")return;
        auto head=runtime.find(above);
        if(head==runtime.end() || !head->second.values.contains("customSound"))return;
        sound=head->second.values.at("customSound");
    }
    const int note=std::stoi(registry.property(id,"note"));
    const float pitch=instrument.tunable?registry.notePitch(note):1.F;
    // Unlike ordinary playSound, NoteBlock explicitly uses Level.random.
    (void)worldRandom.nextLong();
    auto& values=runtime[pos].values;
    values["playCount"]=values.value("playCount",std::uint64_t{0})+1;
    values["lastPlayed"]={{"tick",currentTick},{"instrument",instrument.name},{"note",note},{"pitch",pitch},{"sound",sound}};
    changes[pos]=id;++revision; // Diagnostic data has no comparator side effect.
}
bool Simulator::stimulateNote(BlockPos pos,const Json& input) {
    const auto id=world.get(pos);const auto& name=registry.type(id).name;
    if(at(pos).device==Device::noteBlock) {
        if(input.size()!=1 || !input.contains("playNote") || !input.at("playNote").is_boolean() || !input.at("playNote").get<bool>())throw std::invalid_argument("音符盒演奏输入需要 playNote: true");
        playNote(pos,id);return true;
    }
    if(name=="minecraft:player_head" || name=="minecraft:player_wall_head") {
        if(input.size()!=1 || !input.contains("customSound"))throw std::invalid_argument("玩家头颅输入需要 customSound");
        const auto& value=input.at("customSound");
        if(value.is_null())runtime[pos].values.erase("customSound");
        else {auto sound=soundId(value);runtime[pos].values["customSound"]=sound;}
        changes[pos]=id;++revision;return true;
    }
    return false;
}
}
