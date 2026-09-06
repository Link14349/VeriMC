#pragma once
#include "types.hpp"
#include <memory>
#include <unordered_map>
#include <vector>

namespace simulator {
struct Cell { BlockPos pos; StateId state; };
class World {
public:
    StateId get(BlockPos pos) const;
    StateId set(BlockPos pos, StateId state);
    std::vector<Cell> cells() const;
    std::size_t size() const { return blockCount; }
    std::size_t chunkCount() const { return chunks.size(); }
    std::size_t storageBytes() const { return chunks.size() * sizeof(Chunk); }
    void clear() { chunks.clear(); blockCount = 0; }
private:
    struct Chunk { std::array<StateId, 4096> states{}; std::uint32_t count{}; };
    static int floorChunk(int n) { return n >= 0 ? n / 16 : (n + 1) / 16 - 1; }
    static BlockPos chunkPos(BlockPos p) { return {floorChunk(p.x), floorChunk(p.y), floorChunk(p.z)}; }
    static std::size_t offset(BlockPos p) { return (static_cast<unsigned>(p.x) & 15u) | ((static_cast<unsigned>(p.z) & 15u) << 4) | ((static_cast<unsigned>(p.y) & 15u) << 8); }
    std::unordered_map<BlockPos, std::unique_ptr<Chunk>, PosHash> chunks;
    std::size_t blockCount{};
};
}
