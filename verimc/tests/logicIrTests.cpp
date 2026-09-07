#include "irFixtures.hpp"
#include <iostream>

using namespace verimc;
using namespace verimc::test;
namespace {
// Independent tiny consumer: follows IR references directly, with no text/file representation.
bool evaluate(const LogicGraph& g, NodeId id, bool input) {
    const auto& n = g.nodes.at(id);
    switch (n.op) {
    case NodeKind::Input:
        return input;
    case NodeKind::Constant:
        return std::get<ConstantAttributes>(n.attrs).value != 0;
    case NodeKind::Xor:
        return evaluate(g, n.inputs.at(0), input) != evaluate(g, n.inputs.at(1), input);
    case NodeKind::Signal:
        for (auto& edge : g.connections)
            if (edge.target == id)
                return evaluate(g, edge.source, input);
        break;
    default:
        break;
    }
    throw std::runtime_error("Unexpected test operation");
}
void graphPass() {
    auto original = gate();
    validateLogicGraph(original);
    auto rewritten = original;
    // A consumer can perform x ^ 0 -> x by redirecting an edge in memory.
    rewritten.connections.at(0).source = 0;
    validateLogicGraph(rewritten);
    for (bool input : {false, true}) {
        require(evaluate(original, 2, input) == input, "Xor truth table");
        require(evaluate(rewritten, 2, input) == input, "Rewrite changed result");
    }
    require(original.connections.at(0).source == 3, "Graph copy shared mutable connections");
    auto cycle = original;
    cycle.nodes.at(3).inputs.at(0) = 2;
    rejected([&] { validateLogicGraph(cycle); }, "ECombinationalCycle");
    auto negative = original;
    negative.nodes.at(3).inputs.at(0) = invalidNodeId;
    rejected([&] { validateLogicGraph(negative); });
    auto wrongAttrs = original;
    wrongAttrs.nodes.at(3).attrs = SliceAttributes{0};
    rejected([&] { validateLogicGraph(wrongAttrs); });
    auto dangling = original;
    dangling.connections.at(0).source = 99;
    rejected([&] { validateLogicGraph(dangling); });
    auto overlap = original;
    overlap.connections.push_back(overlap.connections.front());
    rejected([&] { validateLogicGraph(overlap); });
    auto missing = original;
    missing.connections.clear();
    rejected([&] { validateLogicGraph(missing); });
    auto invalidTag = original;
    invalidTag.nodes.at(3).op = static_cast<NodeKind>(1000);
    rejected([&] { validateLogicGraph(invalidTag); });
    auto invalidDirection = original;
    invalidDirection.ports.at(0).direction = static_cast<PortDirection>(1000);
    rejected([&] { validateLogicGraph(invalidDirection); });
}
void registerState() {
    auto g = graph();
    auto bit = scalar(TypeKind::Bit, 1);
    g.nodes = {
        node(NodeKind::Input, scalar(TypeKind::Clock, 1), {}, SignalAttributes{"clk", SignalRole::Input}),
        node(NodeKind::Input, bit, {}, SignalAttributes{"reset", SignalRole::Input}),
        node(NodeKind::Signal, bit, {}, SignalAttributes{"y", SignalRole::Output}),
        node(NodeKind::Register, bit, {3, 0, 1}, RegisterAttributes{"saved", 1})};
    g.ports = {{"clk", PortDirection::Input, 0},
               {"reset", PortDirection::Input, 1},
               {"y", PortDirection::Output, 2}};
    g.connections = {{2, 0, 3, 0, 1, bit, origin()}};
    validateLogicGraph(g); // Holding current state is legal feedback through a register.
    require(g.nodes.at(3).registerInput(RegisterInput::Next) == 3, "Register next value");
    require(g.nodes.at(3).registerInput(RegisterInput::Clock) == 0, "Register clock");
    auto bad = g;
    bad.nodes.at(3).inputs.at(1) = 1;
    rejected([&] { validateLogicGraph(bad); });
    bad = g;
    std::get<RegisterAttributes>(bad.nodes.at(3).attrs).resetValue = 2;
    rejected([&] { validateLogicGraph(bad); });
    bad = g;
    bad.nodes.at(3).inputs.pop_back();
    rejected([&] { validateLogicGraph(bad); });
}
void typeAndConstants() {
    auto g = graph();
    Integer large = (Integer(1) << 256) + 7;
    g.nodes = {node(NodeKind::Constant, scalar(TypeKind::UInt, 257), {}, ConstantAttributes{large}),
               node(NodeKind::Constant, scalar(TypeKind::Int, 257), {}, ConstantAttributes{-large + 8})};
    validateLogicGraph(g);
    auto bad = g;
    std::get<ConstantAttributes>(bad.nodes.at(0).attrs).value = Integer(1) << 257;
    rejected([&] { validateLogicGraph(bad); });
    bad = g;
    bad.nodes.at(0).type.kind = TypeKind::Integer;
    rejected([&] { validateLogicGraph(bad); });
    LogicType choice = scalar(TypeKind::Enum, 2);
    choice.identity = "tests::Choice";
    choice.members = {"a", "b", "c"};
    LogicType arrayType;
    arrayType.kind = TypeKind::Array;
    arrayType.length = 2;
    arrayType.element = std::make_shared<const LogicType>(choice);
    g.nodes = {node(NodeKind::Constant, arrayType, {}, ConstantAttributes{9})}; // [b, c]
    validateLogicGraph(g);
    auto copy = g;
    copy.nodes.at(0).type.element = std::make_shared<const LogicType>(scalar(TypeKind::Bits, 2));
    require(g.nodes.at(0).type.element->kind == TypeKind::Enum, "Array element replacement changed original");
    bad = g;
    std::get<ConstantAttributes>(bad.nodes.at(0).attrs).value = 15;
    rejected([&] { validateLogicGraph(bad); });
    bad = g;
    bad.nodes.at(0).type.element.reset();
    rejected([&] { validateLogicGraph(bad); });
    bad = g;
    bad.nodes.at(0).type.element = std::make_shared<const LogicType>(arrayType);
    rejected([&] { validateLogicGraph(bad); });
    bad = g;
    bad.nodes.at(0).type.length = std::numeric_limits<std::size_t>::max();
    rejected([&] { validateLogicGraph(bad); });
}
} // namespace
int main() {
    try {
        graphPass();
        registerState();
        typeAndConstants();
        std::cout << "IR consumers and validation passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
