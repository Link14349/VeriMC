#include "simulator/simulator.hpp"
#include <algorithm>
#include <cmath>
#include <set>

namespace simulator {
namespace {
Direction turn(Direction direction, int steps) {
    auto found = std::find(horizontal.begin(), horizontal.end(), direction);
    auto index = static_cast<int>(found - horizontal.begin());
    return horizontal[static_cast<std::size_t>((index + steps + 4) % 4)];
}
}

std::size_t Simulator::inventorySize(StateId id) const {
    if(isBookshelf(id)) return 6;
    if(isDecoratedPot(id) || registry[id].device==Device::jukebox) return 1;
    switch (registry[id].device) {
    case Device::container: return 27;
    case Device::hopper: return 5;
    case Device::dropper: case Device::dispenser: case Device::crafter: return 9;
    case Device::furnace: return 3;
    default: return 0;
    }
}
bool Simulator::isBookshelf(StateId state) const {
    return registry[state].device==Device::analog && registry.type(state).className=="ChiseledBookShelfBlock";
}
bool Simulator::isDecoratedPot(StateId state) const {
    return registry[state].device==Device::analog && registry.type(state).className=="DecoratedPotBlock";
}
bool Simulator::canInsertStack(const InventorySlot& slot, ItemStack stack) const {
    // 矿车容器没有重写 canPlaceItem/canTakeItem，也不是 WorldlyContainer：任何物品都能进出。
    if(slot.entity>=0)return true;
    if(at(slot.pos).device==Device::jukebox)return registry.item(stack.item).jukeboxSong>=0 && !stackAt(slot).count;
    if(isBookshelf(world.get(slot.pos))) return registry.item(stack.item).bookshelfBook && !stackAt(slot).count;
    return true;
}
bool Simulator::canExtractStack(const InventorySlot& slot, BlockPos into) const {
    if(slot.entity>=0)return true;
    if(at(slot.pos).device==Device::jukebox){for(const auto& target:containerSlots(into))if(!stackAt(target).count)return true;return false;}
    if(!isBookshelf(world.get(slot.pos))) return true;
    const auto source=stackAt(slot);
    for(const auto& target:containerSlots(into)) {
        const auto stack=stackAt(target);
        if(!stack.count || (source.item==stack.item && source.count+stack.count<=registry.item(stack.item).maxStack)) return true;
    }
    return false;
}
void Simulator::updateBookshelfSlot(const InventorySlot& slot) {
    auto id=world.get(slot.pos);
    runtime[slot.pos].values["lastInteractedSlot"]=slot.index;
    for(std::size_t i=0;i<6;++i) id=registry.withBool(id,"slot_"+std::to_string(i)+"_occupied",stackAt({slot.pos,i}).count>0);
    setBlock(slot.pos,id);
    emitGameEvent("block_change",slot.pos,{false,false,false,id});
    runtimeChanged(slot.pos,false);
}

Direction Simulator::chestConnection(StateId state) const {
    return turn(registry[state].facing, registry.property(state, "type") == "left" ? 1 : -1);
}
bool Simulator::isCopperChest(StateId state) const {
    const auto& name=registry.type(state).className;
    return name=="CopperChestBlock" || name=="WeatheringCopperChestBlock";
}
bool Simulator::chestsConnect(StateId first, StateId second) const {
    return isCopperChest(first) ? isCopperChest(second) : registry[first].type==registry[second].type;
}

StateId Simulator::placedChest(BlockPos pos, StateId state) const {
    if (!registry.has(state, "type") || registry.property(state, "type") != "single") return state;
    for (int step : {1, -1}) {
        auto adjacent = world.get(pos.relative(turn(registry[state].facing, step)));
        if (chestsConnect(state,adjacent) && registry[adjacent].facing == registry[state].facing && registry.property(adjacent, "type") == "single") {
            auto paired=registry.with(state, "type", std::string(step == 1 ? "left" : "right"));
            if (!isCopperChest(state)) return paired;
            auto name=registry.type(state).name, other=registry.type(adjacent).name;
            if (name.starts_with("minecraft:waxed_")!=other.starts_with("minecraft:waxed_")) {
                if(name.starts_with("minecraft:waxed_")) name.erase(10,6);
                if(other.starts_with("minecraft:waxed_")) other.erase(10,6);
            }
            auto age=[](const std::string& value) {return value.find("oxidized")!=std::string::npos?3:value.find("weathered")!=std::string::npos?2:value.find("exposed")!=std::string::npos?1:0;};
            return registry.state(age(name)<=age(other)?name:other,registry.describe(paired).at("properties"));
        }
    }
    return state;
}

void Simulator::updateChestShape(const Update& update) {
    auto id = world.get(update.pos), neighbor = update.neighborState;
    auto type = registry.property(id, "type");
    auto next=id;
    if (chestsConnect(id,neighbor) && axis(update.direction) != 0) {
        auto otherType = registry.property(neighbor, "type");
        if (type == "single" && otherType != "single" && registry[id].facing == registry[neighbor].facing && chestConnection(neighbor) == opposite(update.direction))
            next=registry.with(id, "type", std::string(otherType == "left" ? "right" : "left"));
    } else if (chestConnection(id) == update.direction) {
        next=registry.with(id, "type", std::string("single"));
    }
    if(isCopperChest(id) && isCopperChest(neighbor) && registry.property(next,"type")!="single" && chestConnection(next)==update.direction)
        next=registry.state(registry.type(neighbor).name,registry.describe(next).at("properties"));
    setBlock(update.pos,next,update.flags,update.depth);
}

std::vector<Simulator::InventorySlot> Simulator::containerSlots(BlockPos pos, bool ignoreBlockage) const {
    auto id = world.get(pos);
    std::vector<BlockPos> containers{pos};
    const auto size = inventorySize(id);
    if (!size) return {};
    bool chest = registry[id].device == Device::container && registry.has(id, "type");
    if (chest) {
        if (!ignoreBlockage && at(pos.relative(Direction::up)).conductor) return {};
        auto type = registry.property(id, "type");
        if (type != "single") {
            auto partner = pos.relative(chestConnection(id));
            auto partnerId = world.get(partner);
            auto partnerType = registry.property(partnerId, "type");
            if (registry[partnerId].type == registry[id].type && registry[partnerId].facing == registry[id].facing && partnerType != "single" && partnerType != type) {
                if (!ignoreBlockage && at(partner.relative(Direction::up)).conductor) return {};
                if (type == "right") containers.push_back(partner);
                else containers.insert(containers.begin(), partner);
            }
        }
    }
    std::vector<InventorySlot> result;
    result.reserve(size * containers.size());
    for (auto container : containers) for (std::size_t i = 0; i < size; ++i) result.push_back({container, i});
    return result;
}

namespace {
constexpr std::size_t chestMinecartSlots = 27, hopperMinecartSlots = 5;
}
// MinecartChest.getContainerSize()=27、MinecartHopper.getContainerSize()=5。
// 当前协议只支持这两种矿车。原版选择器也接受运输船/运输竹筏，尚未建模。
std::size_t Simulator::containerEntitySize(const std::string& type) {
    if (type == "chest_minecart") return chestMinecartSlots;
    if (type == "hopper_minecart") return hopperMinecartSlots;
    throw std::invalid_argument("容器实体只支持运输矿车与漏斗矿车");
}
// 器件层实体容器输入：整体替换这一格声明的容器实体集合，空数组表示全部移除。
// 不建模矿车的运动、碰撞与拾取，只声明「哪一格里有哪些容器实体、各装了什么」。
// 允许声明在空气格上：矿车通常停在空气或铁轨那一格里。
void Simulator::stimulateContainerEntities(BlockPos pos, const Json& input) {
    if (input.size() != 1) throw std::invalid_argument("容器实体输入不能与其他刺激字段混用");
    const auto& entities = input.at("containerEntities");
    if (!entities.is_array() || entities.size() > containerEntityLimit) throw std::invalid_argument("容器实体最多 16 个");
    Json stored = Json::array();
    for (const auto& entry : entities) {
        if (!entry.is_object()) throw std::invalid_argument("每个容器实体必须是对象");
        for (const auto& field : entry.items())
            if (field.key() != "type" && field.key() != "inventory") throw std::invalid_argument("容器实体只接受 type 与 inventory");
        if (!entry.contains("type") || !entry.at("type").is_string()) throw std::invalid_argument("容器实体需要字符串 type");
        auto type = entry.at("type").get<std::string>();
        Json row{{"type", type}, {"inventory", Json::array()}};
        for (const auto& [slot, stack] : parseInventory(entry.value("inventory", Json::array()), containerEntitySize(type)))
            if (stack.count) row["inventory"].push_back({{"slot", slot}, {"item", registry.item(stack.item).name}, {"count", stack.count}});
        stored.push_back(std::move(row));
    }
    auto& values = runtime[pos].values;
    if (stored.empty()) values.erase("containerEntities"); else values["containerEntities"] = std::move(stored);
    // 空气格上的空声明不留下运行时记录，否则工程里会多出一行没有内容的器件数据。
    const auto& data = runtime.at(pos);
    if (data.values.empty() && data.inventory.empty() && data.output == 0 && world.get(pos) == 0) runtime.erase(pos);
    // 矿车出现或消失不是方块实体变化，不通知比较器；runtimeChanged 仍会唤醒
    // 这一格下方的漏斗和朝这一格的漏斗，正是可能读到它的两条路径。
    runtimeChanged(pos, false);
}
void Simulator::validateContainerEntities(const Json& entities) const {
    if (!entities.is_array() || entities.empty() || entities.size() > containerEntityLimit) throw std::invalid_argument("无效容器实体列表");
    for (const auto& entry : entities) {
        if (!entry.is_object() || entry.size() != 2 || !entry.contains("type") || !entry.contains("inventory") || !entry.at("type").is_string())
            throw std::invalid_argument("无效容器实体记录");
        const auto& inventory = entry.at("inventory");
        parseInventory(inventory, containerEntitySize(entry.at("type").get<std::string>()));
        if (!inventory.is_array()) throw std::invalid_argument("无效容器实体库存");
        for (const auto& item : inventory)
            if (!item.is_object() || item.size() != 3 || item.at("count").get<std::int64_t>() < 1) throw std::invalid_argument("无效容器实体库存");
    }
}
std::size_t Simulator::containerEntityCount(BlockPos pos) const {
    auto found = runtime.find(pos);
    if (found == runtime.end()) return 0;
    auto entities = found->second.values.find("containerEntities");
    return entities == found->second.values.end() ? 0 : entities->size();
}
std::vector<Simulator::InventorySlot> Simulator::entityContainerSlots(BlockPos pos, int entity) const {
    if (entity < 0 || static_cast<std::size_t>(entity) >= containerEntityCount(pos)) return {};
    const auto& row = runtime.at(pos).values.at("containerEntities").at(static_cast<std::size_t>(entity));
    const std::size_t size = containerEntitySize(row.at("type").get<std::string>());
    std::vector<InventorySlot> result;
    result.reserve(size);
    for (std::size_t i = 0; i < size; ++i) result.push_back({pos, i, entity});
    return result;
}
// 原版 getEntityContainer：候选非空时用 level.random.nextInt(size) 选一个，**总是**消耗一次随机数。
std::optional<int> Simulator::chooseContainerEntity(BlockPos pos) {
    const auto count = containerEntityCount(pos);
    if (!count) return std::nullopt;
    return static_cast<int>(worldRandom.nextInt(static_cast<int>(count)));
}
Json Simulator::containerEntitiesJson(BlockPos pos) const {
    auto found = runtime.find(pos);
    if (found == runtime.end() || !found->second.values.contains("containerEntities")) return Json::array();
    return found->second.values.at("containerEntities");
}
ItemStack Simulator::stackAt(const InventorySlot& slot) const {
    if (slot.entity >= 0) {
        const auto& inventory = runtime.at(slot.pos).values.at("containerEntities").at(static_cast<std::size_t>(slot.entity)).at("inventory");
        for (const auto& row : inventory)
            if (row.at("slot").get<std::size_t>() == slot.index)
                return {registry.itemId(row.at("item")), static_cast<std::uint16_t>(row.at("count").get<int>())};
        return {};
    }
    auto data = runtime.find(slot.pos);
    if (data == runtime.end() || slot.index >= data->second.inventory.size()) return {};
    return data->second.inventory[slot.index];
}

int Simulator::containerAnalog(BlockPos pos) const {
    const auto slots = containerSlots(pos, false);
    if (slots.empty()) return 0;
    // Keep the original slot order and float accumulation; aggregating halves
    // separately can round differently at comparator thresholds.
    float fullness = 0;
    for (const auto& slot : slots) {
        const auto stack = stackAt(slot);
        if (stack.count) fullness += static_cast<float>(stack.count) / static_cast<float>(registry.item(stack.item).maxStack);
    }
    fullness /= static_cast<float>(slots.size());
    return static_cast<int>(std::floor(fullness * 14.0F)) + (fullness > 0 ? 1 : 0);
}

Json Simulator::inventoryJson(BlockPos pos, bool combined) const {
    auto slots = combined ? containerSlots(pos) : std::vector<InventorySlot>{};
    if (!combined) for (std::size_t i = 0; i < inventorySize(world.get(pos)); ++i) slots.push_back({pos, i});
    Json result = Json::array();
    for (std::size_t i = 0; i < slots.size(); ++i) {
        auto stack = stackAt(slots[i]);
        if (stack.count) result.push_back({{"slot", i}, {"item", registry.item(stack.item).name}, {"count", stack.count}});
    }
    return result;
}

std::vector<std::pair<std::size_t, ItemStack>> Simulator::parseInventory(const Json& values, std::size_t size, bool preserveOrder) const {
    if (!values.is_array() || values.size() > size) throw std::invalid_argument("库存修改必须是有效槽位数组");
    std::vector<std::pair<std::size_t, ItemStack>> edits;
    std::set<std::size_t> edited;
    for (const auto& value : values) {
        if (!value.at("slot").is_number_integer() || !value.at("count").is_number_integer() || value.at("slot").get<std::int64_t>() < 0 || value.at("slot").get<std::int64_t>() >= static_cast<std::int64_t>(size) || value.at("count").get<std::int64_t>() < 0 || value.at("count").get<std::int64_t>() > 99) throw std::invalid_argument("库存槽位和数量必须是有效整数");
        auto slot = value.at("slot").get<int>(), count = value.at("count").get<int>();
        if (slot < 0 || static_cast<std::size_t>(slot) >= size || !edited.insert(static_cast<std::size_t>(slot)).second) throw std::invalid_argument("库存槽位越界或重复");
        if (value.contains("components") && !value.at("components").empty()) throw std::invalid_argument("自定义物品组件尚未实现，请使用默认物品");
        std::uint32_t item = count == 0 ? 0 : registry.itemId(value.at("item"));
        if (count < 0 || count > registry.item(item).maxStack || (count > 0 && registry.item(item).name == "minecraft:air")) throw std::invalid_argument("物品数量超出该物品的堆叠上限");
        edits.push_back({static_cast<std::size_t>(slot), {item, static_cast<std::uint16_t>(count)}});
    }
    if(!preserveOrder) std::sort(edits.begin(), edits.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    return edits;
}

void Simulator::setInventory(BlockPos pos, const Json& values, bool combined, bool notify) {
    auto slots = combined ? containerSlots(pos) : std::vector<InventorySlot>{};
    if (!combined) for (std::size_t i = 0; i < inventorySize(world.get(pos)); ++i) slots.push_back({pos, i});
    if (slots.empty()) throw std::invalid_argument("这个器件没有库存槽位");
    const bool bookshelf=isBookshelf(world.get(pos)),jukebox=at(pos).device==Device::jukebox;
    const auto edits=parseInventory(values,slots.size(),bookshelf);
    if(bookshelf) for(const auto& [index,stack]:edits) {
        (void)index;
        if(stack.count && (stack.count>1 || !registry.item(stack.item).bookshelfBook)) throw std::invalid_argument("雕纹书架每槽仅接受一本原版书籍");
    }
    if(jukebox)for(const auto& [index,stack]:edits){(void)index;if(stack.count && (stack.count!=1 || registry.item(stack.item).jukeboxSong<0))throw std::invalid_argument("唱片机只接受一张可播放的默认唱片");}
    for (const auto& [index, stack] : edits) {
        const auto& slot = slots[index];
        if(notify && (bookshelf || jukebox)) {writeStack(slot,stack);continue;}
        auto& inventory = runtime[slot.pos].inventory;
        inventory.resize(inventorySize(world.get(slot.pos)));
        inventory[slot.index] = stack;
        // 原版 BaseContainerBlockEntity.setItem 自己就调用 setChanged，因此每写一格都会通知
        // 一次比较器，而且只通知被写入的那一半箱子。漏斗重写了 setItem 且不调用 setChanged，
        // 饰纹陶罐走 ContainerSingleItem.setItem，同样不调用，两者只有末尾那一次通知。
        const auto written = world.get(slot.pos);
        if (notify && at(slot.pos).device != Device::hopper && !isDecoratedPot(written)) runtimeChanged(slot.pos);
    }
    if (notify) {
        // A combined chest's Container.setChanged notifies both physical halves.
        std::set<BlockPos> visited;
        for (const auto& slot : slots) if (visited.insert(slot.pos).second) runtimeChanged(slot.pos);
    }
}

void Simulator::setViewers(BlockPos pos, int viewers) {
    if (viewers < 0 || viewers > 1000000) throw std::invalid_argument("查看人数必须在 0–1000000 之间");
    auto slots = containerSlots(pos);
    if (viewers > viewerCount(pos) && containerSlots(pos, false).empty()) throw std::invalid_argument("箱盖被上方实体方块挡住");
    std::set<BlockPos> visited;
    for (const auto& slot : slots) if (visited.insert(slot.pos).second) {
        auto id = world.get(slot.pos);
        const auto previous=viewerCount(slot.pos);
        runtime[slot.pos].values["viewers"] = viewers;
        if((previous==0)!=(viewers==0)) {
            if(registry.property(id,"type")!="left") (void)worldRandom.nextFloat();
            if (registry.type(id).className == "BarrelBlock") setBlock(slot.pos, registry.withBool(id, "open", viewers > 0));
            emitGameEvent(viewers?"container_open":"container_close",slot.pos);
        }
        runtimeChanged(slot.pos);
        if (registry.type(id).name == "minecraft:trapped_chest") notifyAttached(slot.pos, Direction::up);
    }
}
}
