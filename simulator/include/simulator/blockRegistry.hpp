#pragma once
#include "types.hpp"
#include <map>
#include <unordered_map>
#include <vector>

namespace simulator {
struct PropertyInfo { std::vector<std::string> values; std::uint32_t stride{}; };
struct ItemInfo { std::string name; std::uint16_t maxStack{}; bool bookshelfBook{}; int jukeboxSong{-1}; float compostChance{-1}; };
struct SongInfo { std::string name, sound; std::uint32_t lengthTicks{}; std::uint8_t comparatorOutput{}; };
struct GameEventInfo { std::string name; int radius{}, frequency{}; bool listenable{}, ignoreSneaking{}; };
struct InstrumentInfo { std::string name, sound; bool tunable{}, above{}, custom{}; };
struct BlockType {
    std::string name, className;
    StateId defaultState{}, firstState{};
    Device device{Device::unsupported};
    std::map<std::string, PropertyInfo> properties;
    std::string supportLevel{"unimplemented"};
    bool occludesVibrations{}, dampensVibrations{}, vibrationResonator{};
    std::uint8_t instrument{};
};
// A power-of-two stride avoids division by 40 in each checked vector lookup.
// Each immutable state also stays inside one aligned 64-byte region.
struct alignas(64) BlockState {
    std::uint16_t type{};
    Device device{Device::air};
    Direction facing{Direction::north};
    std::uint8_t power{}, delay{1}, supportMask{}, rigidMask{}, centerMask{}, staticAnalog{};
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
    std::size_t typeCount() const { return types.size(); }
    const BlockType& blockType(std::uint16_t id) const { return types.at(id); }
    const std::vector<std::uint8_t>& ruleFingerprint() const { return rulesHash; }
    std::uint32_t itemId(const std::string& name) const;
    const ItemInfo& item(std::uint32_t id) const { return items.at(id); }
    Json itemCatalog() const;
    std::uint16_t gameEventId(const std::string& name) const;
    const GameEventInfo& gameEvent(std::uint16_t id) const { return gameEvents.at(id); }
    Json gameEventCatalog() const;
    const InstrumentInfo& instrument(std::uint8_t id) const { return instruments.at(id); }
    std::uint8_t instrumentId(const std::string& name) const;
    float notePitch(int note) const { return notePitches.at(static_cast<std::size_t>(note)); }
    const SongInfo& song(int index) const { return songs.at(static_cast<std::size_t>(index)); }
    int songId(const std::string& name) const;
private:
    std::vector<std::uint8_t> rulesHash;
    std::vector<BlockState> states;
    std::vector<BlockType> types;
    std::unordered_map<std::string, std::uint16_t> names;
    std::vector<ItemInfo> items;
    std::unordered_map<std::string, std::uint32_t> itemNames;
    std::vector<GameEventInfo> gameEvents;
    std::unordered_map<std::string, std::uint16_t> gameEventNames;
    std::vector<InstrumentInfo> instruments;
    std::array<float,25> notePitches{};
    std::vector<SongInfo> songs;
};
}
