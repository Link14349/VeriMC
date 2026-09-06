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
    switch (registry[id].device) {
    case Device::container: return 27;
    case Device::hopper: return 5;
    case Device::dropper: case Device::dispenser: case Device::crafter: return 9;
    case Device::furnace: return 3;
    default: return 0;
    }
}

Direction Simulator::chestConnection(StateId state) const {
    return turn(registry[state].facing, registry.property(state, "type") == "left" ? 1 : -1);
}

StateId Simulator::placedChest(BlockPos pos, StateId state) const {
    if (!registry.has(state, "type") || registry.property(state, "type") != "single") return state;
    for (int step : {1, -1}) {
        auto adjacent = world.get(pos.relative(turn(registry[state].facing, step)));
        if (registry[adjacent].type == registry[state].type && registry[adjacent].facing == registry[state].facing && registry.property(adjacent, "type") == "single")
            return registry.with(state, "type", std::string(step == 1 ? "left" : "right"));
    }
    return state;
}

void Simulator::updateChestShape(const Update& update) {
    auto id = world.get(update.pos), neighbor = update.neighborState;
    auto type = registry.property(id, "type");
    if (registry[id].type == registry[neighbor].type && axis(update.direction) != 0) {
        auto otherType = registry.property(neighbor, "type");
        if (type == "single" && otherType != "single" && registry[id].facing == registry[neighbor].facing && chestConnection(neighbor) == opposite(update.direction))
            setBlock(update.pos, registry.with(id, "type", std::string(otherType == "left" ? "right" : "left")), update.flags, update.depth);
    } else if (chestConnection(id) == update.direction) {
        setBlock(update.pos, registry.with(id, "type", std::string("single")), update.flags, update.depth);
    }
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

ItemStack Simulator::stackAt(const InventorySlot& slot) const {
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

std::vector<std::pair<std::size_t, ItemStack>> Simulator::parseInventory(const Json& values, std::size_t size) const {
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
    std::sort(edits.begin(), edits.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    return edits;
}

void Simulator::setInventory(BlockPos pos, const Json& values, bool combined, bool notify) {
    auto slots = combined ? containerSlots(pos) : std::vector<InventorySlot>{};
    if (!combined) for (std::size_t i = 0; i < inventorySize(world.get(pos)); ++i) slots.push_back({pos, i});
    if (slots.empty()) throw std::invalid_argument("这个器件没有库存槽位");
    for (const auto& [index, stack] : parseInventory(values, slots.size())) {
        const auto& slot = slots[index];
        auto& inventory = runtime[slot.pos].inventory;
        inventory.resize(inventorySize(world.get(slot.pos)));
        inventory[slot.index] = stack;
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
        runtime[slot.pos].values["viewers"] = viewers;
        if (registry.type(id).className == "BarrelBlock") setBlock(slot.pos, registry.withBool(id, "open", viewers > 0));
        runtimeChanged(slot.pos);
        if (registry.type(id).name == "minecraft:trapped_chest") notifyAttached(slot.pos, Direction::up);
    }
}
}
