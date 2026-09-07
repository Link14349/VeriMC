#include "verimc/compiler.hpp"
#include <algorithm>
#include <boost/multiprecision/cpp_int.hpp>
#include <deque>
#include <functional>
#include <limits>
#include <optional>
#include <set>

namespace verimc {
using Json = nlohmann::json;
namespace {
[[noreturn]] void invalid(const std::string& message) {
    throw Diagnostic("EArtifactInvalid", {}, message);
}
void fields(const Json& j, const std::set<std::string>& names) {
    if (!j.is_object() || j.size() != names.size())
        invalid("Missing or extra fields");
    for (auto& [key, value] : j.items())
        if (!names.contains(key))
            invalid("Unknown field: " + key);
}
std::size_t count(const Json& j, std::size_t limit) {
    if (!j.is_number_integer() || j < 0 || j > limit)
        invalid("Invalid bounded integer");
    return j.get<std::size_t>();
}
std::size_t typeBits(const Json& t, bool element = false) {
    if (!t.is_object() || !t.contains("kind") || !t.at("kind").is_string())
        invalid("Invalid type");
    auto k = t.at("kind").get<std::string>();
    if (k == "array") {
        fields(t, {"kind", "width", "length", "element"});
        if (element || count(t.at("width"), 0) != 0)
            invalid("Invalid array type");
        auto n = count(t.at("length"), 65536);
        if (n == 0)
            invalid("Empty array");
        if (t.at("element").value("kind", "") == "clock")
            invalid("Clock array");
        return n * typeBits(t.at("element"), true);
    }
    if (k == "enum") {
        fields(t, {"kind", "width", "identity", "members"});
        if (!t.at("identity").is_string() || t.at("identity").get<std::string>().empty() ||
            !t.at("members").is_array() || t.at("members").size() < 2 || t.at("members").size() > 65536)
            invalid("Invalid enum");
        std::set<std::string> members;
        for (auto& m : t.at("members")) {
            if (!m.is_string() || !members.insert(m.get<std::string>()).second)
                invalid("Duplicate/invalid enum member");
        }
        std::size_t width = 1;
        while ((std::size_t(1) << width) < members.size())
            ++width;
        if (count(t.at("width"), 4096) != width)
            invalid("Incorrect enum width");
        return width;
    }
    fields(t, {"kind", "width"});
    auto width = count(t.at("width"), 4096);
    if (width == 0)
        invalid("Empty type");
    if (k == "bit" || k == "clock") {
        if (width != 1)
            invalid("Scalar width");
    } else if (k == "level") {
        if (width != 4)
            invalid("Level width");
    } else if (k != "bits" && k != "uint" && k != "int")
        invalid("Unknown hardware type");
    return width;
}
bool number(const Json& t) {
    return t["kind"] == "uint" || t["kind"] == "int";
}
bool word(const Json& t) {
    return number(t) || t["kind"] == "bits";
}
bool digital(const Json& t) {
    return word(t) || t["kind"] == "bit";
}
// Whether a typed view can select this range without an implicit conversion.
bool acceptsRange(const Json& container, const Json& view, std::size_t offset, std::size_t width) {
    if (offset == 0 && container == view)
        return true;
    if (word(container))
        return (view["kind"] == "bit" && width == 1) || (view["kind"] == "bits" && width == view["width"]);
    if (container["kind"] == "array") {
        auto elementWidth = typeBits(container["element"]);
        return offset % elementWidth + width <= elementWidth &&
               acceptsRange(container["element"], view, offset % elementWidth, width);
    }
    return false;
}
void constantRange(const Json& value, const Json& type) {
    if (!value.is_string())
        invalid("Constant must be decimal text");
    auto s = value.get<std::string>();
    auto begin = s.starts_with('-') ? 1u : 0u;
    if (s.size() == begin || s.size() > 130000 || s == "-0" || (s.size() > begin + 1 && s[begin] == '0'))
        invalid("Noncanonical constant");
    for (std::size_t i = begin; i < s.size(); ++i)
        if (s[i] < '0' || s[i] > '9')
            invalid("Invalid constant digit");
    using Integer = boost::multiprecision::cpp_int;
    Integer x(s);
    auto width = typeBits(type);
    Integer lo = 0, hi = (Integer(1) << width) - 1;
    if (type["kind"] == "int") {
        lo = -(Integer(1) << (width - 1));
        hi = (Integer(1) << (width - 1)) - 1;
    }
    if (type["kind"] == "enum")
        hi = type["members"].size() - 1;
    if (x < lo || x > hi)
        invalid("Constant out of range");
    if (type["kind"] == "array" && type["element"]["kind"] == "enum") {
        auto w = typeBits(type["element"]);
        Integer mask = (Integer(1) << w) - 1;
        for (std::size_t i = 0; i < type["length"].get<std::size_t>(); ++i)
            if (((x >> (i * w)) & mask) >= type["element"]["members"].size())
                invalid("Invalid enum encoding in array");
    }
}
} // namespace
void validateLogicGraph(const Json& g) {
    try {
        if (!g.is_object() || g.value("format", "") != "verimc.logic" || g.at("formatVersion") != 1)
            throw Diagnostic("EArtifactVersion", {}, "Unsupported .vmcl version");
        fields(g, {"format", "formatVersion", "languageVersion", "compilerVersion", "top", "sources",
                   "instances", "ports", "nodes", "connections", "semantics", "limits", "validation"});
        if (g["languageVersion"] != "0.1" || !g["compilerVersion"].is_string() || !g["top"].is_string())
            invalid("Invalid language/compiler identity");
        if (g["semantics"] != Json({{"clockEdge", "rising"},
                                    {"reset", "synchronousHigh"},
                                    {"stateUpdate", "simultaneous"},
                                    {"initialRegisters", "invalid"}}))
            invalid("Unsupported logic semantics");
        if (g["validation"] !=
            Json({{"stage", "logicalGraph"}, {"physicalVerified", false}, {"sourceTestsExecuted", false}}))
            invalid("Invalid validation claim");
        auto& limits = g.at("limits");
        fields(limits, {"maxNodes", "maxBits", "maxInstances", "maxSteps", "maxDepth", "maxSourceBytes",
                        "maxWidth", "maxArrayLength", "maxIntegerBits"});
        for (auto key : {"maxNodes", "maxBits", "maxInstances", "maxSteps", "maxDepth", "maxSourceBytes",
                         "maxWidth", "maxArrayLength", "maxIntegerBits"})
            if (count(limits[key], 1000000000) == 0)
                invalid("Zero compiler budget");
        if (limits["maxWidth"] != 4096 || limits["maxArrayLength"] != 65536 ||
            limits["maxIntegerBits"] != 65536)
            invalid("Unsupported type limits");
        auto& ns = g.at("nodes");
        auto& es = g.at("connections");
        if (ns.size() > count(limits["maxNodes"], 1000000) ||
            g["instances"].size() > count(limits["maxInstances"], 1000000))
            invalid("Graph exceeds declared budget");
        if (!ns.is_array() || ns.size() > 1000000 || !es.is_array() || es.size() > 10000000)
            invalid("Graph collection budget exceeded");
        std::vector<std::size_t> widths, base{0};
        std::vector<std::vector<std::size_t>> inputs;
        std::map<std::string, std::size_t> sourceFiles;
        if (!g["sources"].is_array() || !g["instances"].is_array() || g["instances"].size() > 100000)
            invalid("Invalid source/instance table");
        for (auto& source : g["sources"]) {
            fields(source, {"path", "sha256", "byteLength"});
            auto path = source.at("path").get<std::string>(), hash = source.at("sha256").get<std::string>();
            std::filesystem::path p(path);
            if (path.empty() || p.is_absolute() || path != p.lexically_normal().generic_string() ||
                *p.begin() == ".." ||
                !sourceFiles.emplace(path, count(source["byteLength"], 64 * 1024 * 1024)).second ||
                hash.size() != 64)
                invalid("Invalid source identity");
            for (char c : hash)
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
                    invalid("Invalid SHA-256");
        }
        auto span = [&](const Json& s) {
            fields(s, {"file", "start", "end", "line", "column"});
            if (!sourceFiles.contains(s.at("file").get<std::string>()))
                invalid("Unknown source path");
            if (count(s.at("start"), 64 * 1024 * 1024) >
                    count(s.at("end"), sourceFiles.at(s.at("file").get<std::string>())) ||
                count(s.at("line"), 64 * 1024 * 1024) == 0 || count(s.at("column"), 64 * 1024 * 1024) == 0)
                invalid("Invalid source span");
        };
        std::set<std::string> instancePaths;
        for (auto& instance : g["instances"]) {
            fields(instance, {"path", "module", "parameters", "source"});
            span(instance["source"]);
            if (!instance["path"].is_string() ||
                !instancePaths.insert(instance["path"].get<std::string>()).second ||
                !instance["module"].is_string() || !instance["parameters"].is_object())
                invalid("Invalid instance");
            for (auto& [key, value] : instance["parameters"].items())
                if (!value.is_string())
                    invalid("Invalid parameter");
        }
        for (std::size_t id = 0; id < ns.size(); ++id) {
            auto& n = ns[id];
            fields(n, {"id", "op", "type", "inputs", "attributes", "source", "instance"});
            if (count(n.at("id"), ns.size()) != id || !n["op"].is_string() || !n["instance"].is_string() ||
                !n["inputs"].is_array())
                invalid("Invalid node identity");
            span(n["source"]);
            auto width = typeBits(n.at("type"));
            if (width > 10000000 - base.back())
                invalid("Graph bit budget exceeded");
            widths.push_back(width);
            base.push_back(base.back() + width);
            if (base.back() > count(limits["maxBits"], 10000000))
                invalid("Graph exceeds declared bit budget");
            std::vector<std::size_t> refs;
            for (auto& input : n["inputs"]) {
                auto ref = count(input, ns.size());
                if (ref == ns.size())
                    invalid("Dangling node reference");
                refs.push_back(ref);
            }
            inputs.push_back(std::move(refs));
        }
        for (std::size_t id = 0; id < ns.size(); ++id) {
            auto& n = ns[id];
            auto op = n["op"].get<std::string>();
            auto& t = n["type"];
            auto& a = n["attributes"];
            auto& in = inputs[id];
            auto arity = [&](std::size_t size) {
                if (in.size() != size)
                    invalid("Wrong operation arity: " + op);
            };
            auto same = [&](std::size_t i) {
                if (ns[in.at(i)]["type"] != t)
                    invalid("Operation type mismatch: " + op);
            };
            auto ti = [&](std::size_t i) -> const Json& { return ns[in.at(i)]["type"]; };
            if (op == "constant") {
                arity(0);
                fields(a, {"value"});
                if (t["kind"] == "clock")
                    invalid("Clock constant");
                constantRange(a["value"], t);
            } else if (op == "input" || op == "signal") {
                arity(0);
                fields(a, {"name", "role"});
                if (!a["name"].is_string() ||
                    !(a["role"] == "input" || a["role"] == "output" || a["role"] == "wire"))
                    invalid("Invalid signal role");
                if (op == "input" && a["role"] != "input")
                    invalid("Invalid input role");
            } else if (op == "register") {
                arity(3);
                fields(a, {"name", "role", "resetValue", "initialState"});
                same(0);
                if (ti(1)["kind"] != "clock" || ti(2)["kind"] != "bit" || t["kind"] == "clock" ||
                    t["kind"] == "level" || (t["kind"] == "array" && t["element"]["kind"] == "level"))
                    invalid("Invalid register signature");
                if (a["role"] != "register" || a["initialState"] != "invalid" || !a["name"].is_string())
                    invalid("Invalid register metadata");
                constantRange(a["resetValue"], t);
            } else if (op == "slice") {
                arity(1);
                fields(a, {"offset"});
                auto off = count(a["offset"], widths[in[0]]);
                if (widths[id] > widths[in[0]] - off || ti(0)["kind"] == "clock" ||
                    !acceptsRange(ti(0), t, off, widths[id]))
                    invalid("Slice out of range");
            } else if (op == "shiftLeft" || op == "shiftRight") {
                arity(1);
                fields(a, {"amount"});
                same(0);
                if (!word(t))
                    invalid("Invalid shift type");
                count(a["amount"], widths[id]);
            } else {
                fields(a, {});
                if (op == "mux") {
                    arity(3);
                    same(1);
                    same(2);
                    if (ti(0)["kind"] != "bit" || t["kind"] == "clock" || t["kind"] == "level" ||
                        (t["kind"] == "array" && t["element"]["kind"] == "level"))
                        invalid("Invalid mux signature");
                } else if (op == "add" || op == "sub") {
                    arity(2);
                    if (!number(ti(0)) || ti(0) != ti(1) || !number(t) ||
                        t["kind"] != (op == "sub" ? Json("int") : ti(0)["kind"]) ||
                        widths[id] != widths[in[0]] + 1)
                        invalid("Invalid arithmetic signature");
                } else if (op == "wrapAdd" || op == "wrapSub") {
                    arity(2);
                    same(0);
                    same(1);
                    if (!number(t))
                        invalid("Invalid wrap signature");
                } else if (op == "and" || op == "or" || op == "xor" || op == "logicalAnd" ||
                           op == "logicalOr") {
                    arity(2);
                    same(0);
                    same(1);
                    if (!digital(t) || ((op == "logicalAnd" || op == "logicalOr") && t["kind"] != "bit"))
                        invalid("Invalid logic signature");
                } else if (op == "eq" || op == "ne" || op == "lt" || op == "le" || op == "gt" || op == "ge") {
                    arity(2);
                    bool eq = op == "eq" || op == "ne";
                    if (t["kind"] != "bit" || ti(0) != ti(1) ||
                        !(eq ? (digital(ti(0)) || ti(0)["kind"] == "enum") : number(ti(0))))
                        invalid("Invalid comparison signature");
                } else if (op == "bitNot" || op == "logicalNot") {
                    arity(1);
                    same(0);
                    if (!digital(t) || (op == "logicalNot" && t["kind"] != "bit"))
                        invalid("Invalid not signature");
                } else if (op == "neg") {
                    arity(1);
                    if (!number(ti(0)) || t["kind"] != "int" || widths[id] != widths[in[0]] + 1)
                        invalid("Invalid neg signature");
                } else if (op == "reinterpret") {
                    arity(1);
                    if (!digital(t) || !digital(ti(0)) || widths[id] != widths[in[0]])
                        invalid("Invalid reinterpretation");
                } else if (op == "widen") {
                    arity(1);
                    if (!number(t) || t["kind"] != ti(0)["kind"] || widths[id] < widths[in[0]])
                        invalid("Invalid widen");
                } else if (op == "low") {
                    arity(1);
                    if (t["kind"] != "bits" || !word(ti(0)) || widths[id] > widths[in[0]])
                        invalid("Invalid low");
                } else if (op == "concat") {
                    if (in.size() < 2 || t["kind"] != "bits")
                        invalid("Invalid concat");
                    std::size_t sum = 0;
                    for (auto ref : in) {
                        if (ns[ref]["type"]["kind"] != "bit" && ns[ref]["type"]["kind"] != "bits")
                            invalid("Invalid concat input");
                        sum += widths[ref];
                    }
                    if (sum != widths[id])
                        invalid("Concat width mismatch");
                } else if (op == "array") {
                    if (t["kind"] != "array" || in.size() != t["length"])
                        invalid("Invalid array construction");
                    for (auto ref : in)
                        if (ns[ref]["type"] != t["element"])
                            invalid("Array element mismatch");
                } else
                    invalid("Unknown node operation: " + op);
            }
        }
        const auto missing = std::numeric_limits<std::size_t>::max();
        std::vector<std::size_t> drivers(base.back(), missing);
        for (auto& e : es) {
            fields(e, {"target", "targetOffset", "source", "sourceOffset", "width", "type", "sourceSpan"});
            span(e["sourceSpan"]);
            auto target = count(e["target"], ns.size()), source = count(e["source"], ns.size());
            if (target == ns.size() || source == ns.size())
                invalid("Dangling edge");
            if (ns[target]["op"] != "signal")
                invalid("Only a signal may have an explicit driver");
            auto to = count(e["targetOffset"], widths[target]),
                 from = count(e["sourceOffset"], widths[source]), width = count(e["width"], 10000000);
            if (width == 0 || width > widths[target] - to || width > widths[source] - from)
                invalid("Invalid connection range");
            auto& st = ns[source]["type"];
            auto& tt = ns[target]["type"];
            auto& edgeType = e["type"];
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
            if (ns[id]["op"] == "signal")
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
            auto op = ns[id]["op"].get<std::string>();
            auto& in = inputs[id];
            auto& a = ns[id]["attributes"];
            for (std::size_t bit = 0; bit < widths[id]; ++bit) {
                auto out = base[id] + bit;
                if (op == "signal")
                    edge(drivers[out], out);
                else if (op == "slice")
                    edge(base[in[0]] + a["offset"].get<std::size_t>() + bit, out);
                else if (op == "concat" || op == "array") {
                    std::size_t offset = 0;
                    for (std::size_t i = 0; i < in.size(); ++i) {
                        auto ref = op == "concat" ? in[in.size() - 1 - i] : in[i];
                        if (bit >= offset && bit < offset + widths[ref]) {
                            edge(base[ref] + bit - offset, out);
                            break;
                        }
                        offset += widths[ref];
                    }
                } else if (op == "register" || op == "input" || op == "constant")
                    continue;
                else if (op == "shiftLeft" || op == "shiftRight") {
                    auto amount = a["amount"].get<std::size_t>();
                    if (op == "shiftLeft") {
                        if (bit >= amount)
                            edge(base[in[0]] + bit - amount, out);
                    } else if (bit + amount < widths[id])
                        edge(base[in[0]] + bit + amount, out);
                    else if (ns[id]["type"]["kind"] == "int")
                        edge(base[in[0]] + widths[in[0]] - 1, out);
                } else if (op == "eq" || op == "ne" || op == "lt" || op == "le" || op == "gt" || op == "ge" ||
                           op == "add" || op == "sub" || op == "wrapAdd" || op == "wrapSub" || op == "neg") {
                    bool cmp =
                        op == "eq" || op == "ne" || op == "lt" || op == "le" || op == "gt" || op == "ge";
                    for (auto ref : in)
                        for (std::size_t k = 0; k < (cmp ? widths[ref] : std::min(bit + 1, widths[ref])); ++k)
                            edge(base[ref] + k, out);
                } else {
                    for (std::size_t i = 0; i < in.size(); ++i) {
                        auto ref = in[i];
                        if (op == "mux" && i == 0)
                            edge(base[ref], out);
                        else if (bit < widths[ref])
                            edge(base[ref] + bit, out);
                        else if (op == "widen" && ns[ref]["type"]["kind"] == "int")
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
            auto bit = std::find_if(indegree.begin(), indegree.end(), [](auto n) { return n != 0; }) -
                       indegree.begin();
            auto id =
                std::upper_bound(base.begin(), base.end(), static_cast<std::size_t>(bit)) - base.begin() - 1;
            auto& s = ns.at(static_cast<std::size_t>(id))["source"];
            throw Diagnostic("ECombinationalCycle", {s["file"], s["start"], s["end"], s["line"], s["column"]},
                             "Combinational dependency cycle");
        }
        std::set<std::size_t> topInputs;
        std::set<std::string> portNames;
        std::size_t clocks = 0;
        std::optional<std::size_t> clock;
        if (!g["ports"].is_array())
            invalid("Invalid port table");
        for (auto& p : g["ports"]) {
            fields(p, {"name", "direction", "node"});
            auto id = count(p["node"], ns.size());
            if (id == ns.size() || !p["name"].is_string() ||
                !portNames.insert(p["name"].get<std::string>()).second)
                invalid("Invalid port");
            if (p["direction"] == "input") {
                if (ns[id]["op"] != "input" || !topInputs.insert(id).second)
                    invalid("Input port mismatch");
                if (ns[id]["type"]["kind"] == "clock") {
                    ++clocks;
                    clock = id;
                }
            } else if (p["direction"] != "output" || ns[id]["op"] != "signal" ||
                       ns[id]["attributes"]["role"] != "output")
                invalid("Output port mismatch");
        }
        if (clocks > 1)
            throw Diagnostic("EClockDomain", {}, "Only one top-level clock is allowed");
        for (std::size_t id = 0; id < ns.size(); ++id) {
            if (ns[id]["op"] == "input" && !topInputs.contains(id))
                invalid("Hidden graph input");
            if (ns[id]["op"] == "register") {
                auto ref = inputs[id][1];
                std::size_t hops = 0;
                while (ns[ref]["op"] == "signal") {
                    if (++hops > ns.size())
                        invalid("Clock alias loop");
                    auto sourceBit = drivers[base[ref]];
                    ref = std::upper_bound(base.begin(), base.end(), sourceBit) - base.begin() - 1;
                }
                if (!clock || ref != *clock)
                    throw Diagnostic("EClockDomain", {},
                                     "Register clock must resolve to the unique top input");
            }
        }
    } catch (const Json::exception& error) {
        invalid(std::string("Malformed .vmcl: ") + error.what());
    }
}
} // namespace verimc
