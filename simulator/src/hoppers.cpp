#include "simulator/simulator.hpp"
#include <algorithm>
#include <cmath>
#include <set>

namespace simulator {

void Simulator::startHopper(BlockPos pos) {
    registerEntity(pos);
    auto& hopper = hoppers[pos];
    hopper.firstTick = currentTick + (beforeBlockEntities() ? 0 : 1);
    wakeHopper(pos);
}

void Simulator::wakeHopper(BlockPos pos) {
    auto found = hoppers.find(pos);
    if (found == hoppers.end()) return;
    auto& hopper = found->second;
    auto rank = entityOrders.at(pos);
    // The current tick will schedule its own continuation after push and pull.
    if (currentPhase == 2 && rank == currentEntityOrder) return;
    bool canRunThisTick = beforeBlockEntities() || (currentPhase == 2 && rank > currentEntityOrder);
    auto when = std::max({hopper.readyAt, hopper.firstTick, currentTick + (canRunThisTick ? 0 : 1)});
    if (hopper.wakeAt <= when) return;
    if (hopper.wakeAt != UINT64_MAX) scheduledKeys.erase({pos, at(pos).type, 2, hopper.generation});
    hopper.wakeAt = when;
    hopper.generation = nextOrder++;
    schedulePhase(pos, when, 2, hopper.generation);
}

void Simulator::wakeHoppers(BlockPos changed) {
    wakeHopper(changed);
    wakeHopper(changed.relative(Direction::down));
    for (auto direction : directions) {
        auto pos = changed.relative(direction);
        if (at(pos).device == Device::hopper && at(pos).facing == opposite(direction)) wakeHopper(pos);
    }
}

bool Simulator::inventoryEmpty(BlockPos pos) const {
    for (const auto& slot : containerSlots(pos)) if (stackAt(slot).count) return false;
    return true;
}

bool Simulator::inventoryFull(BlockPos pos) const {
    for (const auto& slot : containerSlots(pos)) {
        auto stack = stackAt(slot);
        if (!stack.count || stack.count < registry.item(stack.item).maxStack) return false;
    }
    return true;
}

void Simulator::writeStack(const InventorySlot& slot, ItemStack stack, bool notify) {
    if (slot.entity >= 0) {
        // 实体容器的库存按 JSON 存放，与探测铁轨的矿车输入同一种表示。
        // 原版 AbstractMinecartContainer.setChanged 不发比较器通知，因此这里也不通知。
        auto& inventory = runtime.at(slot.pos).values.at("containerEntities").at(static_cast<std::size_t>(slot.entity)).at("inventory");
        for (auto it = inventory.begin(); it != inventory.end();) {
            if (it->at("slot").get<std::size_t>() == slot.index) it = inventory.erase(it); else ++it;
        }
        if (stack.count) inventory.push_back({{"slot", slot.index}, {"item", registry.item(stack.item).name}, {"count", stack.count}});
        std::sort(inventory.begin(), inventory.end(), [](const Json& a, const Json& b) { return a.at("slot") < b.at("slot"); });
        runtimeChanged(slot.pos, false);
        return;
    }
    const auto id=world.get(slot.pos);
    const bool bookshelf=isBookshelf(id), occupied=bookshelf && stackAt(slot).count>0;
    auto& inventory = runtime[slot.pos].inventory;
    inventory.resize(inventorySize(id));
    inventory[slot.index] = stack.count ? stack : ItemStack{};
    if(registry[id].device==Device::jukebox){updateJukeboxItem(slot.pos);return;}
    if(bookshelf) {
        if(occupied || stack.count) updateBookshelfSlot(slot);
        return;
    }
    // Hopper overrides and the pot's ContainerSingleItem omit setChanged from
    // setItem/removeItem; only the successful transfer notifies their readers.
    if (notify && registry[id].device != Device::hopper && !isDecoratedPot(id)) runtimeChanged(slot.pos);
}

void Simulator::containerChanged(BlockPos pos) {
    std::set<BlockPos> visited;
    for (const auto& slot : containerSlots(pos))
        if (visited.insert(slot.pos).second) runtimeChanged(slot.pos);
}

// 原版 getBlockContainer：WorldlyContainerHolder（堆肥桶）或「有方块实体且它是 Container」。
// 双箱走 ChestBlock.getContainer(..., override=true)，因此上方的实体方块不算遮挡。
bool Simulator::hasBlockContainer(BlockPos pos) const {
    return at(pos).device == Device::composter || !containerSlots(pos).empty();
}

// 原版 ejectItems 一开始就调用 getAttachedContainer，方块容器为空才查实体容器；
// 后者只要该格有候选就消耗一次 nextInt，与本次推出是否成功无关。
bool Simulator::hopperEject(BlockPos pos, bool* drew) {
    const auto target = pos.relative(at(pos).facing);
    if (hasBlockContainer(target)) return transferItem(pos, target);
    // 实体候选按**包围盒**取：朝向格里没有任何声明，但邻格的船探进来时照样算候选。
    const auto entity = chooseContainerEntity(cellQueryBox(target));
    if (!entity) return false;
    if (drew) *drew = true;
    return transferSlots(containerSlots(pos), entityContainerSlots(entity->pos, entity->entity), pos, entity->pos, false);
}

bool Simulator::transferItem(BlockPos from, BlockPos to, bool pulling) {
    if(at(from).device==Device::composter || at(to).device==Device::composter)return transferComposter(from,to,pulling);
    return transferSlots(containerSlots(from), containerSlots(to), from, to, pulling);
}
// 原版 getContainerAt 先看方块容器，没有才看实体容器；后者在候选里随机选一个并消耗随机数。
// 这里把「哪一侧是实体容器」交给调用方决定，转移规则本身完全一致。
bool Simulator::transferSlots(const std::vector<InventorySlot>& sourceSlots, const std::vector<InventorySlot>& targetSlots,
                              BlockPos from, BlockPos to, bool pulling) {
    const auto slotsFull = [&](const std::vector<InventorySlot>& slots) {
        for (const auto& slot : slots) {
            const auto stack = stackAt(slot);
            if (!stack.count || stack.count < registry.item(stack.item).maxStack) return false;
        }
        return true;
    };
    const auto slotsEmpty = [&](const std::vector<InventorySlot>& slots) {
        for (const auto& slot : slots) if (stackAt(slot).count) return false;
        return true;
    };
    if (sourceSlots.empty() || targetSlots.empty() || (!pulling && slotsFull(targetSlots))) return false;
    for (const auto& source : sourceSlots) {
        auto original = stackAt(source);
        if (!original.count) continue;
        if(pulling && !canExtractStack(source,targetSlots)) continue;
        auto remaining = original;
        --remaining.count;
        writeStack(source, remaining);
        bool targetWasEmpty = slotsEmpty(targetSlots);
        for (const auto& target : targetSlots) {
            if(!canInsertStack(target,original)) continue;
            auto stack = stackAt(target);
            if (stack.count && (stack.item != original.item || stack.count >= registry.item(stack.item).maxStack)) continue;
            writeStack(target, {original.item, static_cast<std::uint16_t>(stack.count + 1)}, stack.count == 0);
            if (targetWasEmpty && at(to).device == Device::hopper && target.entity < 0) {
                // All transfers here run during the block entity phase. An
                // empty recipient becomes eligible seven ticks later whether
                // it already ticked (cooldown 7) or ticks later today (8 -> 7).
                auto& hopper = hoppers.at(to);
                hopper.readyAt = currentTick + (currentPhase == 2 ? 7 : 8);
            }
            // 实体容器不是方块实体，setChanged 不会通知比较器。
            if (target.entity < 0) containerChanged(to);
            if (pulling && source.entity < 0) containerChanged(from);
            return true;
        }
        // Failed extraction restores the original stack, including vanilla's
        // container notification on both removal and restoration.
        writeStack(source, original, original.count == 1);
    }
    return false;
}

// ---- 器件层实体物品输入（issue #12 第一阶段）----
// 声明落在漏斗吸取范围里的掉落物实体。位置是相对于漏斗方块角点的实数偏移，
// 默认 (0.5, 1.0, 0.5)。列表的顺序**就是**迭代顺序：原版的顺序来自实体分区存储，
// 器件层协议不建模那一层，所以顺序是显式输入而不是推导结果。
void Simulator::stimulateGroundItems(BlockPos pos, const Json& input) {
    if (input.size() != 1) throw std::invalid_argument("掉落物输入不能与其他刺激字段混用");
    if (at(pos).device != Device::hopper) throw std::invalid_argument("目前只有漏斗接受掉落物输入");
    const auto& items = input.at("groundItems");
    if (!items.is_array() || items.size() > 32) throw std::invalid_argument("掉落物列表最多 32 个");
    Json stored = Json::array();
    for (const auto& entry : items) {
        if (!entry.is_object()) throw std::invalid_argument("每个掉落物必须是对象");
        for (const auto& field : entry.items())
            if (field.key() != "item" && field.key() != "count" && field.key() != "x" && field.key() != "y" && field.key() != "z")
                throw std::invalid_argument("掉落物只接受 item、count 与 x/y/z");
        const auto item = registry.itemId(entry.at("item"));
        if (!entry.contains("count") || !entry.at("count").is_number_integer()) throw std::invalid_argument("掉落物必须给出整数 count");
        const int count = entry.at("count").get<int>();
        if (count < 1 || count > static_cast<int>(registry.item(item).maxStack)) throw std::invalid_argument("掉落物数量必须在 1 到该物品堆叠上限之间");
        const double x = entry.value("x", 0.5), y = entry.value("y", 1.0), z = entry.value("z", 0.5);
        for (double value : {x, y, z})
            if (!std::isfinite(value) || value < -2.0 || value > 4.0) throw std::invalid_argument("掉落物坐标必须在漏斗附近 [-2,4] 之内");
        stored.push_back({{"item", registry.item(item).name}, {"count", count}, {"x", x}, {"y", y}, {"z", z}});
    }
    auto& values = runtime[pos].values;
    if (stored.empty()) values.erase("groundItems"); else values["groundItems"] = std::move(stored);
    // 生成掉落物本身不通知比较器；只有真正被吸进来才走容器的 setChanged。
    runtimeChanged(pos, false);
    wakeHopper(pos);
    // 停在漏斗那一格里的掉落物走实体阶段的 entityInside 路径，每刻各触发一次。
    if (hopperHasContact(pos)) schedulePhase(pos, currentTick + (currentPhase < 3 ? 0 : 1), 3, 0);
}

// 原版 Hopper.SUCK_AABB = Block.column(16, 11, 32)，按 (levelX-0.5, levelY-0.5, levelZ-0.5)
// 平移后正好是 x∈[px,px+1]、y∈[py+0.6875, py+2]、z∈[pz,pz+1]。
// 掉落物实体的包围盒宽 0.25、高 0.25，水平居中于位置，竖直从 y 向上。
// AABB.intersects 用严格不等号，相切不算相交。
bool Simulator::itemInSuckRange(BlockPos pos, const Json& entry) const {
    const double x = entry.at("x").get<double>() + pos.x;
    const double y = entry.at("y").get<double>() + pos.y;
    const double z = entry.at("z").get<double>() + pos.z;
    return x - 0.125 < pos.x + 1.0 && x + 0.125 > static_cast<double>(pos.x)
        && y < pos.y + 2.0 && y + 0.25 > pos.y + 0.6875
        && z - 0.125 < pos.z + 1.0 && z + 0.125 > static_cast<double>(pos.z);
}

// 原版 HopperBlockEntity.addItem(null, container, stack, null)：按槽位顺序整叠放入。
// 每成功放进一格就调用一次 container.setChanged()；漏斗从空变非空时额外置 8 gt 冷却。
void Simulator::insertStack(BlockPos pos, ItemStack& stack, Tick cooldown) {
    for (const auto& slot : containerSlots(pos)) {
        if (!stack.count) break;
        if (!canInsertStack(slot, stack)) continue;
        const auto current = stackAt(slot);
        const bool wasEmpty = inventoryEmpty(pos);
        bool success = false;
        if (!current.count) {
            writeStack(slot, stack, false);
            stack.count = 0;
            success = true;
        } else if (current.item == stack.item) {
            const int space = static_cast<int>(registry.item(stack.item).maxStack) - current.count;
            const int moved = std::min<int>(stack.count, std::max(space, 0));
            if (moved > 0) {
                writeStack(slot, {current.item, static_cast<std::uint16_t>(current.count + moved)}, false);
                stack.count = static_cast<std::uint16_t>(stack.count - moved);
                success = true;
            }
        }
        if (!success) continue;
        // from == null，所以原版的 skipTickCount 恒为 0，冷却总是 8 gt。折算成 readyAt 时要看
        // 本刻的 pushItemsTick 是否还会再递减一次：方块实体阶段里已经递减过（+8），
        // 实体阶段还在方块实体阶段之前，本刻还会再减一次（+7）。
        if (wasEmpty) {
            auto hopper = hoppers.find(pos);
            if (hopper != hoppers.end()) hopper->second.readyAt = currentTick + cooldown;
        }
        containerChanged(pos);
    }
}

// 只投影物品与数量：坐标是输入，不是观测量。原版实体每刻都会被 Entity.move 从包围盒
// 反算一次位置，产生 1e-15 量级的漂移，拿它做等值比较没有意义。
// 吸取范围本身仍然被检验：范围判据不一致会让列表里出现或缺少条目。
Json Simulator::suckableItems(BlockPos pos) const {
    Json result = Json::array();
    auto found = runtime.find(pos);
    if (found == runtime.end() || !found->second.values.contains("groundItems")) return result;
    for (const auto& entry : found->second.values.at("groundItems"))
        if (itemInSuckRange(pos, entry)) result.push_back({{"item", entry.at("item")}, {"count", entry.at("count")}});
    return result;
}

// 原版 suckInItems 的实体分支：只有**整叠**被放进去才算 changed；
// 部分放入会把剩余量写回实体并继续看下一个掉落物，本次不算变化、不设 8 gt 冷却。
// 原版 entityInside 只对「实体所在的那一格」调用，所以掉落物必须与漏斗自己这一格重叠。
bool Simulator::itemInsideHopperBlock(BlockPos pos, const Json& entry) const {
    const double x = entry.at("x").get<double>() + pos.x;
    const double y = entry.at("y").get<double>() + pos.y;
    const double z = entry.at("z").get<double>() + pos.z;
    return x - 0.125 < pos.x + 1.0 && x + 0.125 > static_cast<double>(pos.x)
        && y < pos.y + 1.0 && y + 0.25 > static_cast<double>(pos.y)
        && z - 0.125 < pos.z + 1.0 && z + 0.125 > static_cast<double>(pos.z);
}

bool Simulator::hopperHasContact(BlockPos pos) const {
    auto found = runtime.find(pos);
    if (found == runtime.end() || !found->second.values.contains("groundItems")) return false;
    for (const auto& entry : found->second.values.at("groundItems"))
        if (itemInsideHopperBlock(pos, entry) && itemInSuckRange(pos, entry)) return true;
    return false;
}

// 原版 HopperBlock.entityInside -> HopperBlockEntity.entityInside -> tryMoveItems。
// 每个符合条件的掉落物每刻各触发一次完整的 tryMoveItems（先推出、再吸这一个实体），
// 不经过 suckInItems，因此**不受上方完整方块阻挡**。
void Simulator::hopperEntityContact(BlockPos pos) {
    if (at(pos).device != Device::hopper || !hoppers.count(pos)) return;
    for (std::size_t index = 0; index < runtime.at(pos).values.value("groundItems", Json::array()).size(); ++index) {
        auto& hopper = hoppers.at(pos);
        if (hopper.readyAt > currentTick) break;
        if (registry.property(world.get(pos), "enabled") != "true") break;
        const Json entry = runtime.at(pos).values.at("groundItems").at(index);
        if (!itemInsideHopperBlock(pos, entry) || !itemInSuckRange(pos, entry)) continue;
        // 原版 entityInside 是由掉落物**自己**的 tick 驱动的，因此判据是**物品所在区块**
        // 是否 entity ticking，而不是漏斗所在区块。声明的坐标允许落到相邻区块里，
        // 两个区块状态不同时这个区别就是可观测的。
        const BlockPos itemCell{static_cast<int>(std::floor(pos.x + entry.value("x", 0.5))),
                                static_cast<int>(std::floor(pos.y + entry.value("y", 1.0))),
                                static_cast<int>(std::floor(pos.z + entry.value("z", 0.5)))};
        if (!chunkEntityTicking(itemCell)) continue;
        bool changed = false;
        if (!inventoryEmpty(pos)) changed = hopperEject(pos);
        if (!inventoryFull(pos)) {
            const int before = entry.at("count").get<int>();
            ItemStack remaining{registry.itemId(entry.at("item")), static_cast<std::uint16_t>(before)};
            insertStack(pos, remaining, 7);
            auto& list = runtime.at(pos).values.at("groundItems");
            if (!remaining.count) { list.erase(index--); runtimeChanged(pos, false); changed = true; }
            else if (remaining.count != before) { list.at(index)["count"] = remaining.count; runtimeChanged(pos, false); }
        }
        if (changed) {
            // 实体阶段设的 8 gt 冷却，本刻的 pushItemsTick 还会再递减一次。
            hoppers.at(pos).readyAt = currentTick + 7;
            containerChanged(pos);
        }
    }
    if (hopperHasContact(pos)) schedulePhase(pos, currentTick + 1, 3, 0);
}

bool Simulator::suckItemEntities(BlockPos pos) {
    if (!runtime.count(pos) || !runtime.at(pos).values.contains("groundItems")) return false;
    for (std::size_t index = 0; index < runtime.at(pos).values.at("groundItems").size(); ++index) {
        const Json entry = runtime.at(pos).values.at("groundItems").at(index);
        if (!itemInSuckRange(pos, entry)) continue;
        const int before = entry.at("count").get<int>();
        ItemStack remaining{registry.itemId(entry.at("item")), static_cast<std::uint16_t>(before)};
        insertStack(pos, remaining, 8);
        auto& list = runtime.at(pos).values.at("groundItems");
        if (!remaining.count) { list.erase(index); runtimeChanged(pos, false); return true; }
        if (remaining.count != before) { list.at(index)["count"] = remaining.count; runtimeChanged(pos, false); }
    }
    return false;
}

void Simulator::tickHopper(const ScheduledEvent& event) {
    auto found = hoppers.find(event.pos);
    if (found == hoppers.end() || found->second.generation != event.data) return;
    auto& hopper = found->second;
    hopper.wakeAt = UINT64_MAX;
    if (hopper.readyAt > currentTick) {
        hopper.wakeAt = hopper.readyAt;
        schedulePhase(event.pos, hopper.readyAt, 2, hopper.generation);
        return;
    }
    if (registry.property(world.get(event.pos), "enabled") != "true") return;
    bool moved = false;
    // 原版 tryMoveItems 失败时**不设冷却**，因此下一刻还会整套重跑一遍。内核为省事在空转后
    // 就不再排程，靠库存/拓扑/信号变化唤醒——只要空转确实没有可观测效果，这是等价的。
    // 容器实体打破了这个前提：只要那一格有矿车，getEntityContainer 每刻都消耗一次
    // nextInt，哪怕这次一件也搬不动。少抽的那些会让世界随机源整体错位，进而改变之后
    // 任何一次抽取（投掷器选槽等）的结果，所以这种空转必须逐刻重试。
    bool drew = false;
    if (!inventoryEmpty(event.pos)) moved = hopperEject(event.pos, &drew);
    bool retryExtraction = false;
    if (!inventoryFull(event.pos)) {
        auto source = event.pos.relative(Direction::up);
        // 原版 suckInItems 先找上方的容器；找不到容器才看掉落物，
        // 而且此时完整碰撞方块会挡住吸取，除非它在 DOES_NOT_BLOCK_HOPPERS 里。
        if (!hasBlockContainer(source)) {
            // getSourceContainer 的实体分支：容器实体优先于掉落物，选中一个就只从它拉取，
            // 拉不到也不会退回去吸掉落物（原版 container != null 分支直接 return false）。
            if (const auto entity = chooseContainerEntity(cellQueryBox(source))) {
                drew = true;
                moved = transferSlots(entityContainerSlots(entity->pos, entity->entity), containerSlots(event.pos), entity->pos, event.pos, true) || moved;
            }
            else {
                const auto aboveId = world.get(source);
                const bool blocked = registry[aboveId].fullCube && !registry.type(aboveId).doesNotBlockHoppers;
                if (!blocked) moved = suckItemEntities(event.pos) || moved;
            }
        } else {
            bool pulled = transferItem(source, event.pos, true);
            // Failed extraction from ordinary containers can still issue comparator
            // updates on remove/restore. Preserve those observable repeated calls.
            retryExtraction = !pulled && at(source).device != Device::hopper && !isDecoratedPot(world.get(source)) && !inventoryEmpty(source);
            if(retryExtraction && (isBookshelf(world.get(source)) || at(source).device==Device::jukebox)) {
                retryExtraction=false;
                const auto into=containerSlots(event.pos);
                for(const auto& slot:containerSlots(source)) if(stackAt(slot).count && canExtractStack(slot,into)) {retryExtraction=true;break;}
            }
            moved = pulled || moved;
        }
    }
    if (moved) {
        hopper.readyAt = currentTick + 8;
        containerChanged(event.pos);
        hopper.wakeAt = hopper.readyAt;
        schedulePhase(event.pos, hopper.readyAt, 2, hopper.generation);
    } else if (retryExtraction || drew) {
        hopper.wakeAt = currentTick + 1;
        schedulePhase(event.pos, hopper.wakeAt, 2, hopper.generation);
    }
    // A failed transfer has no future work until inventory, topology or power
    // changes. Those mutations wake only adjacent dependent hoppers.
}

// ---- 漏斗矿车主动吸取（issue #12 的最后一条）----
//
// 26.2 反编译源码核实（simulator/.cache/reference/sources）：
//   * `vehicle/minecart/MinecartHopper.java:84-88` `tick()`：先把 consumedItemThisFrame 清零，
//     再 super.tick()（移动，途中 makeStepAlongTrack:91-95 可能提前吸一次），最后 tryConsumeItems()。
//   * 同文件 `:97-102` `tryConsumeItems()`：判据只有 服务端 / isAlive / isEnabled /
//     本帧还没搬过 / suckInItems()。**没有任何冷却字段**——矿车这一侧不存在 8 gt 冷却。
//   * 同文件 `:104-117` `suckInItems()`：先 `HopperBlockEntity.suckInItems(level, this)`，
//     失败才看包围盒 inflate(0.25,0,0.25) 里的掉落物实体。
//   * 同文件 `:63-81`：`getLevelX/Y/Z` 是 `getX() / getY()+0.5 / getZ()`，`isGridAligned()` 为 **false**。
//   * `block/entity/HopperBlockEntity.java:218-246` `suckInItems`：查询格是
//     `BlockPos.containing(levelX, levelY + 1.0, levelZ)`，取用方向恒为 `Direction.DOWN`；
//     `isGridAligned()` 为假时**跳过**「上方完整方块阻挡」那一条。
//   * 同文件 `:354-356 → :367-376`：`getSourceContainer → getContainerAt`，
//     `getBlockContainer`（:378-391）优先，为空才 `getEntityContainer`（:393-398），
//     后者候选非空就一定消耗一次 `level.getRandom().nextInt(size)`。
//   * 同文件 `:314-348` `tryMoveInItem`：置 8 gt 冷却那一条要求收件方 `instanceof HopperBlockEntity`，
//     矿车不是，所以矿车被塞满也不会进入冷却；成功取出时 `:255` 对**源容器**调用 setChanged。
//   * 顺序：`server/level/ServerLevel.java:426` 先 `entityTickList.forEach`（实体），
//     `:450` 才 `tickBlockEntities()`。所以同一刻里矿车吸取**早于**方块漏斗，
//     对应内核的阶段 3 早于阶段 2。
//   * `isEnabled()` 只被 `activateMinecart` 改写，而它唯一的调用点是
//     `NewMinecartBehavior.java:250` / `OldMinecartBehavior.java:66` 的 `moveAlongTrack`——
//     属于轨道与运动模型，本协议明确不建模，因此这里的矿车**恒为启用**。
//
// 本协议约定矿车停在**格中心**（y = 格底 + 0.5），于是 levelY + 1.0 = 格底 + 2.0，
// 掏的是**上面第二格**。停在铁轨高度（y = 格底 + 0.0625）的矿车掏的是正上方那一格，
// 但那个 y **目前无法声明**：位置的粒度是「哪一格」，不是实数坐标。
//
// **尚无原版差分。** 本轮参考捕获资源被另一个 agent 独占，下面的行为全部是从源码推出的期望；
// 唯一的实测旁证是上一轮的原版探针（12 刻、格中心停放、方块漏斗放在上面第二格：
// 第 3/4/5 刻各被搬走一件、3→0、全程零冷却、世界随机源一步没动），只覆盖这一个几何。

bool Simulator::cellHasCartHopper(BlockPos pos) const {
    auto found = runtime.find(pos);
    if (found == runtime.end()) return false;
    auto entities = found->second.values.find("containerEntities");
    if (entities == found->second.values.end()) return false;
    for (const auto& row : *entities) if (row.at("type") == "hopper_minecart") return true;
    return false;
}

void Simulator::rebuildEntityCells() {
    cartCells.clear();
    entityCells.clear();
    for (const auto& [pos, data] : runtime) {
        if (data.values.contains("containerEntities")) entityCells.insert(pos);
        if (cellHasCartHopper(pos)) cartCells.insert(pos);
    }
}

// 矿车自己那一侧的查询盒。MinecartHopper.getLevelX/Y/Z（MinecartHopper.java:63-75）
// 是 `getX() / getY()+0.5 / getZ()`，HopperBlockEntity.getSourceContainer（:354-356）
// 把它 +1.0 之后交给 getContainerAt，实体分支再 ±0.5。协议约定矿车停在格中心
// （脚 y = 格底 + 0.5），于是盒子是 [X, X+1] × [Y+1.5, Y+2.5] × [Z, Z+1]。
//
// **这个盒子与方块容器那一格不是一回事**：方块那一侧走
// `BlockPos.containing(levelX, levelY+1.0, levelZ)` = 上面**第二**格，
// 而实体那一侧按盒子算——上面第二格里、同样停在格中心的实体脚正好在 Y+2.5，
// AABB 的严格不等号把它排除掉，真正落进盒子的是**正上方那一格**里的实体
// （矿车盒 [Y+1.5, Y+2.2]、船盒 [Y+1.5, Y+2.0625] 都与 [Y+1.5, Y+2.5] 相交）。
// 换成「停在铁轨高度」那个约定（脚 y = 格底 + 0.0625）结论一样，仍然只有正上方那一格，
// 所以这条不依赖那个知道得不太确切的 y。
// **这一条只有源码推导，没有原版差分**：本轮参考捕获资源被别的 agent 独占。
Simulator::EntityBox Simulator::cartEntityQueryBox(BlockPos cell) {
    return {static_cast<double>(cell.x), cell.y + 1.5, static_cast<double>(cell.z),
            cell.x + 1.0, cell.y + 2.5, cell.z + 1.0};
}

// 新声明的容器实体要唤醒所有可能读到它的漏斗与漏斗矿车。覆盖格最多是
// 「声明格向水平各扩一格、向上扩一格」（船 3×2×3 = 18 格，矿车 1×2×1 = 2 格），
// 这里按最大范围扫，多唤醒几个漏斗只会让它们各跑一遍 tickHopper——
// 原版本来就是每个漏斗每刻都跑一遍，休眠才是内核的优化，所以多跑永远不会错。
void Simulator::wakeEntityReaders(BlockPos cell) {
    for (int dx = -1; dx <= 1; ++dx) for (int dz = -1; dz <= 1; ++dz) for (int dy = 0; dy <= 1; ++dy) {
        const BlockPos covered{cell.x + dx, cell.y + dy, cell.z + dz};
        // 读者一：正下方那一格的漏斗（suckInItems 查的是自己上面那一格）。
        if (!hoppers.empty()) {
            wakeHopper(covered.relative(Direction::down));
            // 读者二：朝着这一格的漏斗（ejectItems 查的是自己朝向的那一格）。
            for (auto direction : directions) {
                const auto neighbor = covered.relative(direction);
                if (at(neighbor).device == Device::hopper && at(neighbor).facing == opposite(direction)) wakeHopper(neighbor);
            }
        }
        // 读者三：正下方那一格里的漏斗矿车（它的查询盒罩住的是正上方那一格）。
        wakeCartHopper(covered.relative(Direction::down));
    }
}

void Simulator::scheduleCartSuction(BlockPos cell, Tick when) {
    // 方块类型固定记 0：矿车不是方块，那一格的方块可以随便换，事件不能因此被丢掉。
    if (scheduledKeys.insert({cell, 0, 3, cartSuctionEvent}).second)
        scheduled.push({when, 0, nextOrder++, cell, 0, 3, cartSuctionEvent, 0});
}

void Simulator::wakeCartHopper(BlockPos cell) {
    if (!cartCells.count(cell)) return;
    // 刻内顺序是 0（方块计划刻）→ 1（方块事件）→ 3（实体接触）→ 2（方块实体）→ 4（命令）。
    // 只有还没走到阶段 3 的时候才来得及在**本刻**吸取。
    scheduleCartSuction(cell, currentTick + (currentPhase <= 1 ? 0 : 1));
}

void Simulator::wakeCartHoppers(BlockPos changed) {
    if (cartCells.empty()) return;
    // 矿车只读上面第二格，所以一格的方块或器件数据变化只可能影响下方两格里的矿车。
    wakeCartHopper(changed.relative(Direction::down, 2));
}

// 返回「下一刻还得再跑一次」。原版矿车每刻都调用 suckInItems 且没有冷却，
// 内核为省事在无事可做时休眠——只有确实没有可观测效果时这才等价。
// 有可观测效果的三种情况：抽了随机数、搬动了物品、失败的提取仍然通知了源容器。
bool Simulator::cartHopperSuck(BlockPos cell, int entity, BlockPos source) {
    const auto target = entityContainerSlots(cell, entity);
    if (target.empty()) return false;
    if (hasBlockContainer(source)) {
        // 堆肥桶是 WorldlyContainerHolder，走 getBlockContainer 的第一条分支。
        if (at(source).device == Device::composter) return transferComposter(source, cell, true, &target);
        if (transferSlots(containerSlots(source), target, source, cell, true)) return true;
        // 提取失败时原版照样走一遍 removeItem/setItem，对普通容器就是可观测的 setChanged；
        // 漏斗与饰纹陶罐在 setItem 里不通知，失败就真的什么都没发生，可以休眠。
        if (at(source).device == Device::hopper || isDecoratedPot(world.get(source)) || inventoryEmpty(source)) return false;
        if (isBookshelf(world.get(source)) || at(source).device == Device::jukebox) {
            for (const auto& slot : containerSlots(source)) if (stackAt(slot).count && canExtractStack(slot, target)) return true;
            return false;
        }
        return true;
    }
    // 没有方块容器才查实体容器：候选非空就抽一次 nextInt，与这次是否真的搬动无关，
    // 因此只要查询盒里还停着容器实体就必须逐刻重跑（与方块漏斗那一侧同一条理由）。
    // 注意查的是 cartEntityQueryBox 而不是 source 那一格，理由见该函数上面的注释。
    if (const auto chosen = chooseContainerEntity(cartEntityQueryBox(cell))) {
        transferSlots(entityContainerSlots(chosen->pos, chosen->entity), target, chosen->pos, cell, true);
        return true;
    }
    // 掉落物分支（suckInItems 的 else 与 MinecartHopper.suckInItems 的包围盒扫描）
    // 尚未建模：groundItems 是声明在**漏斗方块**上的，矿车没有对应的声明入口。
    return false;
}

void Simulator::tickCartHoppers(BlockPos cell) {
    if (!cartCells.count(cell)) return;
    const auto source = cell.relative(Direction::up, 2);
    bool again = false;
    // 同一格里的多辆矿车按声明顺序各自吸一次，各自都可能抽一次随机数。
    for (std::size_t index = 0; index < containerEntityCount(cell); ++index) {
        if (runtime.at(cell).values.at("containerEntities").at(index).at("type") != "hopper_minecart") continue;
        again = cartHopperSuck(cell, static_cast<int>(index), source) || again;
    }
    if (again) scheduleCartSuction(cell, currentTick + 1);
}
}
