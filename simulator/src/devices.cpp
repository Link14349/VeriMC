#include "simulator/simulator.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace simulator {
namespace {
int integerInRange(const Json& values, const char* key, int fallback, int maximum) {
    if (!values.contains(key)) return fallback;
    const auto& value = values.at(key);
    if (!value.is_number_integer() || value.get<std::int64_t>() < 0 || value.get<std::int64_t>() > maximum)
        throw std::invalid_argument(std::string(key) + " 必须是 0–" + std::to_string(maximum) + " 的整数");
    return value.get<int>();
}

// 26.2 Mth.cos takes a double, indexes a 65536-entry float sine table, then
// daylight arithmetic returns to float before Java's round-to-positive-infinity.
float daylightCos(float angle) {
    static const auto table = [] {
        std::array<float, 65536> values{};
        for (std::size_t i = 0; i < values.size(); ++i)
            values[i] = static_cast<float>(std::sin(static_cast<double>(i) * std::numbers::pi * 2.0 / 65536.0));
        return values;
    }();
    auto index = static_cast<std::int64_t>(static_cast<double>(angle) * 10430.378350470453 + 16384.0);
    return table[static_cast<std::uint64_t>(index) & 65535u];
}
}

void Simulator::updateComparatorNeighbors(BlockPos pos) {
    for (auto direction : horizontal) {
        auto neighbor = pos.relative(direction);
        if (at(neighbor).device == Device::comparator) neighborChanged(neighbor, world.get(pos));
        else if (at(neighbor).conductor) {
            neighbor = neighbor.relative(direction);
            if (at(neighbor).device == Device::comparator) neighborChanged(neighbor, world.get(pos));
        }
    }
}

void Simulator::runtimeChanged(BlockPos pos, bool notifyComparators) {
    ++revision;
    ++sequence;
    changes[pos] = world.get(pos);
    sampleAffected(pos);
    if (notifyComparators) updateComparatorNeighbors(pos);
    if (!hoppers.empty()) wakeHoppers(pos);
}

void Simulator::updatePressurePlate(BlockPos pos) {
    auto id = world.get(pos);
    const auto& state = registry[id];
    const auto& values = runtime[pos].values;
    const auto& name = registry.type(id).name;
    bool stone = name == "minecraft:stone_pressure_plate" || name == "minecraft:polished_blackstone_pressure_plate";
    int count = values.value(stone ? "livingEntities" : "entities", 0);
    int power = count > 0 ? 15 : 0;
    if (state.device == Device::weightedPlate) {
        int maximum = name == "minecraft:light_weighted_pressure_plate" ? 15 : 150;
        power = static_cast<int>(std::ceil(static_cast<float>(std::min(count, maximum)) / static_cast<float>(maximum) * 15.0F));
    }
    int oldPower = state.device == Device::weightedPlate ? state.power : state.powered ? 15 : 0;
    if (oldPower != power) {
        auto next = state.device == Device::weightedPlate ? registry.with(id, "power", power) : registry.withBool(id, "powered", power > 0);
        setBlock(pos, next, 2);
        notifyAttached(pos, Direction::up);
    }
    if((oldPower==0)!=(power==0))emitGameEvent(power?"block_activate":"block_deactivate",pos);
    // Occupancy is supplied after spectator/trigger filtering. While powered,
    // both removal and additional arrivals are observed only at the next poll.
    if (power > 0) schedule(pos, state.device == Device::weightedPlate ? 10 : 20);
}

void Simulator::updateButton(BlockPos pos) {
    auto id = world.get(pos); const auto& state = registry[id];
    if (state.device != Device::button) return;
    const auto& name = registry.type(id).name;
    const bool allowsArrows = name != "minecraft:stone_button" && name != "minecraft:polished_blackstone_button";
    auto found = runtime.find(pos);
    int arrows = found == runtime.end() ? 0 : found->second.values.value("arrows", 0);
    int pressedArrows = found == runtime.end() ? 0 : found->second.values.value("pressedArrows", arrows);
    const bool pressed = allowsArrows && (state.powered ? pressedArrows : arrows) > 0;
    if (pressed != state.powered) {
        setBlock(pos, registry.withBool(id, "powered", pressed));
        notifyAttached(pos, state.connectedDirection);
        emitGameEvent(pressed?"block_activate":"block_deactivate",pos);
    }
    if (pressed) schedule(pos, 30);
    // setBlock queues contact with the released shape, including shallow arrows
    // and manual state edits. Contact runs after the current block-tick phase.
}

