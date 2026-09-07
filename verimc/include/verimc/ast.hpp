#pragma once
#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace verimc {
struct SourceSpan {
    std::string file;
    std::size_t start = 0, end = 0, line = 1, column = 1;
};
nlohmann::json toJson(const SourceSpan& span);
class Diagnostic : public std::runtime_error {
  public:
    std::string code, sourceSha256, instance;
    SourceSpan span;
    std::vector<SourceSpan> related;
    nlohmann::json json() const;
    Diagnostic(std::string code, const SourceSpan& span, std::string message);
};
struct Expr;
using ExprPtr = std::shared_ptr<const Expr>;
struct Expr {
    std::string kind, text;
    std::vector<ExprPtr> args;
    SourceSpan span;
};
struct TypeSyntax {
    std::string name;
    ExprPtr size;
    std::shared_ptr<TypeSyntax> element;
    SourceSpan span;
};
struct Parameter {
    std::string name;
    TypeSyntax type;
    ExprPtr value;
    SourceSpan span;
};
struct ModuleRef {
    std::string name;
    std::vector<std::pair<std::string, ExprPtr>> args;
    SourceSpan span;
};
struct Statement {
    std::string kind, name, indexName;
    TypeSyntax type;
    ModuleRef module;
    std::vector<ExprPtr> exprs;
    std::vector<Statement> body, otherwise;
    bool hasElse = false;
    SourceSpan span;
};
struct Declaration {
    std::string kind, name;
    std::vector<Parameter> parameters;
    std::vector<Statement> body;
    std::vector<std::string> members;
    TypeSyntax type;
    ExprPtr value;
    SourceSpan span;
};
struct SourceFile {
    std::string path, package, sha256;
    std::size_t byteLength = 0;
    std::vector<std::pair<std::string, std::string>> imports;
    std::vector<Declaration> declarations;
};
SourceFile parseSource(const std::string& path, const std::string& text, std::size_t maxDepth = 256);
std::string sha256(const std::string& text);
} // namespace verimc
