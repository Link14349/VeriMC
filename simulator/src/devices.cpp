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

}
// 26.2 Mth.SIN。原版用 (float)Math.sin(i / 10430.378350470453) 构造，这里用等价的
// i * 2π / 65536；两种写法的 double 参数在 9,570 个索引上不同，是否影响 float 结果由
// java26_2SineTable.json 的逐项对照回归证明，不靠推断。
const std::array<float, 65536>& daylightSineTable() {
    static const auto table = [] {
        std::array<float, 65536> values{};
        for (std::size_t i = 0; i < values.size(); ++i)
            values[i] = static_cast<float>(std::sin(static_cast<double>(i) * std::numbers::pi * 2.0 / 65536.0));
        return values;
    }();
    return table;
}
namespace {
// 26.2 Mth.cos takes a double, indexes a 65536-entry float sine table, then
// daylight arithmetic returns to float before Java's round-to-positive-infinity.
float daylightCos(float angle) {
    auto index = static_cast<std::int64_t>(static_cast<double>(angle) * 10430.378350470453 + 16384.0);
    return daylightSineTable()[static_cast<std::uint64_t>(index) & 65535u];
}
}

// 对应 ComparatorBlock.getItemFrame：只有恰好一个朝向匹配的展示框才被采纳，
// 0 个或多个都返回空。读数为 ItemFrame.getAnalogOutput()：空框 0，否则 rotation % 8 + 1。
std::optional<int> Simulator::itemFrameSignal(BlockPos mount, Direction facing) const {
    auto found = runtime.find(mount);
    if (found == runtime.end() || !found->second.values.contains("itemFrames")) return std::nullopt;
    const auto& frames = found->second.values.at("itemFrames");
    const char* name = directionNames[static_cast<unsigned>(facing)];
    std::optional<int> result;
    for (const auto& frame : frames) {
        if (frame.at("facing") != name) continue;
        if (result) return std::nullopt;
        result = frame.value("hasItem", false) ? frame.at("rotation").get<int>() % 8 + 1 : 0;
    }
    return result;
}
void Simulator::stimulateItemFrames(BlockPos pos, const Json& input) {
    if (input.size() != 1) throw std::invalid_argument("物品展示框输入不能与其他刺激字段混用");
    const auto& frames = input.at("itemFrames");
    if (!frames.is_array() || frames.size() > 12) throw std::invalid_argument("物品展示框列表最多 12 个");
    if (world.get(pos) == 0) throw std::invalid_argument("物品展示框必须挂在一个方块上");
    Json stored = Json::array();
    std::vector<Direction> touched;
    for (const auto& frame : frames) {
        if (!frame.is_object()) throw std::invalid_argument("每个物品展示框必须是对象");
        for (const auto& field : frame.items())
            if (field.key() != "facing" && field.key() != "rotation" && field.key() != "hasItem")
                throw std::invalid_argument("物品展示框只接受 facing、rotation 和 hasItem");
        auto facing = parseDirection(frame.at("facing"));
        int rotation = integerInRange(frame, "rotation", 0, 7);
        if (frame.contains("hasItem") && !frame.at("hasItem").is_boolean()) throw std::invalid_argument("hasItem 必须为布尔值");
        stored.push_back({{"facing", directionNames[static_cast<unsigned>(facing)]}, {"rotation", rotation}, {"hasItem", frame.value("hasItem", false)}});
        if (std::find(touched.begin(), touched.end(), facing) == touched.end()) touched.push_back(facing);
    }
    auto& values = runtime[pos].values;
    if (values.contains("itemFrames"))
        for (const auto& frame : values.at("itemFrames")) {
            auto facing = parseDirection(frame.at("facing"));
            if (std::find(touched.begin(), touched.end(), facing) == touched.end()) touched.push_back(facing);
        }
    if (stored.empty()) values.erase("itemFrames"); else values["itemFrames"] = std::move(stored);
    // Inventory and analog output share this record with stimulus fields.
    // Removing the last frame must not remove its host container's contents.
    if (values.empty() && runtime.at(pos).inventory.empty() && runtime.at(pos).output == 0) runtime.erase(pos);
    runtimeChanged(pos, false);
    // 原版 ItemFrame.setItem / setRotation 从展示框自身所在格发出 updateNeighbourForOutputSignal。
    // 原版 ItemFrame 传的 changedBlock 是 Blocks.AIR，不是展示框挂靠的方块。
    for (auto facing : touched) updateComparatorNeighbors(pos.relative(facing), 0);
}

