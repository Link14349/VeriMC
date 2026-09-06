#include "simulator/simulator.hpp"

namespace simulator {
bool Simulator::connectsTripwire(StateId neighbor, Direction direction) const {
    const auto& state = registry[neighbor];
    return state.device == Device::tripwire || (state.device == Device::tripwireHook && state.facing == opposite(direction));
}

void Simulator::updateTripwireSource(BlockPos pos, StateId state) {
    // This asymmetry is observable in crossing wires and must not be replaced
    // with a scan in all four directions.
    for (auto direction : {Direction::south, Direction::west}) {
        for (int distance = 1; distance < 42; ++distance) {
            auto next = pos.relative(direction, distance);
            auto id = world.get(next);
            if (registry[id].device == Device::tripwireHook) {
                if (registry[id].facing == opposite(direction)) calculateTripwire(next, id, false, true, distance, state);
                break;
            }
            if (registry[id].device != Device::tripwire) break;
        }
    }
}

void Simulator::calculateTripwire(BlockPos pos, StateId state, bool destroying, bool notify, int wireSource, StateId sourceState) {
    if (!registry.has(state, "facing")) return;
    const auto direction = registry[state].facing;
    bool wasAttached = registry.property(state, "attached") == "true";
    bool attached = !destroying, powered = false;
    int receiver = 0;
    std::array<StateId, 42> wireStates{};
    for (int distance = 1; distance < 42; ++distance) {
        auto wire = world.get(pos.relative(direction, distance));
        if (registry[wire].device == Device::tripwireHook) {
            if (registry[wire].facing == opposite(direction)) receiver = distance;
            break;
        }
        if (registry[wire].device != Device::tripwire && distance != wireSource) {
            attached = false;
            continue;
        }
        if (distance == wireSource) wire = sourceState;
        bool armed = registry.property(wire, "disarmed") != "true";
        powered = powered || (armed && registry[wire].powered);
        wireStates[static_cast<std::size_t>(distance)] = wire;
        if (distance == wireSource) {
            // The original call carries the hook's type even during removal.
            blockTicks.schedule({currentTick + 10, 0, nextOrder++, pos, registry[state].type});
            attached = attached && armed;
        }
    }
    attached = attached && receiver > 1;
    powered = powered && attached;
    auto next = registry.type(state).defaultState;
    next = registry.withBool(registry.withBool(next, "attached", attached), "powered", powered);
    auto notifyHook = [&](BlockPos hook, Direction facing) {
        updateNeighbors(hook, -1, state);
        updateNeighbors(hook.relative(opposite(facing)), -1, state);
    };
    if (receiver > 0) {
        auto other = pos.relative(direction, receiver);
        setBlock(other, registry.with(next, "facing", std::string(directionNames[static_cast<unsigned>(opposite(direction))])));
        notifyHook(other, opposite(direction));
        if (at(pos).device != Device::tripwireHook) { removeTripwireHook(pos, next); return; }
    }
    if (!destroying) {
        setBlock(pos, registry.with(next, "facing", std::string(directionNames[static_cast<unsigned>(direction)])));
        if (notify) notifyHook(pos, direction);
    }
    if (wasAttached != attached) {
        for (int distance = 1; distance < receiver; ++distance) {
            auto wire = wireStates[static_cast<std::size_t>(distance)];
            if (!wire) continue;
            auto nextPos = pos.relative(direction, distance);
            auto device = at(nextPos).device;
            if (device == Device::tripwire || device == Device::tripwireHook)
                setBlock(nextPos, registry.withBool(wire, "attached", attached));
        }
    }
}

void Simulator::removeTripwireHook(BlockPos pos, StateId state) {
    if (registry.property(state, "attached") == "true" || registry[state].powered)
        calculateTripwire(pos, state, true, false);
    if (registry[state].powered) {
        updateNeighbors(pos, -1, state);
        updateNeighbors(pos.relative(opposite(registry[state].facing)), -1, state);
    }
}

void Simulator::updateTripwire(BlockPos pos) {
    auto state = world.get(pos);
    if (registry[state].device != Device::tripwire) return;
    bool wasPressed = registry[state].powered;
    auto data = runtime.find(pos);
    bool pressed = data != runtime.end() && data->second.values.value("entities", 0) > 0;
    if (pressed != wasPressed) {
        state = registry.withBool(state, "powered", pressed);
        setBlock(pos, state);
        updateTripwireSource(pos, state);
    }
    if (pressed) schedule(pos, 10);
    else if (wasPressed) schedule(pos, 0);
}

void Simulator::tripwireContact(BlockPos pos) {
    auto data = runtime.find(pos);
    if (at(pos).device != Device::tripwire || at(pos).powered || data == runtime.end() || data->second.values.value("entities", 0) == 0) return;
    if (!hasScheduled(pos)) updateTripwire(pos);
    else schedulePhase(pos, currentTick + 1, 3, 0);
}
}
