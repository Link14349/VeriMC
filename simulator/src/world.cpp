#include "simulator/world.hpp"
#include <algorithm>

namespace simulator {
StateId World::get(BlockPos pos) const {
    auto found = chunks.find(chunkPos(pos));
    return found == chunks.end() ? 0 : found->second->states[offset(pos)];
}
StateId World::set(BlockPos pos, StateId state) {
    const auto key = chunkPos(pos);
    auto found = chunks.find(key);
    if (found == chunks.end()) {
        if (state == 0) return 0;
        found = chunks.emplace(key, std::make_unique<Chunk>()).first;
    }
    auto& chunk = *found->second;
    auto old = chunk.states[offset(pos)];
    chunk.states[offset(pos)] = state;
    if (old == 0 && state != 0) { ++chunk.count; ++blockCount; }
    if (old != 0 && state == 0) { --chunk.count; --blockCount; }
    if (chunk.count == 0) chunks.erase(found);
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
}
