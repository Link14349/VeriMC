#include "simulator/world.hpp"
#include <algorithm>

namespace simulator {
World::World(const World& other) : blockCount(other.blockCount) {
    chunks.reserve(other.chunks.size());
    for (const auto& [pos, chunk] : other.chunks) chunks.emplace(pos, std::make_unique<Chunk>(*chunk));
}
World& World::operator=(const World& other) { if (this != &other) { World copy(other); *this = std::move(copy); } return *this; }
World::World(World&& other) noexcept : chunks(std::move(other.chunks)), blockCount(other.blockCount) {
    other.chunks.clear(); other.blockCount = 0; other.chunkCache = {};
}
World& World::operator=(World&& other) noexcept {
    if (this != &other) {
        chunks = std::move(other.chunks); blockCount = other.blockCount; chunkCache = {};
        other.chunks.clear(); other.blockCount = 0; other.chunkCache = {};
    }
    return *this;
}
StateId World::set(BlockPos pos, StateId state) {
    const auto key = chunkPos(pos);
    auto& cached = chunkCache[cacheIndex(key)];
    if (!cached.valid || cached.pos != key) {
        const auto found = chunks.find(key);
        cached = {key, found == chunks.end() ? nullptr : found->second.get(), true};
    }
    if (!cached.chunk) {
        if (state == 0) return 0;
        cached.chunk = chunks.emplace(key, std::make_unique<Chunk>()).first->second.get();
    }
    auto& chunk = *cached.chunk;
    auto old = chunk.states[offset(pos)];
    chunk.states[offset(pos)] = state;
    if (old == 0 && state != 0) { ++chunk.count; ++blockCount; }
    if (old != 0 && state == 0) { --chunk.count; --blockCount; }
    if (chunk.count == 0) { chunks.erase(key); cached.chunk = nullptr; }
    return old;
}
std::vector<Cell> World::cells() const {
    std::vector<Cell> result;
    result.reserve(blockCount);
    for (const auto& [p, chunk] : chunks) for (std::size_t i = 0; i < 4096; ++i) {
        if (auto id = chunk->states[i]) result.push_back({{p.x * 16 + static_cast<int>(i & 15), p.y * 16 + static_cast<int>(i >> 8), p.z * 16 + static_cast<int>((i >> 4) & 15)}, id});
    }
    std::sort(result.begin(), result.end(), [](const Cell& a, const Cell& b) { return a.pos < b.pos; });
    return result;
}
std::vector<BlockPos> World::sectionPositions() const {
    std::vector<BlockPos> positions; positions.reserve(chunks.size());
    for (const auto& [pos, chunk] : chunks) { (void)chunk; positions.push_back(pos); }
    std::sort(positions.begin(), positions.end()); return positions;
}
std::span<const StateId, 4096> World::sectionStates(BlockPos section) const {
    return chunks.at(section)->states;
}
void World::forEachCell(const std::function<void(Cell)>& visit) const {
    for (const auto section : sectionPositions()) {
        const auto states = sectionStates(section);
        for (std::size_t i = 0; i < states.size(); ++i) if (states[i])
            visit({{section.x * 16 + static_cast<int>(i & 15), section.y * 16 + static_cast<int>(i >> 8), section.z * 16 + static_cast<int>((i >> 4) & 15)}, states[i]});
    }
}
void World::forEachCellXyz(const std::function<void(Cell)>& visit) const {
    const auto positions = sectionPositions();
    for (std::size_t xBegin = 0; xBegin < positions.size();) {
        auto xEnd = xBegin + 1;
        while (xEnd < positions.size() && positions[xEnd].x == positions[xBegin].x) ++xEnd;
        for (int x = 0; x < 16; ++x) for (auto yBegin = xBegin; yBegin < xEnd;) {
            auto yEnd = yBegin + 1;
            while (yEnd < xEnd && positions[yEnd].y == positions[yBegin].y) ++yEnd;
            for (int y = 0; y < 16; ++y) for (auto index = yBegin; index < yEnd; ++index) {
                const auto pos = positions[index]; const auto states = sectionStates(pos);
                for (int z = 0; z < 16; ++z) if (auto state = states[static_cast<std::size_t>(x | (z << 4) | (y << 8))])
                    visit({{pos.x * 16 + x, pos.y * 16 + y, pos.z * 16 + z}, state});
            }
            yBegin = yEnd;
        }
        xBegin = xEnd;
    }
}
}
