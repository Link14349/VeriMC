#include "verimc/vmclJson.hpp"
#include <nlohmann/json.hpp>
#include <set>

namespace verimc {
namespace {
using Json = nlohmann::json;
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
const Json& array(const Json& j, std::size_t limit) {
    if (!j.is_array() || j.size() > limit)
        invalid("Invalid or oversized array");
    return j;
}
Integer decimal(const Json& j, std::size_t maxDigits = 130000) {
    auto s = j.get<std::string>();
    auto begin = s.starts_with('-') ? 1u : 0u;
    if (s.size() == begin || s.size() > maxDigits || s == "-0" || (s.size() > begin + 1 && s[begin] == '0'))
        invalid("Noncanonical constant");
    for (std::size_t i = begin; i < s.size(); ++i)
        if (s[i] < '0' || s[i] > '9')
            invalid("Invalid constant digit");
    return Integer(s);
}
Json sourceJson(const SourceSpan& s) {
    return {{"file", s.file}, {"start", s.start}, {"end", s.end}, {"line", s.line}, {"column", s.column}};
}
SourceSpan sourceFromJson(const Json& s) {
    fields(s, {"file", "start", "end", "line", "column"});
    return {s.at("file").get<std::string>(), count(s.at("start"), 64 * 1024 * 1024),
            count(s.at("end"), 64 * 1024 * 1024), count(s.at("line"), 64 * 1024 * 1024),
            count(s.at("column"), 64 * 1024 * 1024)};
}
Json typeJson(const LogicType& t) {
    Json j = {{"kind", typeKindName(t.kind)}, {"width", t.width}};
    if (t.kind == TypeKind::Array) {
        j["length"] = t.length;
        j["element"] = typeJson(*t.element);
    }
    if (t.kind == TypeKind::Enum) {
        j["identity"] = t.identity;
        j["members"] = t.members;
    }
    return j;
}
LogicType typeFromJson(const Json& j, bool element = false) {
    LogicType t;
    t.kind = typeKindFromName(j.at("kind").get<std::string>());
    t.width = count(j.at("width"), maxLogicWidth);
    if (t.kind == TypeKind::Array) {
        fields(j, {"kind", "width", "length", "element"});
        if (element)
            invalid("Nested array type");
        t.length = count(j.at("length"), maxLogicArrayLength);
        t.element = std::make_shared<const LogicType>(typeFromJson(j.at("element"), true));
    } else if (t.kind == TypeKind::Enum) {
        fields(j, {"kind", "width", "identity", "members"});
        t.identity = j.at("identity").get<std::string>();
        for (auto& m : array(j.at("members"), maxLogicArrayLength))
            t.members.push_back(m.get<std::string>());
    } else
        fields(j, {"kind", "width"});
    return t;
}
Json attributesJson(const LogicNode& n) {
    switch (n.op) {
    case NodeKind::Input:
    case NodeKind::Signal: {
        auto& a = std::get<SignalAttributes>(n.attrs);
        return {{"name", a.name}, {"role", signalRoleName(a.role)}};
    }
    case NodeKind::Register: {
        auto& a = std::get<RegisterAttributes>(n.attrs);
        return {{"name", a.name},
                {"role", "register"},
                {"resetValue", a.resetValue.str()},
                {"initialState", "invalid"}};
    }
    case NodeKind::Constant:
        return {{"value", std::get<ConstantAttributes>(n.attrs).value.str()}};
    case NodeKind::Slice:
        return {{"offset", std::get<SliceAttributes>(n.attrs).offset}};
    case NodeKind::ShiftLeft:
    case NodeKind::ShiftRight:
        return {{"amount", std::get<ShiftAttributes>(n.attrs).amount}};
    default:
        return Json::object();
    }
}
NodeAttributes attributesFromJson(NodeKind op, const Json& a) {
    switch (op) {
    case NodeKind::Input:
    case NodeKind::Signal:
        fields(a, {"name", "role"});
        return SignalAttributes{a.at("name").get<std::string>(),
                                signalRoleFromName(a.at("role").get<std::string>())};
    case NodeKind::Register:
        fields(a, {"name", "role", "resetValue", "initialState"});
        if (a.at("role") != "register" || a.at("initialState") != "invalid")
            invalid("Invalid register metadata");
        return RegisterAttributes{a.at("name").get<std::string>(), decimal(a.at("resetValue"))};
    case NodeKind::Constant:
        fields(a, {"value"});
        return ConstantAttributes{decimal(a.at("value"))};
    case NodeKind::Slice:
        fields(a, {"offset"});
        return SliceAttributes{count(a.at("offset"), 10000000)};
    case NodeKind::ShiftLeft:
    case NodeKind::ShiftRight:
        fields(a, {"amount"});
        return ShiftAttributes{count(a.at("amount"), maxLogicWidth)};
    default:
        fields(a, {});
        return {};
    }
}
Json semanticsJson() {
    return {{"clockEdge", "rising"},
            {"reset", "synchronousHigh"},
            {"stateUpdate", "simultaneous"},
            {"initialRegisters", "invalid"}};
}
Json validationJson() {
    return {{"stage", "logicalGraph"}, {"physicalVerified", false}, {"sourceTestsExecuted", false}};
}
} // namespace
std::string writeVmclJson(const LogicGraph& g) {
    validateLogicGraph(g);
    Json sources = Json::array(), instances = Json::array(), ports = Json::array(), nodes = Json::array(),
         connections = Json::array();
    for (auto& s : g.sources)
        sources.push_back({{"path", s.path}, {"sha256", s.sha256}, {"byteLength", s.byteLength}});
    for (auto& i : g.instances) {
        Json parameters = Json::object();
        for (auto& [name, value] : i.parameters)
            parameters[name] = value.str();
        instances.push_back({{"path", i.path},
                             {"module", i.module},
                             {"parameters", parameters},
                             {"source", sourceJson(i.span)}});
    }
    for (auto& p : g.ports)
        ports.push_back({{"name", p.name},
                         {"direction", p.direction == PortDirection::Input ? "input" : "output"},
                         {"node", p.node}});
    for (std::size_t id = 0; id < g.nodes.size(); ++id) {
        auto& n = g.nodes[id];
        nodes.push_back({{"id", id},
                         {"op", nodeKindName(n.op)},
                         {"type", typeJson(n.type)},
                         {"inputs", n.inputs},
                         {"attributes", attributesJson(n)},
                         {"source", sourceJson(n.span)},
                         {"instance", n.instance}});
    }
    for (auto& c : g.connections)
        connections.push_back({{"target", c.target},
                               {"targetOffset", c.targetOffset},
                               {"source", c.source},
                               {"sourceOffset", c.sourceOffset},
                               {"width", c.width},
                               {"type", typeJson(c.type)},
                               {"sourceSpan", sourceJson(c.span)}});
    auto& l = g.limits;
    Json graph = {{"format", "verimc.logic"},
                  {"formatVersion", 1},
                  {"languageVersion", g.languageVersion},
                  {"compilerVersion", g.compilerVersion},
                  {"top", g.top},
                  {"sources", sources},
                  {"instances", instances},
                  {"ports", ports},
                  {"nodes", nodes},
                  {"connections", connections},
                  {"semantics", semanticsJson()},
                  {"limits",
                   {{"maxNodes", l.maxNodes},
                    {"maxBits", l.maxBits},
                    {"maxInstances", l.maxInstances},
                    {"maxSteps", l.maxSteps},
                    {"maxDepth", l.maxDepth},
                    {"maxSourceBytes", l.maxSourceBytes},
                    {"maxWidth", maxLogicWidth},
                    {"maxArrayLength", maxLogicArrayLength},
                    {"maxIntegerBits", maxIntegerBits}}},
                  {"validation", validationJson()}};
    return graph.dump(2) + "\n";
}
LogicGraph readVmclJson(std::string_view text) {
    try {
        if (text.size() > 256 * 1024 * 1024)
            invalid("Artifact exceeds 256 MB");
        std::vector<std::set<std::string>> keys;
        auto callback = [&](int depth, Json::parse_event_t event, Json& parsed) {
            if (depth > 128)
                throw Diagnostic("EElaborationLimit", {}, "JSON nesting budget exceeded");
            if (event == Json::parse_event_t::object_start)
                keys.emplace_back();
            else if (event == Json::parse_event_t::object_end)
                keys.pop_back();
            else if (event == Json::parse_event_t::key &&
                     !keys.back().insert(parsed.get<std::string>()).second)
                invalid("Duplicate JSON key");
            return true;
        };
        auto j = Json::parse(text, callback);
        if (!j.is_object() || j.value("format", "") != "verimc.logic" || j.at("formatVersion") != 1)
            throw Diagnostic("EArtifactVersion", {}, "Unsupported .vmcl version");
        fields(j, {"format", "formatVersion", "languageVersion", "compilerVersion", "top", "sources",
                   "instances", "ports", "nodes", "connections", "semantics", "limits", "validation"});
        if (j.at("semantics") != semanticsJson())
            invalid("Unsupported logic semantics");
        if (j.at("validation") != validationJson())
            invalid("Invalid validation claim");
        auto& l = j.at("limits");
        fields(l, {"maxNodes", "maxBits", "maxInstances", "maxSteps", "maxDepth", "maxSourceBytes",
                   "maxWidth", "maxArrayLength", "maxIntegerBits"});
        if (count(l.at("maxWidth"), maxLogicWidth) != maxLogicWidth ||
            count(l.at("maxArrayLength"), maxLogicArrayLength) != maxLogicArrayLength ||
            count(l.at("maxIntegerBits"), maxIntegerBits) != maxIntegerBits)
            invalid("Unsupported type limits");
        LogicGraph g;
        g.languageVersion = j.at("languageVersion").get<std::string>();
        g.compilerVersion = j.at("compilerVersion").get<std::string>();
        g.top = j.at("top").get<std::string>();
        g.limits = {count(l.at("maxNodes"), 1000000),     count(l.at("maxBits"), 10000000),
                    count(l.at("maxInstances"), 1000000), count(l.at("maxSteps"), 1000000000),
                    count(l.at("maxDepth"), 1000000000),  count(l.at("maxSourceBytes"), 1000000000)};
        for (auto& s : array(j.at("sources"), 1000000)) {
            fields(s, {"path", "sha256", "byteLength"});
            g.sources.push_back({s.at("path").get<std::string>(), s.at("sha256").get<std::string>(),
                                 count(s.at("byteLength"), 64 * 1024 * 1024)});
        }
        for (auto& i : array(j.at("instances"), 100000)) {
            fields(i, {"path", "module", "parameters", "source"});
            LogicInstance instance{i.at("path").get<std::string>(),
                                   i.at("module").get<std::string>(),
                                   {},
                                   sourceFromJson(i.at("source"))};
            if (!i.at("parameters").is_object())
                invalid("Invalid parameter table");
            for (auto& [name, value] : i.at("parameters").items())
                instance.parameters.emplace(name, decimal(value, 19730));
            g.instances.push_back(std::move(instance));
        }
        for (auto& n : array(j.at("nodes"), g.limits.maxNodes)) {
            fields(n, {"id", "op", "type", "inputs", "attributes", "source", "instance"});
            if (count(n.at("id"), 1000000) != g.nodes.size())
                invalid("Invalid node identity");
            auto op = nodeKindFromName(n.at("op").get<std::string>());
            LogicNode node{op,
                           typeFromJson(n.at("type")),
                           {},
                           attributesFromJson(op, n.at("attributes")),
                           sourceFromJson(n.at("source")),
                           n.at("instance").get<std::string>()};
            for (auto& input : array(n.at("inputs"), 10000000))
                node.inputs.push_back(static_cast<NodeId>(count(input, 1000000)));
            g.nodes.push_back(std::move(node));
        }
        for (auto& e : array(j.at("connections"), 10000000)) {
            fields(e, {"target", "targetOffset", "source", "sourceOffset", "width", "type", "sourceSpan"});
            g.connections.push_back({static_cast<NodeId>(count(e.at("target"), 1000000)),
                                     count(e.at("targetOffset"), 10000000),
                                     static_cast<NodeId>(count(e.at("source"), 1000000)),
                                     count(e.at("sourceOffset"), 10000000), count(e.at("width"), 10000000),
                                     typeFromJson(e.at("type")), sourceFromJson(e.at("sourceSpan"))});
        }
        for (auto& p : array(j.at("ports"), 1000000)) {
            fields(p, {"name", "direction", "node"});
            auto direction = p.at("direction").get<std::string>();
            if (direction != "input" && direction != "output")
                invalid("Unknown port direction");
            g.ports.push_back({p.at("name").get<std::string>(),
                               direction == "input" ? PortDirection::Input : PortDirection::Output,
                               static_cast<NodeId>(count(p.at("node"), 1000000))});
        }
        validateLogicGraph(g);
        return g;
    } catch (const Json::exception& error) {
        invalid(std::string("Malformed .vmcl: ") + error.what());
    }
}
std::string writeDiagnosticJson(const Diagnostic& d) {
    Json locations = Json::array();
    for (auto& item : d.related)
        locations.push_back(sourceJson(item));
    return Json({{"code", d.code},
                 {"severity", "error"},
                 {"message", d.what()},
                 {"source", sourceJson(d.span)},
                 {"sourceSha256", d.sourceSha256.empty() ? Json(nullptr) : Json(d.sourceSha256)},
                 {"instance", d.instance.empty() ? Json(nullptr) : Json(d.instance)},
                 {"related", locations}})
        .dump();
}
} // namespace verimc
