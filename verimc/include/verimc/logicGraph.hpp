#pragma once
#include "verimc/diagnostic.hpp"
#include <boost/multiprecision/cpp_int.hpp>
#include <cstdint>
#include <map>
#include <memory>
#include <variant>

namespace verimc {
using Integer = boost::multiprecision::cpp_int;
using NodeId = std::int32_t;
inline constexpr NodeId invalidNodeId = -1;
inline constexpr std::size_t maxLogicWidth = 4096, maxLogicArrayLength = 65536, maxIntegerBits = 65536;

enum class TypeKind { Bit, Bits, UInt, Int, Level, Clock, Array, Enum, Nat, Integer };
// Nat and Integer are frontend constant types; neither may occur in a LogicGraph.
struct LogicType {
    TypeKind kind = TypeKind::Integer;
    std::string identity;
    std::size_t width = 0, length = 0;
    std::vector<std::string> members;
    // Shared element descriptions are immutable, so copying a graph does not alias mutable types.
    std::shared_ptr<const LogicType> element;
    bool compileTime() const;
    bool number() const;
    bool word() const;
    bool digital() const;
    std::size_t bits() const;
    bool operator==(const LogicType& other) const;
    std::string describe() const;
};
std::string typeKindName(TypeKind kind);
TypeKind typeKindFromName(const std::string& name);

enum class NodeKind {
    Input,
    Signal,
    Register,
    Constant,
    Slice,
    ShiftLeft,
    ShiftRight,
    Mux,
    Add,
    Sub,
    WrapAdd,
    WrapSub,
    And,
    Or,
    Xor,
    LogicalAnd,
    LogicalOr,
    Eq,
    Ne,
    Lt,
    Le,
    Gt,
    Ge,
    BitNot,
    LogicalNot,
    Neg,
    Reinterpret,
    Widen,
    Low,
    Concat,
    Array
};
std::string nodeKindName(NodeKind kind);
NodeKind nodeKindFromName(const std::string& name);
enum class SignalRole { Input, Output, Wire };
std::string signalRoleName(SignalRole role);
SignalRole signalRoleFromName(const std::string& name);
struct SignalAttributes {
    std::string name;
    SignalRole role = SignalRole::Wire;
};
struct RegisterAttributes {
    std::string name;
    Integer resetValue = 0;
};
struct ConstantAttributes {
    Integer value = 0;
};
struct SliceAttributes {
    std::size_t offset = 0;
};
struct ShiftAttributes {
    std::size_t amount = 0;
};
using NodeAttributes = std::variant<std::monostate, SignalAttributes, RegisterAttributes, ConstantAttributes,
                                    SliceAttributes, ShiftAttributes>;
// Register inputs are next value, clock, reset. The output is the current state.
// Rising edge, synchronous active-high reset, simultaneous commit, initially invalid.
enum class RegisterInput : std::size_t { Next, Clock, Reset };
struct LogicNode {
    NodeKind op = NodeKind::Constant;
    LogicType type;
    std::vector<NodeId> inputs;
    NodeAttributes attrs;
    SourceSpan span;
    std::string instance;
    NodeId registerInput(RegisterInput input) const;
};
struct LogicConnection {
    NodeId target = invalidNodeId;
    std::size_t targetOffset = 0;
    NodeId source = invalidNodeId;
    std::size_t sourceOffset = 0, width = 0;
    LogicType type;
    SourceSpan span;
};
enum class PortDirection { Input, Output };
struct LogicPort {
    std::string name;
    PortDirection direction = PortDirection::Input;
    NodeId node = invalidNodeId;
};
struct LogicSource {
    std::string path, sha256;
    std::size_t byteLength = 0;
};
struct LogicInstance {
    std::string path, module;
    std::map<std::string, Integer> parameters;
    SourceSpan span;
};
struct GraphLimits {
    std::size_t maxNodes = 100000, maxBits = 1000000, maxInstances = 10000;
    std::size_t maxSteps = 1000000, maxDepth = 128, maxSourceBytes = 4 * 1024 * 1024;
};
struct LogicGraph {
    std::string languageVersion = "0.1", compilerVersion = "0.1.0", top;
    std::vector<LogicSource> sources;
    std::vector<LogicInstance> instances;
    std::vector<LogicPort> ports;
    // NodeId is the vector index. Append preserves IDs; erasing/reordering requires remapping all refs.
    std::vector<LogicNode> nodes;
    std::vector<LogicConnection> connections;
    GraphLimits limits;
};
// Validates the in-memory graph directly; never invokes a serializer or parser.
void validateLogicGraph(const LogicGraph& graph);
} // namespace verimc
