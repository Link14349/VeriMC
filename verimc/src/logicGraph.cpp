#include "verimc/logicGraph.hpp"
#include <algorithm>
#include <deque>
#include <filesystem>
#include <limits>
#include <optional>
#include <set>

namespace verimc {
namespace {
[[noreturn]] void invalid(const std::string& message) {
    throw Diagnostic("EArtifactInvalid", {}, message);
}
std::size_t count(std::size_t value, std::size_t limit) {
    if (value > limit)
        invalid("Invalid bounded integer");
    return value;
}
std::size_t nodeIndex(NodeId id, std::size_t size) {
    if (id < 0 || static_cast<std::size_t>(id) >= size)
        invalid("Dangling node reference");
    return static_cast<std::size_t>(id);
}
template <class T> const T& attributes(const NodeAttributes& value) {
    if (!std::holds_alternative<T>(value))
        invalid("Wrong operation attributes");
    return std::get<T>(value);
}
std::size_t typeBits(const LogicType& t, bool element = false) {
    typeKindName(t.kind); // Reject invalid enum tags in programmatically constructed graphs.
    if (t.kind == TypeKind::Array) {
        if (element || t.width != 0 || !t.element || t.length == 0 || t.length > maxLogicArrayLength ||
            !t.identity.empty() || !t.members.empty() || t.element->kind == TypeKind::Clock)
            invalid("Invalid array type");
        return t.length * typeBits(*t.element, true);
    }
    if (t.element || t.length != 0)
        invalid("Unexpected element type");
    if (t.kind == TypeKind::Enum) {
        if (t.identity.empty() || t.members.size() < 2 || t.members.size() > maxLogicArrayLength)
            invalid("Invalid enum");
        std::set<std::string> members(t.members.begin(), t.members.end());
        if (members.size() != t.members.size())
            invalid("Duplicate enum member");
        std::size_t width = 1;
        while ((std::size_t(1) << width) < members.size())
            ++width;
        if (t.width != width)
            invalid("Incorrect enum width");
        return width;
    }
    if (!t.identity.empty() || !t.members.empty())
        invalid("Unexpected enum metadata");
    if (t.width == 0 || t.width > maxLogicWidth || t.compileTime())
        invalid("Invalid hardware type");
    if ((t.kind == TypeKind::Bit || t.kind == TypeKind::Clock) && t.width != 1)
        invalid("Scalar width");
    if (t.kind == TypeKind::Level && t.width != 4)
        invalid("Level width");
    return t.width;
}
bool acceptsRange(const LogicType& container, const LogicType& view, std::size_t offset, std::size_t width) {
    if (offset == 0 && container == view)
        return true;
    if (container.word())
        return (view.kind == TypeKind::Bit && width == 1) ||
               (view.kind == TypeKind::Bits && width == view.width);
    if (container.kind == TypeKind::Array) {
        auto elementWidth = typeBits(*container.element);
        return offset % elementWidth + width <= elementWidth &&
               acceptsRange(*container.element, view, offset % elementWidth, width);
    }
    return false;
}
void constantRange(const Integer& x, const LogicType& type) {
    auto width = typeBits(type);
    Integer lo = 0, hi = (Integer(1) << width) - 1;
    if (type.kind == TypeKind::Int) {
        lo = -(Integer(1) << (width - 1));
        hi = (Integer(1) << (width - 1)) - 1;
    }
    if (type.kind == TypeKind::Enum)
        hi = type.members.size() - 1;
    if (x < lo || x > hi)
        invalid("Constant out of range");
    if (type.kind == TypeKind::Array && type.element->kind == TypeKind::Enum) {
        auto w = typeBits(*type.element);
        Integer mask = (Integer(1) << w) - 1;
        for (std::size_t i = 0; i < type.length; ++i)
            if (((x >> (i * w)) & mask) >= type.element->members.size())
                invalid("Invalid enum encoding in array");
    }
}
} // namespace
void validateLogicGraph(const LogicGraph& g) {
    if (g.languageVersion != "0.1")
        invalid("Unsupported language version");
    auto& limits = g.limits;
    for (auto value : {limits.maxNodes, limits.maxBits, limits.maxInstances, limits.maxSteps, limits.maxDepth,
                       limits.maxSourceBytes})
        if (value == 0 || value > 1000000000)
            invalid("Invalid compiler budget");
    auto& ns = g.nodes;
    auto& es = g.connections;
    if (ns.size() > count(limits.maxNodes, 1000000) ||
        g.instances.size() > count(limits.maxInstances, 1000000))
        invalid("Graph exceeds declared budget");
    if (ns.size() > 1000000 || es.size() > 10000000 || g.instances.size() > 100000)
        invalid("Graph collection budget exceeded");
    std::vector<std::size_t> widths, base{0};
    std::vector<std::vector<std::size_t>> inputs;
    std::map<std::string, std::size_t> sourceFiles;
    for (auto& source : g.sources) {
        auto& path = source.path;
        auto& hash = source.sha256;
        std::filesystem::path filePath(path);
        if (path.empty() || filePath.is_absolute() || path != filePath.lexically_normal().generic_string() ||
            *filePath.begin() == ".." ||
            !sourceFiles.emplace(path, count(source.byteLength, 64 * 1024 * 1024)).second ||
            hash.size() != 64)
            invalid("Invalid source identity");
        for (char c : hash)
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
                invalid("Invalid SHA-256");
    }
    auto span = [&](const SourceSpan& s) {
        if (!sourceFiles.contains(s.file))
            invalid("Unknown source path");
        if (s.start > s.end || s.end > sourceFiles.at(s.file) || s.line == 0 || s.column == 0 ||
            s.line > 64 * 1024 * 1024 || s.column > 64 * 1024 * 1024)
            invalid("Invalid source span");
    };
    std::set<std::string> instancePaths;
    for (auto& instance : g.instances) {
        span(instance.span);
        if (!instancePaths.insert(instance.path).second)
            invalid("Duplicate instance");
        for (auto& [key, value] : instance.parameters) {
            Integer magnitude = value < 0 ? -value : value;
            if (magnitude != 0 && boost::multiprecision::msb(magnitude) >= maxIntegerBits)
                invalid("Parameter exceeds integer budget");
        }
    }
    for (auto& n : ns) {
        span(n.span);
        auto width = typeBits(n.type);
        if (width > 10000000 - base.back())
            invalid("Graph bit budget exceeded");
        widths.push_back(width);
        base.push_back(base.back() + width);
        if (base.back() > count(limits.maxBits, 10000000))
            invalid("Graph exceeds declared bit budget");
        std::vector<std::size_t> refs;
        for (auto input : n.inputs)
            refs.push_back(nodeIndex(input, ns.size()));
        inputs.push_back(std::move(refs));
        nodeKindName(n.op);
    }
    for (std::size_t id = 0; id < ns.size(); ++id) {
        auto& n = ns[id];
        auto op = n.op;
        auto& t = n.type;
        auto& a = n.attrs;
        auto& in = inputs[id];
        auto arity = [&](std::size_t size) {
            if (in.size() != size)
                invalid("Wrong operation arity: " + nodeKindName(op));
        };
        auto same = [&](std::size_t i) {
            if (ns[in.at(i)].type != t)
                invalid("Operation type mismatch: " + nodeKindName(op));
        };
        auto ti = [&](std::size_t i) -> const LogicType& { return ns[in.at(i)].type; };
        if (op == NodeKind::Constant) {
            arity(0);
            attributes<ConstantAttributes>(a);
            if (t.kind == TypeKind::Clock)
                invalid("Clock constant");
            constantRange(std::get<ConstantAttributes>(a).value, t);
        } else if (op == NodeKind::Input || op == NodeKind::Signal) {
            arity(0);
            auto& signal = attributes<SignalAttributes>(a);
            signalRoleName(signal.role);
            if (op == NodeKind::Input && signal.role != SignalRole::Input)
                invalid("Invalid input role");
        } else if (op == NodeKind::Register) {
            arity(3);
            auto& reg = attributes<RegisterAttributes>(a);
            same(0);
            if (ti(1).kind != TypeKind::Clock || ti(2).kind != TypeKind::Bit || t.kind == TypeKind::Clock ||
                t.kind == TypeKind::Level ||
                (t.kind == TypeKind::Array && t.element->kind == TypeKind::Level))
                invalid("Invalid register signature");
            constantRange(reg.resetValue, t);
        } else if (op == NodeKind::Slice) {
            arity(1);
            attributes<SliceAttributes>(a);
            auto off = count(std::get<SliceAttributes>(a).offset, widths[in[0]]);
            if (widths[id] > widths[in[0]] - off || ti(0).kind == TypeKind::Clock ||
                !acceptsRange(ti(0), t, off, widths[id]))
                invalid("Slice out of range");
        } else if (op == NodeKind::ShiftLeft || op == NodeKind::ShiftRight) {
            arity(1);
            attributes<ShiftAttributes>(a);
            same(0);
            if (!t.word())
                invalid("Invalid shift type");
            count(std::get<ShiftAttributes>(a).amount, widths[id]);
        } else {
            attributes<std::monostate>(a);
            if (op == NodeKind::Mux) {
                arity(3);
                same(1);
                same(2);
                if (ti(0).kind != TypeKind::Bit || t.kind == TypeKind::Clock || t.kind == TypeKind::Level ||
                    (t.kind == TypeKind::Array && t.element->kind == TypeKind::Level))
                    invalid("Invalid mux signature");
            } else if (op == NodeKind::Add || op == NodeKind::Sub) {
                arity(2);
                if (!ti(0).number() || ti(0) != ti(1) || !t.number() ||
                    t.kind != (op == NodeKind::Sub ? TypeKind::Int : ti(0).kind) ||
                    widths[id] != widths[in[0]] + 1)
                    invalid("Invalid arithmetic signature");
            } else if (op == NodeKind::WrapAdd || op == NodeKind::WrapSub) {
                arity(2);
                same(0);
                same(1);
                if (!t.number())
                    invalid("Invalid wrap signature");
            } else if (op == NodeKind::And || op == NodeKind::Or || op == NodeKind::Xor ||
                       op == NodeKind::LogicalAnd || op == NodeKind::LogicalOr) {
                arity(2);
                same(0);
                same(1);
                if (!t.digital() ||
                    ((op == NodeKind::LogicalAnd || op == NodeKind::LogicalOr) && t.kind != TypeKind::Bit))
                    invalid("Invalid logic signature");
            } else if (op == NodeKind::Eq || op == NodeKind::Ne || op == NodeKind::Lt || op == NodeKind::Le ||
                       op == NodeKind::Gt || op == NodeKind::Ge) {
                arity(2);
                bool eq = op == NodeKind::Eq || op == NodeKind::Ne;
                if (t.kind != TypeKind::Bit || ti(0) != ti(1) ||
                    !(eq ? (ti(0).digital() || ti(0).kind == TypeKind::Enum) : ti(0).number()))
                    invalid("Invalid comparison signature");
            } else if (op == NodeKind::BitNot || op == NodeKind::LogicalNot) {
                arity(1);
                same(0);
                if (!t.digital() || (op == NodeKind::LogicalNot && t.kind != TypeKind::Bit))
                    invalid("Invalid not signature");
            } else if (op == NodeKind::Neg) {
                arity(1);
                if (!ti(0).number() || t.kind != TypeKind::Int || widths[id] != widths[in[0]] + 1)
                    invalid("Invalid neg signature");
            } else if (op == NodeKind::Reinterpret) {
                arity(1);
                if (!t.digital() || !ti(0).digital() || widths[id] != widths[in[0]])
                    invalid("Invalid reinterpretation");
            } else if (op == NodeKind::Widen) {
                arity(1);
                if (!t.number() || t.kind != ti(0).kind || widths[id] < widths[in[0]])
                    invalid("Invalid widen");
            } else if (op == NodeKind::Low) {
                arity(1);
                if (t.kind != TypeKind::Bits || !ti(0).word() || widths[id] > widths[in[0]])
                    invalid("Invalid low");
            } else if (op == NodeKind::Concat) {
                if (in.size() < 2 || t.kind != TypeKind::Bits)
                    invalid("Invalid concat");
                std::size_t sum = 0;
                for (auto ref : in) {
                    if (ns[ref].type.kind != TypeKind::Bit && ns[ref].type.kind != TypeKind::Bits)
                        invalid("Invalid concat input");
                    sum += widths[ref];
                }
                if (sum != widths[id])
                    invalid("Concat width mismatch");
            } else if (op == NodeKind::Array) {
                if (t.kind != TypeKind::Array || in.size() != t.length)
                    invalid("Invalid array construction");
                for (auto ref : in)
                    if (ns[ref].type != *t.element)
                        invalid("Array element mismatch");
            } else
                invalid("Unknown node operation: " + nodeKindName(op));
        }
    }
    const auto missing = std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> drivers(base.back(), missing);
    for (auto& e : es) {
        span(e.span);
        auto target = nodeIndex(e.target, ns.size()), source = nodeIndex(e.source, ns.size());
        if (target == ns.size() || source == ns.size())
            invalid("Dangling edge");
        if (ns[target].op != NodeKind::Signal)
            invalid("Only a signal may have an explicit driver");
        auto to = count(e.targetOffset, widths[target]), from = count(e.sourceOffset, widths[source]),
             width = count(e.width, 10000000);
        if (width == 0 || width > widths[target] - to || width > widths[source] - from)
            invalid("Invalid connection range");
        auto& st = ns[source].type;
        auto& tt = ns[target].type;
        auto& edgeType = e.type;
        if (typeBits(edgeType) != width || st != edgeType || from != 0 || width != widths[source])
            invalid("Connection source must be a complete value of its declared type");
        if (!acceptsRange(tt, edgeType, to, width))
            invalid("Illegal connection target type or indivisible slice");
        for (std::size_t i = 0; i < width; ++i) {
            auto pos = base[target] + to + i;
            if (drivers[pos] != missing)
                invalid("Overlapping graph drivers");
            drivers[pos] = base[source] + from + i;
        }
    }
    for (std::size_t id = 0; id < ns.size(); ++id)
        if (ns[id].op == NodeKind::Signal)
            for (std::size_t bit = 0; bit < widths[id]; ++bit)
                if (drivers[base[id] + bit] == missing)
                    invalid("Undriven graph signal");
    // Kahn traversal on bit dependencies avoids both false bus-level cycles and recursive C++ DFS.
    std::vector<std::vector<std::size_t>> consumers(base.back());
    std::vector<std::size_t> indegree(base.back(), 0);
    std::size_t work = 0;
    auto edge = [&](std::size_t from, std::size_t to) {
        if (++work > 20000000)
            throw Diagnostic("EElaborationLimit", {}, "Logic dependency budget exceeded");
        consumers[from].push_back(to);
        ++indegree[to];
    };
    for (std::size_t id = 0; id < ns.size(); ++id) {
        auto op = ns[id].op;
        auto& in = inputs[id];
        auto& a = ns[id].attrs;
        for (std::size_t bit = 0; bit < widths[id]; ++bit) {
            auto out = base[id] + bit;
            if (op == NodeKind::Signal)
                edge(drivers[out], out);
            else if (op == NodeKind::Slice)
                edge(base[in[0]] + std::get<SliceAttributes>(a).offset + bit, out);
            else if (op == NodeKind::Concat || op == NodeKind::Array) {
                std::size_t offset = 0;
                for (std::size_t i = 0; i < in.size(); ++i) {
                    auto ref = op == NodeKind::Concat ? in[in.size() - 1 - i] : in[i];
                    if (bit >= offset && bit < offset + widths[ref]) {
                        edge(base[ref] + bit - offset, out);
                        break;
                    }
                    offset += widths[ref];
                }
            } else if (op == NodeKind::Register || op == NodeKind::Input || op == NodeKind::Constant)
                continue;
            else if (op == NodeKind::ShiftLeft || op == NodeKind::ShiftRight) {
                auto amount = std::get<ShiftAttributes>(a).amount;
                if (op == NodeKind::ShiftLeft) {
                    if (bit >= amount)
                        edge(base[in[0]] + bit - amount, out);
                } else if (bit + amount < widths[id])
                    edge(base[in[0]] + bit + amount, out);
                else if (ns[id].type.kind == TypeKind::Int)
                    edge(base[in[0]] + widths[in[0]] - 1, out);
            } else if (op == NodeKind::Eq || op == NodeKind::Ne || op == NodeKind::Lt || op == NodeKind::Le ||
                       op == NodeKind::Gt || op == NodeKind::Ge || op == NodeKind::Add ||
                       op == NodeKind::Sub || op == NodeKind::WrapAdd || op == NodeKind::WrapSub ||
                       op == NodeKind::Neg) {
                bool cmp = op == NodeKind::Eq || op == NodeKind::Ne || op == NodeKind::Lt ||
                           op == NodeKind::Le || op == NodeKind::Gt || op == NodeKind::Ge;
                for (auto ref : in)
                    for (std::size_t k = 0; k < (cmp ? widths[ref] : std::min(bit + 1, widths[ref])); ++k)
                        edge(base[ref] + k, out);
            } else {
                for (std::size_t i = 0; i < in.size(); ++i) {
                    auto ref = in[i];
                    if (op == NodeKind::Mux && i == 0)
                        edge(base[ref], out);
                    else if (bit < widths[ref])
                        edge(base[ref] + bit, out);
                    else if (op == NodeKind::Widen && ns[ref].type.kind == TypeKind::Int)
                        edge(base[ref] + widths[ref] - 1, out);
                }
            }
        }
    }
    std::deque<std::size_t> ready;
    for (std::size_t i = 0; i < indegree.size(); ++i)
        if (indegree[i] == 0)
            ready.push_back(i);
    std::size_t visited = 0;
    while (!ready.empty()) {
        auto n = ready.front();
        ready.pop_front();
        ++visited;
        for (auto c : consumers[n])
            if (--indegree[c] == 0)
                ready.push_back(c);
    }
    if (visited != base.back()) {
        auto bit =
            std::find_if(indegree.begin(), indegree.end(), [](auto n) { return n != 0; }) - indegree.begin();
        auto id =
            std::upper_bound(base.begin(), base.end(), static_cast<std::size_t>(bit)) - base.begin() - 1;
        auto& s = ns.at(static_cast<std::size_t>(id)).span;
        throw Diagnostic("ECombinationalCycle", s, "Combinational dependency cycle");
    }
    std::set<std::size_t> topInputs;
    std::set<std::string> portNames;
    std::size_t clocks = 0;
    std::optional<std::size_t> clock;
    for (auto& p : g.ports) {
        auto id = nodeIndex(p.node, ns.size());
        if (id == ns.size() || !portNames.insert(p.name).second)
            invalid("Invalid port");
        if (p.direction == PortDirection::Input) {
            if (ns[id].op != NodeKind::Input || !topInputs.insert(id).second)
                invalid("Input port mismatch");
            if (ns[id].type.kind == TypeKind::Clock) {
                ++clocks;
                clock = id;
            }
        } else if (p.direction != PortDirection::Output || ns[id].op != NodeKind::Signal ||
                   std::get<SignalAttributes>(ns[id].attrs).role != SignalRole::Output)
            invalid("Output port mismatch");
    }
    if (clocks > 1)
        throw Diagnostic("EClockDomain", {}, "Only one top-level clock is allowed");
    for (std::size_t id = 0; id < ns.size(); ++id) {
        if (ns[id].op == NodeKind::Input && !topInputs.contains(id))
            invalid("Hidden graph input");
        if (ns[id].op == NodeKind::Register) {
            auto ref = inputs[id][1];
            std::size_t hops = 0;
            while (ns[ref].op == NodeKind::Signal) {
                if (++hops > ns.size())
                    invalid("Clock alias loop");
                auto sourceBit = drivers[base[ref]];
                ref = std::upper_bound(base.begin(), base.end(), sourceBit) - base.begin() - 1;
            }
            if (!clock || ref != *clock)
                throw Diagnostic("EClockDomain", {}, "Register clock must resolve to the unique top input");
        }
    }
}
} // namespace verimc