void Simulator::buttonContact(BlockPos pos) {
    if (at(pos).device == Device::button && !at(pos).powered) updateButton(pos);
}

void Simulator::updateDaylight(BlockPos pos) {
    auto id = world.get(pos);
    auto found = runtime.find(pos);
    const Json empty = Json::object();
    const auto& values = found == runtime.end() ? empty : found->second.values;
    int power = values.value("skyBrightness", 0);
    if (registry.property(id, "inverted") == "true") power = 15 - power;
    else if (power > 0) {
        float angle = values.value("sunAngle", 0.0F) * static_cast<float>(std::numbers::pi / 180.0);
        float offset = angle < static_cast<float>(std::numbers::pi) ? 0.0F : static_cast<float>(std::numbers::pi * 2.0);
        angle += (offset - angle) * 0.2F;
        power = static_cast<int>(std::floor(static_cast<float>(power) * daylightCos(angle) + 0.5F));
    }
    setBlock(pos, registry.with(id, "power", std::clamp(power, 0, 15)));
    // Constant laboratory sky input cannot change on subsequent 20-gt polls.
    // A new stimulus invalidates this shortcut and schedules the next boundary.
}

bool Simulator::interactDevice(BlockPos pos) {
    auto id = world.get(pos);
    const auto& state = registry[id];
    const auto& name = registry.type(id).name;
    if (state.device == Device::door || state.device == Device::trapdoor || state.device == Device::fenceGate) {
        if (name == "minecraft:iron_door" || name == "minecraft:iron_trapdoor") throw std::invalid_argument("铁门和铁活板门需要红石信号驱动");
        setBlock(pos, registry.withBool(id, "open", registry.property(id, "open") != "true"), state.device == Device::trapdoor ? 2 : 10);
        (void)worldRandom.nextFloat();emitGameEvent(registry.property(id,"open")=="true"?"block_close":"block_open",pos);
        return true;
    }
    if (state.device == Device::container) {
        int viewers = runtime.contains(pos) ? runtime.at(pos).values.value("viewers", 0) : 0;
        setViewers(pos, viewers > 0 ? 0 : 1);
        return true;
    }
    if (state.device == Device::daylight) {
        auto inverted = registry.withBool(id, "inverted", registry.property(id, "inverted") != "true");
        setBlock(pos, inverted, 2);
        // 原版在写入新状态后、刷新强度前发出 BLOCK_CHANGE，上下文携带新状态。
        emitGameEvent("block_change", pos, {false, false, false, inverted});
        updateDaylight(pos);
        return true;
    }
    if (state.device == Device::lectern) {
        auto it = runtime.find(pos);
        int pages = it == runtime.end() ? 0 : it->second.values.value("pages", 0);
        int page = it == runtime.end() ? 0 : it->second.values.value("page", 0);
        if (pages == 0) throw std::invalid_argument("请先在环境输入中放入书本");
        return stimulateDevice(pos, {{"page", (page + 1) % pages}});
    }
    if (registry.type(id).className == "CopperGolemStatueBlock" || registry.type(id).className == "WeatheringCopperGolemStatueBlock") {
        const auto& poses = registry.type(id).properties.at("copper_golem_pose").values;
        auto current = std::find(poses.begin(), poses.end(), registry.property(id, "copper_golem_pose"));
        auto next = (static_cast<std::size_t>(current - poses.begin()) + 1) % poses.size();
        setBlock(pos, registry.with(id, "copper_golem_pose", poses[next]));
        return true;
    }
    return false;
}

