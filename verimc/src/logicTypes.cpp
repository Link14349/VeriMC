#include "verimc/logicGraph.hpp"
#include <limits>

namespace verimc {
std::string typeKindName(TypeKind kind) {
    switch (kind) {
    case TypeKind::Bit:
        return "bit";
    case TypeKind::Bits:
        return "bits";
    case TypeKind::UInt:
        return "uint";
    case TypeKind::Int:
        return "int";
    case TypeKind::Level:
        return "level";
    case TypeKind::Clock:
        return "clock";
    case TypeKind::Array:
        return "array";
    case TypeKind::Enum:
        return "enum";
    case TypeKind::Nat:
        return "nat";
    case TypeKind::Integer:
        return "integer";
    }
    throw Diagnostic("EArtifactInvalid", {}, "Invalid TypeKind");
}
TypeKind typeKindFromName(const std::string& name) {
    if (name == "bit")
        return TypeKind::Bit;
    if (name == "bits")
        return TypeKind::Bits;
    if (name == "uint")
        return TypeKind::UInt;
    if (name == "int")
        return TypeKind::Int;
    if (name == "level")
        return TypeKind::Level;
    if (name == "clock")
        return TypeKind::Clock;
    if (name == "array")
        return TypeKind::Array;
    if (name == "enum")
        return TypeKind::Enum;
    if (name == "nat")
        return TypeKind::Nat;
    if (name == "integer")
        return TypeKind::Integer;
    throw Diagnostic("EArtifactInvalid", {}, "Unknown TypeKind: " + name);
}
std::string nodeKindName(NodeKind kind) {
    switch (kind) {
    case NodeKind::Input:
        return "input";
    case NodeKind::Signal:
        return "signal";
    case NodeKind::Register:
        return "register";
    case NodeKind::Constant:
        return "constant";
    case NodeKind::Slice:
        return "slice";
    case NodeKind::ShiftLeft:
        return "shiftLeft";
    case NodeKind::ShiftRight:
        return "shiftRight";
    case NodeKind::Mux:
        return "mux";
    case NodeKind::Add:
        return "add";
    case NodeKind::Sub:
        return "sub";
    case NodeKind::WrapAdd:
        return "wrapAdd";
    case NodeKind::WrapSub:
        return "wrapSub";
    case NodeKind::And:
        return "and";
    case NodeKind::Or:
        return "or";
    case NodeKind::Xor:
        return "xor";
    case NodeKind::LogicalAnd:
        return "logicalAnd";
    case NodeKind::LogicalOr:
        return "logicalOr";
    case NodeKind::Eq:
        return "eq";
    case NodeKind::Ne:
        return "ne";
    case NodeKind::Lt:
        return "lt";
    case NodeKind::Le:
        return "le";
    case NodeKind::Gt:
        return "gt";
    case NodeKind::Ge:
        return "ge";
    case NodeKind::BitNot:
        return "bitNot";
    case NodeKind::LogicalNot:
        return "logicalNot";
    case NodeKind::Neg:
        return "neg";
    case NodeKind::Reinterpret:
        return "reinterpret";
    case NodeKind::Widen:
        return "widen";
    case NodeKind::Low:
        return "low";
    case NodeKind::Concat:
        return "concat";
    case NodeKind::Array:
        return "array";
    }
    throw Diagnostic("EArtifactInvalid", {}, "Invalid NodeKind");
}
NodeKind nodeKindFromName(const std::string& name) {
    if (name == "input")
        return NodeKind::Input;
    if (name == "signal")
        return NodeKind::Signal;
    if (name == "register")
        return NodeKind::Register;
    if (name == "constant")
        return NodeKind::Constant;
    if (name == "slice")
        return NodeKind::Slice;
    if (name == "shiftLeft")
        return NodeKind::ShiftLeft;
    if (name == "shiftRight")
        return NodeKind::ShiftRight;
    if (name == "mux")
        return NodeKind::Mux;
    if (name == "add")
        return NodeKind::Add;
    if (name == "sub")
        return NodeKind::Sub;
    if (name == "wrapAdd")
        return NodeKind::WrapAdd;
    if (name == "wrapSub")
        return NodeKind::WrapSub;
    if (name == "and")
        return NodeKind::And;
    if (name == "or")
        return NodeKind::Or;
    if (name == "xor")
        return NodeKind::Xor;
    if (name == "logicalAnd")
        return NodeKind::LogicalAnd;
    if (name == "logicalOr")
        return NodeKind::LogicalOr;
    if (name == "eq")
        return NodeKind::Eq;
    if (name == "ne")
        return NodeKind::Ne;
    if (name == "lt")
        return NodeKind::Lt;
    if (name == "le")
        return NodeKind::Le;
    if (name == "gt")
        return NodeKind::Gt;
    if (name == "ge")
        return NodeKind::Ge;
    if (name == "bitNot")
        return NodeKind::BitNot;
    if (name == "logicalNot")
        return NodeKind::LogicalNot;
    if (name == "neg")
        return NodeKind::Neg;
    if (name == "reinterpret")
        return NodeKind::Reinterpret;
    if (name == "widen")
        return NodeKind::Widen;
    if (name == "low")
        return NodeKind::Low;
    if (name == "concat")
        return NodeKind::Concat;
    if (name == "array")
        return NodeKind::Array;
    throw Diagnostic("EArtifactInvalid", {}, "Unknown NodeKind: " + name);
}
std::string signalRoleName(SignalRole kind) {
    switch (kind) {
    case SignalRole::Input:
        return "input";
    case SignalRole::Output:
        return "output";
    case SignalRole::Wire:
        return "wire";
    }
    throw Diagnostic("EArtifactInvalid", {}, "Invalid SignalRole");
}
SignalRole signalRoleFromName(const std::string& name) {
    if (name == "input")
        return SignalRole::Input;
    if (name == "output")
        return SignalRole::Output;
    if (name == "wire")
        return SignalRole::Wire;
    throw Diagnostic("EArtifactInvalid", {}, "Unknown SignalRole: " + name);
}
bool LogicType::compileTime() const {
    return kind == TypeKind::Nat || kind == TypeKind::Integer;
}
bool LogicType::number() const {
    return kind == TypeKind::UInt || kind == TypeKind::Int;
}
bool LogicType::word() const {
    return number() || kind == TypeKind::Bits;
}
bool LogicType::digital() const {
    return word() || kind == TypeKind::Bit;
}
std::size_t LogicType::bits() const {
    if (kind != TypeKind::Array)
        return width;
    if (!element || element->kind == TypeKind::Array ||
        (element->width && length > std::numeric_limits<std::size_t>::max() / element->width))
        throw Diagnostic("EArtifactInvalid", {}, "Invalid array layout");
    return length * element->width;
}
bool LogicType::operator==(const LogicType& other) const {
    return kind == other.kind && width == other.width && identity == other.identity &&
           length == other.length && members == other.members &&
           ((!element && !other.element) || (element && other.element && *element == *other.element));
}
std::string LogicType::describe() const {
    if (kind == TypeKind::Array)
        return element ? "array<" + element->describe() + ", " + std::to_string(length) + ">" : "array<?>";
    if (kind == TypeKind::Enum)
        return identity;
    return typeKindName(kind) + (word() ? "<" + std::to_string(width) + ">" : "");
}
NodeId LogicNode::registerInput(RegisterInput input) const {
    if (op != NodeKind::Register || static_cast<std::size_t>(input) >= 3)
        throw Diagnostic("EArtifactInvalid", span, "Expected register input");
    return inputs.at(static_cast<std::size_t>(input));
}
} // namespace verimc
