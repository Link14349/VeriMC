#pragma once
#include "types.hpp"
#include <functional>
#include <istream>
#include <ostream>

namespace simulator {
class Simulator;
struct ProjectInfo {
    std::string name{"未命名电路"};
    BlockPos origin{};
};
struct ProjectFileOptions {
    std::uint64_t maxFileBytes{8ULL * 1024 * 1024 * 1024};
    std::uint64_t maxMemoryBytes{2ULL * 1024 * 1024 * 1024};
    std::uint64_t maxBlocks{2000000};
    // Called at bounded parsing/encoding checkpoints; throwing cancels the
    // operation before the candidate replaces the current world.
    std::function<void(const std::string&, std::uint64_t, std::uint64_t)> progress;
};
void writeVmcb(std::ostream& output, const Simulator& sim, const ProjectInfo& info, bool checkpoint, const ProjectFileOptions& options = {});
// Detects VMCB magic or parses the legacy JSON stream natively. On failure,
// sim is unchanged. The input must support seeking (a file or memory stream).
ProjectInfo readProjectFile(std::istream& input, Simulator& sim, const ProjectFileOptions& options = {});
}
