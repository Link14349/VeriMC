#pragma once
#include "types.hpp"
#include <memory>
#include <unordered_map>
#include <vector>
#include <functional>
#include <span>

namespace simulator {
struct Cell { BlockPos pos; StateId state; };
class World {
public:
    World() = default;
    World(const World& other);
    World& operator=(const World& other);
    World(World&& other) noexcept;
    World& operator=(World&& other) noexcept;
    StateId get(BlockPos pos) const {
        const auto key = chunkPos(pos);
        auto& cached = chunkCache[cacheIndex(key)];
        if (!cached.valid || cached.pos != key) {
            auto found = chunks.find(key);
            cached = {key, found == chunks.end() ? nullptr : found->second.get(), true};
        }
        return cached.chunk ? cached.chunk->states[offset(pos)] : 0;
    }
    StateId set(BlockPos pos, StateId state);
    std::vector<Cell> cells() const;
    std::vector<BlockPos> sectionPositions() const;
    std::span<const StateId, 4096> sectionStates(BlockPos section) const;
    // Read-only traversal; callbacks must not modify this World.
    void forEachCell(const std::function<void(Cell)>& visit) const;
    void forEachCellXyz(const std::function<void(Cell)>& visit) const;
    std::size_t size() const { return blockCount; }
    std::size_t chunkCount() const { return chunks.size(); }
    std::size_t storageBytes() const { return chunks.size() * sizeof(Chunk); }
    void clear() { chunks.clear(); blockCount = 0; chunkCache = {}; }
private:
    struct Chunk { std::array<StateId, 4096> states{}; std::uint32_t count{}; };
    // Pointers refer to individually owned chunks, so map rehashing is safe.
    // Each mutation updates this direct-mapped cache, including cached misses.
    // World has one owner thread; concurrent readers require separate snapshots.
    struct CachedChunk { BlockPos pos{}; Chunk* chunk{}; bool valid{}; };
    static std::size_t cacheIndex(BlockPos key) {
        return (static_cast<unsigned>(key.x) + static_cast<unsigned>(key.y) * 3u + static_cast<unsigned>(key.z) * 5u) & 7u;
    }
    mutable std::array<CachedChunk, 8> chunkCache{};
    // C++20 defines signed right shift as division rounded toward -infinity,
    // including INT_MIN; this matches Minecraft's negative chunk coordinates.
    static int floorChunk(int n) { return n >> 4; }
    static BlockPos chunkPos(BlockPos p) { return {floorChunk(p.x), floorChunk(p.y), floorChunk(p.z)}; }
    static std::size_t offset(BlockPos p) { return (static_cast<unsigned>(p.x) & 15u) | ((static_cast<unsigned>(p.z) & 15u) << 4) | ((static_cast<unsigned>(p.y) & 15u) << 8); }
    std::unordered_map<BlockPos, std::unique_ptr<Chunk>, PosHash> chunks;
    std::size_t blockCount{};
};
}
