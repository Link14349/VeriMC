#pragma once
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace verimc {
struct SourceSpan {
    std::string file;
    std::size_t start = 0, end = 0, line = 1, column = 1;
};
class Diagnostic : public std::runtime_error {
  public:
    std::string code, sourceSha256, instance;
    SourceSpan span;
    std::vector<SourceSpan> related;
    Diagnostic(std::string code, const SourceSpan& span, std::string message);
};
} // namespace verimc
