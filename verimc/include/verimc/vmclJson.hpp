#pragma once
#include "verimc/logicGraph.hpp"
#include <string_view>

namespace verimc {
// .vmcl v1 adapter. Both entry points validate the IR; format details stay in the implementation.
std::string writeVmclJson(const LogicGraph& graph);
LogicGraph readVmclJson(std::string_view text);
std::string writeDiagnosticJson(const Diagnostic& diagnostic);
} // namespace verimc