// 原版 Level.updateNeighbourForOutputSignal 对每个命中的比较器发 FullNeighborUpdate，
// 目标状态是**入队那一刻**的比较器状态；来源方块由调用方给出，默认是变化格自己的方块。
void Simulator::updateComparatorNeighbors(BlockPos pos, StateId source) {
    const auto changed = source == noSnapshot ? world.get(pos) : source;
    for (auto direction : horizontal) {
        auto neighbor = pos.relative(direction);
        // 原版这里有 hasChunkAt 守卫：未加载的位置直接跳过。
        if (!chunkLoaded(neighbor)) continue;
        if (at(neighbor).device == Device::comparator) neighborChangedSnapshot(neighbor, world.get(neighbor), changed);
        else if (at(neighbor).conductor) {
            neighbor = neighbor.relative(direction);
            if (!chunkLoaded(neighbor)) continue;
            if (at(neighbor).device == Device::comparator) neighborChangedSnapshot(neighbor, world.get(neighbor), changed);
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
    wakeCartHoppers(pos);
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

bool Simulator::interactDevice(BlockPos pos, std::optional<Direction> playerFacing) {
    auto id = world.get(pos);
    const auto& state = registry[id];
    const auto& name = registry.type(id).name;
    if (state.device == Device::door || state.device == Device::trapdoor || state.device == Device::fenceGate) {
        if (name == "minecraft:iron_door" || name == "minecraft:iron_trapdoor") throw std::invalid_argument("铁门和铁活板门需要红石信号驱动");
        const bool opening = registry.property(id, "open") != "true";
        auto next = registry.withBool(id, "open", opening);
        // 原版 FenceGateBlock：从背面打开时会把朝向翻到玩家的朝向。没有给玩家朝向时不翻转。
        if (opening && state.device == Device::fenceGate && playerFacing && state.facing == opposite(*playerFacing))
            next = registry.withBool(registry.with(id, "facing", std::string(directionNames[static_cast<unsigned>(*playerFacing)])), "open", true);
        setBlock(pos, next, state.device == Device::trapdoor ? 2 : 10);
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
    // 容器实体声明与这一格是什么方块无关，空气格也接受，所以放在器件分派之前。
    if (stimulus.contains("containerEntities")) { stimulateContainerEntities(pos, stimulus); return true; }
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
    if (state.device == Device::hopper && stimulus.contains("groundItems")) { stimulateGroundItems(pos, stimulus); return true; }
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
    if (data.values.contains("containerEntities")) validateContainerEntities(data.values.at("containerEntities"));
    if (data.values.contains("groundItems")) {
        if (device != Device::hopper) throw std::invalid_argument("只有漏斗可以持有掉落物输入");
        const auto& items = data.values.at("groundItems");
        if (!items.is_array() || items.size() > 32) throw std::invalid_argument("无效掉落物列表");
        for (const auto& entry : items) {
            if (!entry.is_object() || entry.size() != 5) throw std::invalid_argument("无效掉落物记录");
            const auto item = registry.itemId(entry.at("item"));
            integerInRange(entry, "count", 1, registry.item(item).maxStack);
            for (const char* axis : {"x", "y", "z"}) {
                if (!entry.at(axis).is_number()) throw std::invalid_argument("无效掉落物坐标");
                const double value = entry.at(axis).get<double>();
                if (!std::isfinite(value) || value < -2.0 || value > 4.0) throw std::invalid_argument("无效掉落物坐标");
            }
        }
    }
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
    if (data.values.contains("itemFrames")) {
        const auto& frames = data.values.at("itemFrames");
        if (!frames.is_array() || frames.empty() || frames.size() > 12) throw std::invalid_argument("无效物品展示框列表");
        for (const auto& frame : frames) {
            if (!frame.is_object() || frame.size() != 3) throw std::invalid_argument("无效物品展示框记录");
            parseDirection(frame.at("facing"));
            integerInRange(frame, "rotation", 0, 7);
            if (!frame.at("hasItem").is_boolean()) throw std::invalid_argument("无效物品展示框物品标记");
        }
    }
}
}
