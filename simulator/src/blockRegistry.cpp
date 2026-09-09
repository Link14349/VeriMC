#include "simulator/blockRegistry.hpp"
#include "simulator/vmcbEncoding.hpp"
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <unordered_set>

namespace simulator {
namespace {
Device classify(const std::string& name, const std::string& c) {
    static const std::unordered_map<std::string, Device> classes{
        {"ComposterBlock", Device::composter}, {"JukeboxBlock", Device::jukebox}, {"BellBlock", Device::bell}, {"AirBlock", Device::air}, {"RedStoneWireBlock", Device::wire}, {"RedstoneBlock", Device::source},
        {"LeverBlock", Device::lever}, {"ButtonBlock", Device::button}, {"RedstoneTorchBlock", Device::torch},
        {"RedstoneWallTorchBlock", Device::wallTorch}, {"RepeaterBlock", Device::repeater}, {"ComparatorBlock", Device::comparator},
        {"ObserverBlock", Device::observer}, {"RedstoneLampBlock", Device::lamp}, {"CopperBulbBlock", Device::bulb},
        {"WeatheringCopperBulbBlock", Device::bulb}, {"PistonBaseBlock", Device::piston}, {"PistonHeadBlock", Device::pistonHead},
        {"MovingPistonBlock", Device::movingPiston}, {"HopperBlock", Device::hopper}, {"ChestBlock", Device::container},
        {"TrappedChestBlock", Device::container}, {"BarrelBlock", Device::container}, {"ShulkerBoxBlock", Device::container},
        {"CopperChestBlock", Device::container}, {"WeatheringCopperChestBlock", Device::container},
        {"DispenserBlock", Device::dispenser}, {"DropperBlock", Device::dropper}, {"CrafterBlock", Device::crafter},
        {"FurnaceBlock", Device::furnace}, {"BlastFurnaceBlock", Device::furnace}, {"SmokerBlock", Device::furnace},
        {"DoorBlock", Device::door}, {"TrapDoorBlock", Device::trapdoor}, {"WeatheringCopperDoorBlock", Device::door},
        {"WeatheringCopperTrapDoorBlock", Device::trapdoor}, {"FenceGateBlock", Device::fenceGate}, {"NoteBlock", Device::noteBlock},
        {"RailBlock", Device::rail}, {"PoweredRailBlock", Device::poweredRail}, {"DetectorRailBlock", Device::detectorRail},
        {"PressurePlateBlock", Device::pressurePlate}, {"WeightedPressurePlateBlock", Device::weightedPlate},
        {"TargetBlock", Device::target}, {"DaylightDetectorBlock", Device::daylight},
        {"LightningRodBlock", Device::lightningRod}, {"WeatheringLightningRodBlock", Device::lightningRod},
        {"SculkSensorBlock", Device::sculkSensor}, {"CalibratedSculkSensorBlock", Device::calibratedSensor},
        {"TripWireBlock", Device::tripwire}, {"TripWireHookBlock", Device::tripwireHook}, {"LecternBlock", Device::lectern}
    };
    if (name == "minecraft:activator_rail") return Device::activatorRail;
    if (auto it = classes.find(c); it != classes.end()) return it->second;
    if (name == "minecraft:redstone_block") return Device::source;
    static const std::vector<std::string> noteBases{"clay","gold_block","packed_ice","bone_block","iron_block","soul_sand","pumpkin","emerald_block","hay_block","copper_block","exposed_copper","weathered_copper","oxidized_copper","waxed_copper_block","waxed_exposed_copper","waxed_weathered_copper","waxed_oxidized_copper"};
    if(std::find(noteBases.begin(),noteBases.end(),name.substr(10))!=noteBases.end())return Device::solid;
    if(c=="WitherSkullBlock" || c=="WitherWallSkullBlock" || c=="SkullBlock" || c=="WallSkullBlock" || c=="PlayerHeadBlock" || c=="PlayerWallHeadBlock")return Device::solid;
    static const std::vector<std::string> analogClasses{"ComposterBlock", "CakeBlock", "CandleCakeBlock", "CauldronBlock", "LayeredCauldronBlock", "LavaCauldronBlock", "ChiseledBookShelfBlock", "DecoratedPotBlock", "JukeboxBlock", "CopperGolemStatueBlock", "WeatheringCopperGolemStatueBlock", "RespawnAnchorBlock", "BeehiveBlock", "EndPortalFrameBlock"};
    if (std::find(analogClasses.begin(), analogClasses.end(), c) != analogClasses.end()) return Device::analog;
    // The palette admits a deliberate structural whitelist. Unknown behavior stays explicit.
    if (name == "minecraft:slime_block" || name == "minecraft:honey_block" || name == "minecraft:obsidian" || name == "minecraft:bedrock" || name == "minecraft:stone" || name == "minecraft:smooth_stone" || name == "minecraft:glass" || name == "minecraft:glowstone" || name == "minecraft:sea_lantern" || name == "minecraft:target" || name.ends_with("_wool") || name.ends_with("_concrete") || name.ends_with("_planks") || name.ends_with("_terracotta") || name.ends_with("_stained_glass") || name.ends_with("_slab") || name.ends_with("_stairs")) return Device::solid;
    return Device::unsupported;
}
}
BlockRegistry::BlockRegistry(const std::string& path) {
    rulesHash = vmcb::rulesDigest(path);
    std::ifstream file(path);
    if (!file) throw std::runtime_error("找不到方块注册表：" + path);
    std::ifstream itemFile(std::filesystem::path(path).parent_path() / "itemDefinitions.json");
    if (!itemFile) throw std::runtime_error("找不到物品注册表 itemDefinitions.json");
    const auto itemData = Json::parse(itemFile);
    if (itemData.at("version") != "26.2") throw std::runtime_error("Item registry version mismatch");
    for (const auto& row : itemData.at("items")) {
        itemNames.emplace(row.at("name").get<std::string>(), static_cast<std::uint32_t>(items.size()));
        items.push_back({row.at("name"), row.at("maxStack"), row.value("bookshelfBook",false)});
    }
    std::ifstream jukeboxFile(std::filesystem::path(path).parent_path()/"jukeboxRules.json");
    if(!jukeboxFile)throw std::runtime_error("找不到唱片机规则 jukeboxRules.json");
    const auto jukeboxData=Json::parse(jukeboxFile);
    if(jukeboxData.at("version")!="26.2")throw std::runtime_error("Jukebox registry version mismatch");
    for(const auto& row:jukeboxData.at("songs"))songs.push_back({row.at("name"),row.at("sound"),row.at("lengthTicks"),row.at("comparatorOutput")});
    for(const auto& [name,songName]:jukeboxData.at("items").items())items.at(itemId(name)).jukeboxSong=songId(songName);
    std::ifstream compostFile(std::filesystem::path(path).parent_path()/"compostingRules.json");
    if(!compostFile)throw std::runtime_error("找不到堆肥规则 compostingRules.json");
    const auto compostData=Json::parse(compostFile);
    if(compostData.at("version")!="26.2")throw std::runtime_error("Composting registry version mismatch");
    for(const auto& [name,chance]:compostData.at("items").items())items.at(itemId(name)).compostChance=chance;
    std::ifstream vibrationFile(std::filesystem::path(path).parent_path() / "vibrationRules.json");
    if(!vibrationFile) throw std::runtime_error("找不到振动规则 vibrationRules.json");
    const auto vibrationData=Json::parse(vibrationFile);
    if(vibrationData.at("version")!="26.2") throw std::runtime_error("Vibration registry version mismatch");
    for(const auto& row:vibrationData.at("events")) {
        gameEventNames.emplace(row.at("name").get<std::string>(),static_cast<std::uint16_t>(gameEvents.size()));
        gameEvents.push_back({row.at("name"),row.at("radius"),row.at("frequency"),row.at("listenable"),row.at("ignoreSneaking")});
    }
    std::ifstream tagFile(std::filesystem::path(path).parent_path() / "blockTags.json");
    if (!tagFile) throw std::runtime_error("找不到方块标签 blockTags.json");
    const auto tagData = Json::parse(tagFile);
    if (tagData.at("version") != "26.2") throw std::runtime_error("Block tag registry version mismatch");
    std::unordered_set<std::string> wallNames, hopperTransparentNames;
    for (const auto& name : tagData.at("walls")) wallNames.insert(name.get<std::string>());
    for (const auto& name : tagData.at("does_not_block_hoppers")) hopperTransparentNames.insert(name.get<std::string>());
    const Json data = Json::parse(file);
    std::ifstream noteFile(std::filesystem::path(path).parent_path()/"noteRules.json");
    if(!noteFile)throw std::runtime_error("找不到音符盒规则 noteRules.json");
    const auto noteData=Json::parse(noteFile);
    if(noteData.at("version")!="26.2")throw std::runtime_error("Note registry version mismatch");
    for(const auto& row:noteData.at("instruments"))instruments.push_back({row.at("name"),row.at("sound"),row.at("tunable"),row.at("above"),row.at("custom")});
    notePitches=noteData.at("pitches").get<std::array<float,25>>();
    if (data.at("version") != "26.2") throw std::runtime_error("需要 Minecraft 26.2 数据");
    for (const auto& b : data.at("blocks")) {
        BlockType typeInfo;
        typeInfo.name = b.at("name"); typeInfo.className = b.at("className");
        typeInfo.defaultState = b.at("defaultState"); typeInfo.firstState = b.at("states")[0].at("id");
        typeInfo.stairs = b.at("stairs");
        typeInfo.wall = wallNames.contains(typeInfo.name);
        typeInfo.doesNotBlockHoppers = hopperTransparentNames.contains(typeInfo.name);
        typeInfo.device = classify(typeInfo.name, typeInfo.className);
        typeInfo.instrument=instrumentId(noteData.at("blocks").at(typeInfo.name));
        for(const auto& name:vibrationData.at("occludes_vibration_signals")) if(name==typeInfo.name) typeInfo.occludesVibrations=true;
        for(const auto& name:vibrationData.at("dampens_vibrations")) if(name==typeInfo.name) typeInfo.dampensVibrations=true;
        for(const auto& name:vibrationData.at("vibration_resonators")) if(name==typeInfo.name) typeInfo.vibrationResonator=true;
        if(typeInfo.occludesVibrations || typeInfo.dampensVibrations || typeInfo.vibrationResonator) typeInfo.device=Device::solid;
        typeInfo.supportLevel = (typeInfo.device <= Device::movingPiston || typeInfo.device == Device::target) ? "implemented" : "unimplemented";
        if (typeInfo.device == Device::door || typeInfo.device == Device::trapdoor || typeInfo.device == Device::fenceGate) typeInfo.supportLevel = "implemented";
        if (typeInfo.device == Device::pressurePlate || typeInfo.device == Device::weightedPlate || typeInfo.device == Device::lightningRod || typeInfo.device == Device::daylight || typeInfo.device == Device::lectern) typeInfo.supportLevel = "externalStimulus";
        static const std::vector<std::string> environmentReadouts{"CauldronBlock", "LayeredCauldronBlock", "LavaCauldronBlock", "RespawnAnchorBlock", "BeehiveBlock", "EndPortalFrameBlock", "CopperGolemStatueBlock", "WeatheringCopperGolemStatueBlock"};
        if (std::find(environmentReadouts.begin(), environmentReadouts.end(), typeInfo.className) != environmentReadouts.end()) typeInfo.supportLevel = "externalStimulus";
        if (typeInfo.className == "ChestBlock" || typeInfo.className == "TrappedChestBlock" || typeInfo.className == "BarrelBlock") typeInfo.supportLevel = "implemented";
        if (typeInfo.className == "CopperChestBlock" || typeInfo.className == "WeatheringCopperChestBlock") typeInfo.supportLevel = "implemented";
        if (typeInfo.device == Device::hopper) typeInfo.supportLevel = "implemented";
        if (typeInfo.device == Device::dropper) typeInfo.supportLevel = "partial";
        if (typeInfo.device == Device::composter) typeInfo.supportLevel = "partial";
        if(typeInfo.device==Device::jukebox || typeInfo.device==Device::bell || typeInfo.device==Device::noteBlock || typeInfo.className.find("SkullBlock")!=std::string::npos || typeInfo.className=="PlayerHeadBlock" || typeInfo.className=="PlayerWallHeadBlock")typeInfo.supportLevel="partial";
        if (typeInfo.className == "ChiseledBookShelfBlock") typeInfo.supportLevel = "partial";
        if (typeInfo.className == "DecoratedPotBlock") typeInfo.supportLevel = "partial";
        if (typeInfo.device == Device::sculkSensor || typeInfo.device == Device::calibratedSensor) typeInfo.supportLevel = "partial";
        if (typeInfo.device == Device::target) typeInfo.supportLevel = "externalStimulus";
        if (typeInfo.device == Device::rail || typeInfo.device == Device::poweredRail || typeInfo.device == Device::activatorRail) typeInfo.supportLevel = "implemented";
        if (typeInfo.device == Device::detectorRail) typeInfo.supportLevel = "externalStimulus";
        if (typeInfo.device == Device::tripwire) typeInfo.supportLevel = "externalStimulus";
        if (typeInfo.device == Device::tripwireHook) typeInfo.supportLevel = "implemented";
        // R14 门禁：发射器/合成器/熔炉只有占位枚举和容量，行为表尚未实现。
        // 谁想放开 supportLevel，必须先实现分发表、槽位禁用和分面槽位，否则这里立刻失败。
        if ((typeInfo.device == Device::dispenser || typeInfo.device == Device::crafter || typeInfo.device == Device::furnace)
            && typeInfo.supportLevel != "unimplemented")
            throw std::runtime_error("该器件的行为尚未实现，不能标记为可用：" + typeInfo.name);
        auto typeId = static_cast<std::uint16_t>(types.size());
        names.emplace(typeInfo.name, typeId);
        for (const auto& s : b.at("states")) for (const auto& [key, value] : s.at("properties").items()) {
            auto& values = typeInfo.properties[key].values;
            const auto str = value.get<std::string>();
            if (std::find(values.begin(), values.end(), str) == values.end()) values.push_back(str);
        }
        for (auto& [key, prop] : typeInfo.properties) {
            prop.stride = 1;
            if (prop.values.size() > 1) for (const auto& s : b.at("states")) {
                if (s.at("properties").at(key) == prop.values[1]) { prop.stride = s.at("id").get<StateId>() - typeInfo.firstState; break; }
            }
        }
        types.push_back(typeInfo);
        for (const auto& s : b.at("states")) {
            StateId id = s.at("id");
            if (states.size() <= id) states.resize(static_cast<std::size_t>(id) + 1);
            auto& st = states[id]; st.type = typeId; st.device = typeInfo.device;
            st.conductor = s.at("conductor"); st.fullCube = s.at("fullCube"); st.analogSource = s.at("analogSource");
            st.blockEntity = s.at("blockEntity"); st.replaceable = s.at("replaceable"); st.signalSource = s.at("signalSource");
            st.supportMask = s.at("supportMask"); st.rigidMask = s.at("rigidMask"); st.centerMask = s.at("centerMask");
            st.weak = s.at("weakSignal").get<decltype(st.weak)>(); st.strong = s.at("strongSignal").get<decltype(st.strong)>();
            auto reaction = s.at("pushReaction").get<std::string>();
            st.pushReaction = reaction == "NORMAL" ? 0 : reaction == "DESTROY" ? 1 : reaction == "BLOCK" ? 2 : reaction == "PUSH_ONLY" ? 3 : 4;
            st.indestructible = s.at("destroySpeed").get<float>() == -1.0f;
            const auto& p = s.at("properties");
            auto val = [&](const char* key, const char* fallback) { return p.value(key, std::string(fallback)); };
            st.facing = parseDirection(val("facing", "north"));
            st.power = static_cast<std::uint8_t>(std::stoi(val("power", "0"))); st.delay = static_cast<std::uint8_t>(std::stoi(val("delay", "1")));
            st.powered = val("powered", "false") == "true"; st.lit = val("lit", "false") == "true";
            st.locked = val("locked", "false") == "true"; st.extended = val("extended", "false") == "true";
            st.subtract = val("mode", "compare") == "subtract"; st.sticky = typeInfo.name == "minecraft:sticky_piston";
            const auto& className = typeInfo.className;
            if (className == "ComposterBlock") st.staticAnalog = static_cast<std::uint8_t>(std::stoi(val("level", "0")));
            if (className == "LayeredCauldronBlock") st.staticAnalog = static_cast<std::uint8_t>(std::stoi(val("level", "0")));
            if (className == "LavaCauldronBlock") st.staticAnalog = 3;
            if (className == "BeehiveBlock") st.staticAnalog = static_cast<std::uint8_t>(std::stoi(val("honey_level", "0")));
            if (className == "RespawnAnchorBlock") st.staticAnalog = static_cast<std::uint8_t>(std::stoi(val("charges", "0")) * 15 / 4);
            if (className == "EndPortalFrameBlock") st.staticAnalog = val("eye", "false") == "true" ? 15 : 0;
            if (className == "CopperGolemStatueBlock" || className == "WeatheringCopperGolemStatueBlock") {
                const auto& poses = typeInfo.properties.at("copper_golem_pose").values;
                st.staticAnalog = static_cast<std::uint8_t>(std::find(poses.begin(), poses.end(), val("copper_golem_pose", "standing")) - poses.begin() + 1);
            }
            auto face = val("face", "floor"); st.connectedDirection = face == "floor" ? Direction::up : face == "ceiling" ? Direction::down : st.facing;
            for (std::size_t i = 0; i < 4; ++i) { auto side = val(directionNames[static_cast<unsigned>(horizontal[i])], "none"); st.wireSides[i] = side == "up" ? 2 : side == "side" ? 1 : 0; }
        }
    }
}
std::uint8_t BlockRegistry::instrumentId(const std::string& name) const {
    for(std::size_t i=0;i<instruments.size();++i)if(instruments[i].name==name)return static_cast<std::uint8_t>(i);
    throw std::invalid_argument("未知乐器："+name);
}
int BlockRegistry::songId(const std::string& name) const {
    const auto key=name.find(':')==std::string::npos?"minecraft:"+name:name;
    for(std::size_t i=0;i<songs.size();++i)if(songs[i].name==key)return static_cast<int>(i);
    throw std::invalid_argument("未知唱片曲目："+name);
}
std::uint16_t BlockRegistry::gameEventId(const std::string& name) const {
    auto key=name.find(':')==std::string::npos?"minecraft:"+name:name;
    const auto found=gameEventNames.find(key);
    if(found==gameEventNames.end()) throw std::invalid_argument("未知游戏事件："+name);
    return found->second;
}
Json BlockRegistry::gameEventCatalog() const {
    Json result=Json::array();
    for(const auto& event:gameEvents) result.push_back({{"name",event.name},{"radius",event.radius},{"frequency",event.frequency},{"listenable",event.listenable},{"ignoreSneaking",event.ignoreSneaking}});
    return result;
}
std::uint32_t BlockRegistry::itemId(const std::string& name) const {
    auto key = name.find(':') == std::string::npos ? "minecraft:" + name : name;
    auto found = itemNames.find(key);
    if (found == itemNames.end()) throw std::invalid_argument("Unknown item: " + name);
    return found->second;
}
Json BlockRegistry::itemCatalog() const {
    Json result = Json::array();
    for (const auto& info : items) {
        Json row{{"name", info.name}, {"maxStack", info.maxStack}};
        if(info.bookshelfBook) row["bookshelfBook"]=true;
        if(info.compostChance>=0)row["compostChance"]=info.compostChance;
        if(info.jukeboxSong>=0)row["jukeboxSong"]={{"name",song(info.jukeboxSong).name},{"lengthTicks",song(info.jukeboxSong).lengthTicks},{"comparatorOutput",song(info.jukeboxSong).comparatorOutput}};
        result.push_back(std::move(row));
    }
    return result;
}
StateId BlockRegistry::state(const std::string& name, const Json& props) const {
    std::string key = name.find(':') == std::string::npos ? "minecraft:" + name : name;
    auto found = names.find(key);
    if (found == names.end()) throw std::invalid_argument("未知方块：" + name);
    StateId id = types[found->second].defaultState;
    for (const auto& [prop, v] : props.items()) id = with(id, prop, v.is_string() ? v.get<std::string>() : v.dump());
    return id;
}
StateId BlockRegistry::with(StateId id, const std::string& key, const std::string& value) const {
    const auto& t = type(id);
    auto found = t.properties.find(key);
    if (found == t.properties.end()) throw std::invalid_argument(t.name + " 没有属性 " + key);
    const auto& p = found->second;
    auto vi = std::find(p.values.begin(), p.values.end(), value);
    if (vi == p.values.end()) throw std::invalid_argument("无效属性 " + key + "=" + value);
    auto oldIndex = ((id - t.firstState) / p.stride) % p.values.size();
    auto next = static_cast<std::int64_t>(id) + (std::distance(p.values.begin(), vi) - static_cast<std::int64_t>(oldIndex)) * p.stride;
    if (next < 0 || static_cast<std::size_t>(next) >= states.size() || states[static_cast<std::size_t>(next)].type != states[id].type) throw std::logic_error("方块状态索引不一致");
    return static_cast<StateId>(next);
}
std::string BlockRegistry::property(StateId id, const std::string& key, const std::string& fallback) const {
    const auto& t = type(id);
    auto found = t.properties.find(key);
    if (found == t.properties.end()) return fallback;
    const auto& p = found->second;
    return p.values[((id - t.firstState) / p.stride) % p.values.size()];
}
Json BlockRegistry::describe(StateId id) const {
    const auto& t = type(id); Json props = Json::object();
    for (const auto& [key, p] : t.properties) props[key] = property(id, key);
    return {{"stateId", id}, {"name", t.name}, {"properties", props}};
}
Json BlockRegistry::catalog() const {
    Json result = Json::array();
    for (const auto& t : types) {
        if (t.supportLevel == "unimplemented" || t.device == Device::air || t.device == Device::movingPiston || t.device == Device::pistonHead) continue;
        Json props = Json::object(); for (const auto& [key, p] : t.properties) props[key] = p.values;
        result.push_back({{"name", t.name}, {"defaultState", t.defaultState}, {"defaultProperties", describe(t.defaultState).at("properties")}, {"device", static_cast<unsigned>(t.device)}, {"properties", props}, {"supportLevel", t.supportLevel}});
    }
    return result;
}
}