bool Simulator::stimulateDevice(BlockPos pos, const Json& stimulus) {
    if (!stimulus.is_object()) throw std::invalid_argument("环境输入必须是对象");
    auto id = world.get(pos);
    const auto& state = registry[id];
    if (state.device == Device::button) {
        if (!stimulus.contains("arrows") || stimulus.size() > (stimulus.contains("pressedArrows") ? 2u : 1u)) throw std::invalid_argument("按钮输入需要 arrows，可选 pressedArrows");
        int arrows = integerInRange(stimulus, "arrows", 0, 1000000);
        int pressedArrows = integerInRange(stimulus, "pressedArrows", arrows, 1000000);
        if (pressedArrows > arrows) throw std::invalid_argument("按下形状内的箭数不能超过弹起形状内的箭数");
        runtime[pos].values = {{"arrows", arrows}, {"pressedArrows", pressedArrows}};
        runtimeChanged(pos, false);
        if (arrows == 0) scheduledKeys.erase({pos, state.type, 3, 0});
        else buttonContact(pos);
        return true;
    }
    if (state.device == Device::target) {
        for (const auto& field : stimulus.items()) if (field.key() != "hit" && field.key() != "face" && field.key() != "arrow")
            throw std::invalid_argument("标靶输入需要命中面 face 和格内坐标 hit，不能直接指定信号强度");
        const auto direction = parseDirection(stimulus.at("face"));
        const auto& hit = stimulus.at("hit");
        if (!hit.is_array() || hit.size() != 3) throw std::invalid_argument("命中坐标需要三个 0–1 数值");
        if (stimulus.contains("arrow") && !stimulus.at("arrow").is_boolean()) throw std::invalid_argument("arrow 必须为布尔值");
        std::array<double,3> distances{};
        const std::array<int,3> coordinates{pos.x,pos.y,pos.z};
        for (std::size_t i=0;i<3;++i) {
            if (!hit[i].is_number() || !std::isfinite(hit[i].get<double>()) || hit[i]<0 || hit[i]>1) throw std::invalid_argument("命中坐标需要三个 0–1 数值");
            // Match vanilla's fraction of world-space doubles, including
            // negative positions and rounding near signal thresholds.
            const double absolute=coordinates[i]+hit[i].get<double>();
            distances[i]=std::abs(absolute-std::floor(absolute)-.5);
        }
        const auto normal=axis(direction)==0?1u:axis(direction)==1?2u:0u;
        double distance=0;
        for(std::size_t i=0;i<3;++i) if(i!=normal) distance=std::max(distance,distances[i]);
        const int strength=std::max(1,static_cast<int>(std::ceil(15.0*std::clamp((.5-distance)/.5,0.0,1.0))));
        if (!hasScheduled(pos)) { setBlock(pos,registry.with(id,"power",strength));schedule(pos,stimulus.value("arrow",true)?20:8); }
        return true;
    }
    if (state.device == Device::detectorRail) { setCartInput(pos, stimulus); return true; }
    if(state.device==Device::composter) {
        if(stimulus.size()!=1 || !stimulus.contains("compostItem") || !stimulus.at("compostItem").is_string())throw std::invalid_argument("堆肥输入需要 compostItem 物品 ID");
        addCompost(pos,registry.itemId(stimulus.at("compostItem")));return true;
    }
    if (state.device == Device::tripwire) {
        if (stimulus.contains("shear")) {
            if (stimulus.size() != 1 || stimulus.at("shear") != true) throw std::invalid_argument("剪断输入必须为 shear: true");
            setBlock(pos, registry.withBool(id, "disarmed", true), 260);
            emitGameEvent("shear",pos);
            emitGameEvent("block_destroy",pos,{false,false,false,id});
            setBlock(pos, 0);
        } else {
            if (stimulus.size() != 1 || !stimulus.contains("entities")) throw std::invalid_argument("绊线输入需要 entities 实体数量");
            int count = integerInRange(stimulus, "entities", 0, 1000000);
            runtime[pos].values = {{"entities", count}};
            runtimeChanged(pos, false);
            if (count > 0) tripwireContact(pos);
            else scheduledKeys.erase({pos, state.type, 3, 0});
        }
        return true;
    }
    if (state.device == Device::jukebox || state.device == Device::container || state.device == Device::hopper || state.device == Device::dropper || isBookshelf(id) || isDecoratedPot(id)) {
        if (stimulus.contains("inventory")) setInventory(pos, stimulus.at("inventory"));
        else if (state.device == Device::container && stimulus.contains("viewers")) setViewers(pos, integerInRange(stimulus, "viewers", 0, 1000000));
        else throw std::invalid_argument("Container input requires inventory or viewers");
        return true;
    }
    if (state.device == Device::pressurePlate || state.device == Device::weightedPlate) {
        int count = integerInRange(stimulus, "entities", 0, 1000000);
        int living = integerInRange(stimulus, "livingEntities", count, 1000000);
        if (living > count) throw std::invalid_argument("生物数量不能超过总实体数量");
        runtime[pos].values = {{"entities", count}, {"livingEntities", living}};
        runtimeChanged(pos);
        if (!state.powered && state.power == 0) updatePressurePlate(pos);
        return true;
    }
    if (state.device == Device::lightningRod) {
        setBlock(pos, registry.withBool(id, "powered", true));
        updateNeighbors(pos.relative(opposite(state.facing)), -1, id);
        schedule(pos, 8);
        return true;
    }
    if (state.device == Device::daylight) {
        int sky = integerInRange(stimulus, "skyBrightness", 0, 15);
        float angle = stimulus.value("sunAngle", 0.0F);
        if (!std::isfinite(angle) || angle < 0 || angle > 360) throw std::invalid_argument("太阳角度必须在 0–360 度之间");
        runtime[pos].values = {{"skyBrightness", sky}, {"sunAngle", angle}};
        runtimeChanged(pos);
        schedulePhase(pos, currentTick + 20 - currentTick % 20, 2, 0);
        return true;
    }
    if (state.device == Device::lectern) {
        Json values = runtime.contains(pos) ? runtime.at(pos).values : Json::object();
        if (stimulus.contains("pages")) {
            int pages = integerInRange(stimulus, "pages", 0, 100);
            values = {{"pages", pages}, {"page", 0}};
            runtime[pos].values = values;
            setBlock(pos, registry.withBool(registry.withBool(id, "has_book", pages > 0), "powered", false));
            updateNeighbors(pos.relative(Direction::down), -1, id);
            runtimeChanged(pos);
            return true;
        }
        int pages = values.value("pages", 0);
        if (pages == 0) throw std::invalid_argument("讲台上没有书本");
        int page = integerInRange(stimulus, "page", 0, pages - 1);
        if (page != values.value("page", 0)) {
            values["page"] = page;
            runtime[pos].values = values;
            runtimeChanged(pos);
            setBlock(pos, registry.withBool(id, "powered", true));
            updateNeighbors(pos.relative(Direction::down), -1, id);
            schedule(pos, 2);
        }
        return true;
    }
    return false;
}

