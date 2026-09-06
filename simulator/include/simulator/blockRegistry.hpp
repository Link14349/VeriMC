#pragma once
#include "types.hpp"
#include <map>
#include <unordered_map>
#include <vector>

namespace simulator {
struct PropertyInfo { std::vector<std::string> values; std::uint32_t stride{}; };
struct BlockType {
    std::string name, className;
    StateId defaultState{}, firstState{};
    Device device{Device::unsupported};
    std::map<std::string, PropertyInfo> properties;
    std::string supportLevel{"unimplemented"};
};
struct BlockState {
    std::uint16_t type{};
    Device device{Device::air};
    Direction facing{Direction::north};
    std::uint8_t power{}, delay{1}, supportMask{}, rigidMask{}, centerMask{};
    bool conductor{}, fullCube{}, analogSource{}, blockEntity{}, replaceable{}, signalSource{};
    bool powered{}, lit{}, locked{}, extended{}, subtract{}, sticky{};
    std::uint8_t pushReaction{};
    std::array<std::uint8_t, 6> weak{}, strong{};
    std::array<std::uint8_t, 4> wireSides{};
    Direction connectedDirection{Direction::up};
};
class BlockRegistry {
public:
    explicit BlockRegistry(const std::string& path = std::string(SIMULATOR_DATA_DIR) + "/blockStates.json");
    const BlockState& operator[](StateId id) const { return states.at(id); }
    const BlockType& type(StateId id) const { return types[states.at(id).type]; }
    StateId state(const std::string& name, const Json& properties = Json::object()) const;
    StateId with(StateId id, const std::string& property, const std::string& value) const;
    StateId with(StateId id, const std::string& property, int value) const { return with(id, property, std::to_string(value)); }
    StateId withBool(StateId id, const std::string& property, bool value) const { return with(id, property, value ? std::string("true") : std::string("false")); }
    std::string property(StateId id, const std::string& key, const std::string& fallback = "") const;
    Json describe(StateId id) const;
    Json catalog() const;
    bool has(StateId id, const std::string& property) const { return type(id).properties.contains(property); }
    std::size_t stateCount() const { return states.size(); }
private:
    std::vector<BlockState> states;
    std::vector<BlockType> types;
    std::unordered_map<std::string, std::uint16_t> names;
};
}
