#pragma once
#include "verimc/logicGraph.hpp"
#include <functional>

#if defined(NLOHMANN_JSON_VERSION_MAJOR) || defined(ANTLR4CPP_PUBLIC)
#error "IR public headers must not pull JSON or ANTLR into consumers"
#endif

namespace verimc::test {
inline void require(bool condition, const std::string& message) {
    if (!condition)
        throw std::runtime_error(message);
}
inline void rejected(const std::function<void()>& action, const std::string& code = "EArtifactInvalid") {
    try {
        action();
    } catch (const Diagnostic& error) {
        require(error.code == code, "Wrong diagnostic: " + error.code);
        return;
    }
    throw std::runtime_error("Expected rejection: " + code);
}
inline LogicType scalar(TypeKind kind, std::size_t width) {
    LogicType type;
    type.kind = kind;
    type.width = width;
    return type;
}
inline SourceSpan origin() {
    return {"test.vmc", 0, 1, 1, 1};
}
inline LogicGraph graph() {
    LogicGraph g;
    g.top = "tests::Gate";
    g.sources.push_back({"test.vmc", std::string(64, '0'), 1});
    g.instances.push_back({"Gate", "tests::Gate", {}, origin()});
    return g;
}
inline LogicNode node(NodeKind op, LogicType type, std::vector<NodeId> inputs, NodeAttributes attrs = {}) {
    return {op, std::move(type), std::move(inputs), std::move(attrs), origin(), "Gate"};
}
inline LogicGraph gate() {
    auto g = graph();
    auto bit = scalar(TypeKind::Bit, 1);
    g.nodes = {node(NodeKind::Input, bit, {}, SignalAttributes{"a", SignalRole::Input}),
               node(NodeKind::Constant, bit, {}, ConstantAttributes{0}),
               node(NodeKind::Signal, bit, {}, SignalAttributes{"y", SignalRole::Output}),
               node(NodeKind::Xor, bit, {0, 1})};
    g.ports = {{"a", PortDirection::Input, 0}, {"y", PortDirection::Output, 2}};
    g.connections = {{2, 0, 3, 0, 1, bit, origin()}};
    return g;
}
} // namespace verimc::test