void Simulator::validateRuntime(BlockPos pos) const {
    const auto& data = runtime.at(pos);
    if (!data.values.is_object() || data.output < 0 || data.output > 15) throw std::invalid_argument("无效器件内部状态");
    auto device = at(pos).device;
    validateNoteRuntime(pos);
    if(device==Device::bell) {
        for(const auto* field:{"ringCount","lastRingTick","bellWakeAt","bellGeneration"})if(data.values.contains(field) && (!data.values.at(field).is_number_integer() || data.values.at(field)<0))throw std::invalid_argument("无效钟运行字段");
        if(data.values.contains("ringing") && !data.values.at("ringing").is_boolean())throw std::invalid_argument("无效钟摆动状态");
        if(data.values.contains("ringDirection") && axis(parseDirection(data.values.at("ringDirection")))==0)throw std::invalid_argument("钟的敲击方向必须水平");
        if(data.values.contains("bellWakeAt")!=data.values.contains("bellGeneration"))throw std::invalid_argument("钟的结束时间与事件标记不一致");
    }
    if(isSensor(device)) integerInRange(data.values,"lastVibrationFrequency",0,15);
    if(isBookshelf(world.get(pos)) && data.values.contains("lastInteractedSlot")) {
        const auto& last=data.values.at("lastInteractedSlot");
        if(!last.is_number_integer() || last < -1 || last > 5) throw std::invalid_argument("无效雕纹书架最后操作槽位");
    }
    if (device == Device::detectorRail) normalizeCarts(data.values.value("carts", Json::array()));
    if (device == Device::tripwire) integerInRange(data.values, "entities", 0, 1000000);
    if (device == Device::button) {
        int arrows = integerInRange(data.values, "arrows", 0, 1000000);
        if (integerInRange(data.values, "pressedArrows", arrows, 1000000) > arrows) throw std::invalid_argument("无效按钮箭矢数量");
    }
    if (inventorySize(world.get(pos))) integerInRange(data.values, "viewers", 0, 1000000);
    if (device == Device::pressurePlate || device == Device::weightedPlate) {
        int entities = integerInRange(data.values, "entities", 0, 1000000);
        if (integerInRange(data.values, "livingEntities", 0, 1000000) > entities) throw std::invalid_argument("无效压力板实体数量");
    }
    if (device == Device::daylight) {
        integerInRange(data.values, "skyBrightness", 0, 15);
        float angle = data.values.value("sunAngle", 0.0F);
        if (!std::isfinite(angle) || angle < 0 || angle > 360) throw std::invalid_argument("无效太阳角度");
    }
    if (device == Device::lectern) {
        int pages = integerInRange(data.values, "pages", 0, 100);
        integerInRange(data.values, "page", 0, std::max(0, pages - 1));
    }
}
}
