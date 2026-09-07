#pragma once
#include "verimc/logicGraph.hpp"
#include <cstdint>
#include <filesystem>

namespace verimc {
struct CompileOptions : GraphLimits {
    std::filesystem::path input, projectRoot;
    std::string top;
    std::map<std::string, std::string> parameters;
};
LogicGraph compileFile(const CompileOptions& options);
} // namespace verimc
