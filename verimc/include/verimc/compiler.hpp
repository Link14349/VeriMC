#pragma once
#include "verimc/ast.hpp"
#include <cstdint>

namespace verimc {
struct CompileOptions {
    std::filesystem::path input, projectRoot;
    std::string top;
    std::map<std::string, std::string> parameters;
    std::size_t maxNodes = 100000, maxBits = 1000000, maxInstances = 10000;
    std::size_t maxSteps = 1000000, maxDepth = 128, maxSourceBytes = 4 * 1024 * 1024;
};
nlohmann::json compileFile(const CompileOptions& options);
void validateLogicGraph(const nlohmann::json& graph);
} // namespace verimc
