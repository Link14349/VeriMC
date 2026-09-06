#include "simulator/simulator.hpp"
#include <cmath>

namespace simulator {
namespace {
std::uint64_t unsignedInteger(const Json& value) {
    if (!value.is_number_integer() || value < 0) throw std::invalid_argument("外部动作序号必须是非负整数");
    return value.get<std::uint64_t>();
}
}
Json Simulator::pendingActionsJson() const {
    Json result = Json::array();
    if (!pendingActionIds.empty()) for (const auto& action : environmentActions)
        if (pendingActionIds.contains(action.at("id"))) result.push_back(action);
    return result;
}
void Simulator::recordAction(Json action) {
    while (environmentActions.size() >= actionCapacity) {
        if (pendingActionIds.contains(environmentActions.front().at("id"))) {
            faulted = true; breakRequested = true;
            throw std::runtime_error("外部动作记录已满，不能覆盖尚未确认的动作；请恢复有效快照");
        }
        environmentActions.pop_front(); ++actionsDropped;
    }
    action["id"] = nextActionId++; action["tick"] = currentTick; action["sequence"] = ++sequence; action["resolved"] = false;
    pendingActionIds.insert(action.at("id").get<std::uint64_t>()); environmentActions.push_back(std::move(action));
    breakRequested = true; pauseReason = "存在待处理的外部动作，请检查输出并提供环境反馈，或确认本次不反馈";
    ++revision;
}
void Simulator::resolveAction(std::uint64_t id) {
    if (!pendingActionIds.contains(id)) throw std::invalid_argument("外部动作不存在或已确认");
    for (auto& action : environmentActions) if (action.at("id") == id) { action["resolved"] = true; break; }
    pendingActionIds.erase(id); ++revision;
    if (pendingActionIds.empty() && pauseReason.starts_with("存在待处理的外部动作")) pauseReason.clear();
}
void Simulator::loadActions(const Json& data) {
    nextActionId = unsignedInteger(data.value("nextActionId", Json(1))); actionsDropped = unsignedInteger(data.value("actionsDropped", Json(0)));
    const auto actions = data.value("environmentActions", Json::array());
    if (!actions.is_array() || actions.size() > actionCapacity || nextActionId == 0) throw std::invalid_argument("无效外部动作历史");
    std::uint64_t previous = 0;
    for (const auto& action : actions) {
        auto id = unsignedInteger(action.at("id"));
        if (id <= previous || id >= nextActionId || unsignedInteger(action.at("tick")) > currentTick || unsignedInteger(action.at("sequence")) > sequence || !action.at("resolved").is_boolean()) throw std::invalid_argument("无效外部动作时序");
        previous = id;
        if (action.at("kind") != "itemEjected") throw std::invalid_argument("尚未支持该外部动作");
        action.at("source").get<BlockPos>();
        auto itemId = registry.itemId(action.at("item"));
        const auto& count = action.at("count");
        if (!itemId || !count.is_number_integer() || count < 1 || count > registry.item(itemId).maxStack) throw std::invalid_argument("无效投放物品数量");
        for (auto field : {"position", "velocity"}) {
            const auto& values = action.at(field);
            if (!values.is_array() || values.size() != 3) throw std::invalid_argument("无效投放向量");
            for (const auto& value : values) if (!value.is_number() || !std::isfinite(value.get<double>()) || std::abs(value.get<double>()) > 30000000) throw std::invalid_argument("无效投放向量");
        }
        environmentActions.push_back(action);
        if (!action.at("resolved").get<bool>()) pendingActionIds.insert(id);
    }
}
}
