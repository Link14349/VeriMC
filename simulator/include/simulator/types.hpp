#pragma once
#include <array>
#include <cstdint>
#include <compare>
#include <string>
#include <nlohmann/json.hpp>

namespace simulator {
using Json = nlohmann::json;
using StateId = std::uint32_t;
using Tick = std::uint64_t;
enum class Direction : std::uint8_t { down, up, north, south, west, east };
constexpr std::array<Direction, 6> directions{Direction::down, Direction::up, Direction::north, Direction::south, Direction::west, Direction::east};
constexpr std::array<Direction, 6> updateOrder{Direction::west, Direction::east, Direction::down, Direction::up, Direction::north, Direction::south};
constexpr std::array<Direction, 6> shapeOrder{Direction::west, Direction::east, Direction::north, Direction::south, Direction::down, Direction::up};
constexpr std::array<Direction, 4> horizontal{Direction::north, Direction::east, Direction::south, Direction::west};
constexpr std::array<const char*, 6> directionNames{"down", "up", "north", "south", "west", "east"};
constexpr Direction opposite(Direction d) { return static_cast<Direction>(static_cast<unsigned>(d) ^ 1u); }
constexpr unsigned axis(Direction d) { return static_cast<unsigned>(d) / 2; }
struct BlockPos {
    std::int32_t x{}, y{}, z{};
    auto operator<=>(const BlockPos&) const = default;
    BlockPos relative(Direction d, int distance = 1) const {
        switch (d) {
        case Direction::down: return {x, y - distance, z};
        case Direction::up: return {x, y + distance, z};
        case Direction::north: return {x, y, z - distance};
        case Direction::south: return {x, y, z + distance};
        case Direction::west: return {x - distance, y, z};
        case Direction::east: return {x + distance, y, z};
        }
        return *this;
    }
};
struct PosHash {
    std::size_t operator()(BlockPos p) const noexcept {
        // Unsigned arithmetic intentionally defines overflow for negative positions.
        std::uint64_t h = static_cast<std::uint32_t>(p.x) * 0x9e3779b185ebca87ULL;
        h ^= static_cast<std::uint32_t>(p.y) * 0xc2b2ae3d27d4eb4fULL;
        h ^= static_cast<std::uint32_t>(p.z) * 0x165667b19e3779f9ULL;
        return static_cast<std::size_t>(h ^ (h >> 32));
    }
};
inline void to_json(Json& j, const BlockPos& p) { j = Json::array({p.x, p.y, p.z}); }
inline void from_json(const Json& j, BlockPos& p) {
    if (!j.is_array() || j.size() != 3) throw std::invalid_argument("坐标必须包含 x、y、z");
    for (const auto& n : j) if (!n.is_number_integer() || n.get<std::int64_t>() < -29999984 || n.get<std::int64_t>() > 29999984) throw std::invalid_argument("坐标超出支持范围 ±29999984");
    p = {j[0].get<std::int32_t>(), j[1].get<std::int32_t>(), j[2].get<std::int32_t>()};
}
inline Direction parseDirection(const std::string& s) {
    for (auto d : directions) if (s == directionNames[static_cast<unsigned>(d)]) return d;
    throw std::invalid_argument("无效方向：" + s);
}
enum class Device : std::uint8_t {
    air, solid, wire, source, lever, button, torch, wallTorch, repeater, comparator, observer, lamp, bulb,
    piston, pistonHead, movingPiston, hopper, container, dropper, dispenser, crafter, furnace,
    door, trapdoor, fenceGate, noteBlock, rail, poweredRail, detectorRail, activatorRail,
    pressurePlate, weightedPlate, target, daylight, lightningRod, sculkSensor, calibratedSensor,
    tripwire, tripwireHook, lectern, analog, unsupported
};
constexpr bool isRail(Device device) { return device >= Device::rail && device <= Device::activatorRail; }
constexpr bool isSensor(Device device) { return device == Device::sculkSensor || device == Device::calibratedSensor; }
}
