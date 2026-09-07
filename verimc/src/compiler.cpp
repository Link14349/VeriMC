#include "verimc/compiler.hpp"
#include "verimc/ast.hpp"
#include <algorithm>
#include <boost/multiprecision/cpp_int.hpp>
#include <fstream>
#include <functional>
#include <optional>
#include <set>
#include <sstream>

namespace verimc {
namespace {
using Type = LogicType;
using Node = LogicNode;
Type scalar(const std::string& kind, std::size_t width = 0) {
    Type t;
    t.kind = typeKindFromName(kind);
    t.width = width;
    return t;
}
struct Value {
    Type type;
    NodeId node = invalidNodeId;
    std::optional<Integer> constant;
    // Arithmetic invalidity is deferred until selected; static type errors are never deferred.
    std::shared_ptr<Diagnostic> invalidConstant;
    Value() = default;
    Value(Type t, NodeId id, std::optional<Integer> value, std::shared_ptr<Diagnostic> invalid = nullptr)
        : type(std::move(t)), node(id), constant(std::move(value)), invalidConstant(std::move(invalid)) {}
};
struct Symbol {
    Value value;
    std::string role;
    SourceSpan span;
};
struct FileState;
struct Scope {
    Scope* parent = nullptr;
    FileState* file = nullptr;
    std::string path, modulePath;
    std::map<std::string, Symbol> symbols;
    std::map<std::string, Scope*> instances;
    std::map<std::string, std::map<std::size_t, Scope*>> generates;
};
struct FileState {
    SourceFile source;
    std::map<std::string, FileState*> imports;
    std::map<std::string, const Declaration*> declarations;
    Scope* globals = nullptr;
};
struct Target {
    NodeId node;
    std::size_t offset, width;
    Type type;
    std::string role;
    SourceSpan span;
};
struct Write {
    Target target;
    Value value;
    SourceSpan span;
};
using Writes = std::map<std::pair<NodeId, std::size_t>, Write>;
Integer mask(std::size_t width) {
    return (Integer(1) << width) - 1;
}
Integer normalized(Integer value, const Type& type) {
    if (type.compileTime())
        return value;
    value &= mask(type.bits());
    if (type.kind == TypeKind::Int &&
        boost::multiprecision::bit_test(value, static_cast<unsigned>(type.width - 1)))
        value -= Integer(1) << type.width;
    return value;
}
Integer integerLiteral(std::string text) {
    text.erase(std::remove(text.begin(), text.end(), '_'), text.end());
    bool negative = !text.empty() && text[0] == '-';
    if (negative)
        text.erase(0, 1);
    int base = 10;
    std::size_t start = 0;
    if (text.starts_with("0x")) {
        base = 16;
        start = 2;
    } else if (text.starts_with("0b")) {
        base = 2;
        start = 2;
    }
    if (start == text.size())
        throw std::invalid_argument("Empty integer");
    const std::size_t maxDigits = base == 2 ? 65536 : base == 16 ? 16384 : 19729;
    if (text.size() - start > maxDigits)
        throw Diagnostic("EElaborationLimit", {}, "Integer literal exceeds bit budget");
    Integer value = 0;
    for (std::size_t i = start; i < text.size(); ++i) {
        char c = text[i];
        int digit = c >= '0' && c <= '9'   ? c - '0'
                    : c >= 'a' && c <= 'f' ? c - 'a' + 10
                    : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                           : 99;
        if (digit >= base)
            throw std::invalid_argument("Invalid integer");
        value = value * base + digit;
    }
    return negative ? -value : value;
}
class Compiler {
    CompileOptions options;
    std::filesystem::path root;
    std::map<std::string, std::unique_ptr<FileState>> files;
    std::set<std::string> loading;
    std::vector<std::unique_ptr<Scope>> scopes;
    std::vector<Node> nodes;
    std::vector<Write> connections;
    std::vector<LogicPort> ports;
    std::vector<LogicInstance> instances;
    std::vector<std::pair<const Statement*, Scope*>> pending;
    std::set<std::pair<FileState*, std::string>> activeModules, activeFunctions;
    std::set<std::pair<std::string, std::string>> packageNames;
    std::size_t steps = 0, bitCount = 0, depth = 0, instanceCount = 0, sourceBytes = 0;
    std::set<NodeId> ownedRegisters;
    std::map<std::pair<NodeId, std::size_t>, SourceSpan> driven;
    SourceSpan location;
    std::string currentInstance;
    void fail(const std::string& code, const SourceSpan& s, const std::string& message,
              std::vector<SourceSpan> related = {}) const {
        Diagnostic error(code, s, message);
        error.related = std::move(related);
        error.instance = currentInstance;
        auto file = files.find(s.file);
        if (file != files.end())
            error.sourceSha256 = file->second->source.sha256;
        throw error;
    }
    struct ScopeGuard {
        Compiler& compiler;
        std::string previous;
        ScopeGuard(Compiler& c, Scope* scope) : compiler(c), previous(c.currentInstance) {
            c.currentInstance = scope->path;
        }
        ~ScopeGuard() {
            compiler.currentInstance = std::move(previous);
        }
    };
    void step(const SourceSpan& s) {
        if (++steps > options.maxSteps)
            fail("EElaborationLimit", s, "Elaboration step budget exceeded");
    }
    struct DepthGuard {
        Compiler& c;
        DepthGuard(Compiler& compiler, const SourceSpan& s) : c(compiler) {
            if (++c.depth > c.options.maxDepth)
                c.fail("EElaborationLimit", s, "Elaboration depth budget exceeded");
        }
        ~DepthGuard() {
            --c.depth;
        }
    };
    Scope* scope(FileState* f, Scope* parent, std::string path, std::string modulePath) {
        auto p = std::make_unique<Scope>();
        p->file = f;
        p->parent = parent;
        p->path = std::move(path);
        p->modulePath = std::move(modulePath);
        auto result = p.get();
        scopes.push_back(std::move(p));
        return result;
    }
    bool exists(Scope* s, const std::string& name) {
        for (auto p = s; p; p = p->parent)
            if (p->symbols.contains(name) || p->instances.contains(name) || p->generates.contains(name))
                return true;
        return s->file->declarations.contains(name) || s->file->imports.contains(name);
    }
    void nameFree(Scope* s, const std::string& name, const SourceSpan& span) {
        if (exists(s, name))
            fail("EName", span, "Name already visible: " + name);
    }
    void bind(Scope* s, const std::string& name, Value v, const std::string& role, const SourceSpan& span) {
        nameFree(s, name, span);
        s->symbols.emplace(name, Symbol{v, role, span});
    }
    Symbol* lookup(Scope* s, const std::string& name) {
        for (auto p = s; p; p = p->parent) {
            auto i = p->symbols.find(name);
            if (i != p->symbols.end())
                return &i->second;
        }
        return nullptr;
    }
    Scope* findInstance(Scope* s, const std::string& name) {
        for (auto p = s; p; p = p->parent) {
            auto i = p->instances.find(name);
            if (i != p->instances.end())
                return i->second;
        }
        return nullptr;
    }
    FileState* loadFile(const std::filesystem::path& input) {
        auto real = std::filesystem::weakly_canonical(input);
        auto relative = real.lexically_relative(root);
        if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
            fail("EImport", {input.string()}, "Import is outside project root");
        auto key = relative.generic_string();
        if (loading.contains(key))
            fail("EImportCycle", {key}, "Import cycle");
        if (files.contains(key))
            return files.at(key).get();
        if (loading.size() >= options.maxDepth)
            fail("EElaborationLimit", {key}, "Import depth exceeded");
        std::error_code ec;
        auto size = std::filesystem::file_size(real, ec);
        if (ec)
            fail("EResourceMissing", {key}, "Cannot read source file");
        if (size > options.maxSourceBytes || size > options.maxSourceBytes - sourceBytes)
            fail("EElaborationLimit", {key}, "Source byte budget exceeded");
        sourceBytes += static_cast<std::size_t>(size);
        std::ifstream stream(real, std::ios::binary);
        std::ostringstream buffer;
        buffer << stream.rdbuf();
        if (!stream)
            fail("EResourceMissing", {key}, "Cannot read source file");
        auto state = std::make_unique<FileState>();
        state->source = parseSource(key, buffer.str(), options.maxDepth);
        auto f = state.get();
        files[key] = std::move(state);
        loading.insert(key);
        for (auto& d : f->source.declarations) {
            static const std::set<std::string> builtins = {
                "u",   "s",      "b",          "strength", "asBits",  "asUint",  "asInt",   "widen",
                "low", "concat", "repeatBits", "select",   "wrapAdd", "wrapSub", "ceilLog2"};
            if (d.kind == "functionDecl" && builtins.contains(d.name))
                fail("EName", d.span, "Cannot redefine a builtin function");
            if (!f->declarations.emplace(d.name, &d).second ||
                !packageNames.emplace(f->source.package, d.name).second)
                fail("EName", d.span, "Duplicate declaration: " + d.name);
        }
        f->globals = scope(f, nullptr, "", "");
        for (auto& [alias, path] : f->source.imports) {
            if (f->declarations.contains(alias) || f->imports.contains(alias))
                fail("EName", {key}, "Duplicate import alias: " + alias);
            f->imports[alias] = loadFile(real.parent_path() / path);
        }
        // Compile-time declarations are ordered; all module/function/type names are predeclared.
        for (auto& d : f->source.declarations)
            if (d.kind == "constDecl") {
                auto v = eval(d.value, f->globals);
                v = coerce(v, resolveType(d.type, f->globals), d.span);
                requireConstant(v, d.span);
                f->globals->symbols.emplace(d.name, Symbol{v, "constant", d.span});
            }
        loading.erase(key);
        return f;
    }
    std::pair<FileState*, const Declaration*> declaration(FileState* f, const std::string& name,
                                                          const SourceSpan& s) {
        auto split = name.find("::");
        if (split != std::string::npos) {
            auto alias = name.substr(0, split);
            auto it = f->imports.find(alias);
            if (it == f->imports.end())
                fail("EName", s, "Unknown import: " + alias);
            return declaration(it->second, name.substr(split + 2), s);
        }
        auto it = f->declarations.find(name);
        if (it == f->declarations.end())
            fail("EName", s, "Unknown declaration: " + name);
        return {f, it->second};
    }
    std::size_t natural(const Value& v, const SourceSpan& s, std::size_t maximum = 1000000) {
        requireConstant(v, s);
        if (!v.type.compileTime() || *v.constant < 0)
            fail("ETypeMismatch", s, "Expected nonnegative compile-time integer");
        if (*v.constant > maximum)
            fail("EElaborationLimit", s, "Compile-time size exceeds budget");
        return v.constant->convert_to<std::size_t>();
    }
    void requireConstant(const Value& v, const SourceSpan& s) {
        if (v.invalidConstant)
            throw *v.invalidConstant;
        if (!v.constant)
            fail("EConstantRequired", s, "Expected compile-time constant");
    }
    void checkInteger(const Integer& v, const SourceSpan& s) {
        Integer a = v < 0 ? -v : v;
        if (a != 0 && boost::multiprecision::msb(a) > 65535)
            fail("EElaborationLimit", s, "Compile-time integer exceeds 65536-bit budget");
    }
    void checkType(const Type& t, const SourceSpan& s) {
        if (t.word() && (t.width == 0 || t.width > 4096))
            fail("EElaborationLimit", s, "Bit width must be 1..4096");
        if (t.bits() > options.maxBits)
            fail("EElaborationLimit", s, "Type exceeds signal bit budget");
    }
    Type resolveType(const TypeSyntax& syntax, Scope* s) {
        auto k = syntax.name;
        if (k == "bit" || k == "clock")
            return scalar(k, 1);
        if (k == "level")
            return scalar(k, 4);
        if (k == "nat" || k == "integer")
            return scalar(k);
        if (k == "bits" || k == "uint" || k == "int") {
            auto n = natural(eval(syntax.size, s), syntax.span, 4096);
            if (n == 0)
                fail("ETypeMismatch", syntax.span, "Zero width is not allowed");
            return scalar(k, n);
        }
        if (k == "array") {
            Type t = scalar(k);
            t.element = std::make_shared<Type>(resolveType(*syntax.element, s));
            if (t.element->kind == TypeKind::Array || t.element->kind == TypeKind::Clock ||
                t.element->compileTime())
                fail("ETypeMismatch", syntax.span, "Unsupported array element type");
            t.length = natural(eval(syntax.size, s), syntax.span, 65536);
            if (t.length == 0)
                fail("ETypeMismatch", syntax.span, "Zero length is not allowed");
            checkType(t, syntax.span);
            return t;
        }
        auto [f, d] = declaration(s->file, k, syntax.span);
        if (d->kind != "enumDecl")
            fail("ETypeMismatch", syntax.span, "Expected enum type");
        Type t = scalar("enum", 1);
        t.identity = f->source.package + "::" + d->name;
        t.members = d->members;
        std::set<std::string> unique(d->members.begin(), d->members.end());
        if (unique.size() != d->members.size())
            fail("EName", d->span, "Duplicate enum member");
        while ((std::size_t(1) << t.width) < t.members.size())
            ++t.width;
        return t;
    }
    Value coerce(Value v, const Type& type, const SourceSpan& s) {
        if (type.compileTime() && v.type.compileTime()) {
            if (v.invalidConstant) {
                v.type = type;
                return v;
            }
            requireConstant(v, s);
            if (type.kind == TypeKind::Nat && *v.constant < 0)
                fail("ETypeMismatch", s, "Negative nat");
            v.type = type;
            return v;
        }
        if (!(v.type == type))
            fail(v.type.kind == TypeKind::Level || type.kind == TypeKind::Level ? "ELevelConversion"
                                                                                : "ETypeMismatch",
                 s, "Type mismatch: " + v.type.describe() + " -> " + type.describe());
        return v;
    }
    NodeId node(std::string op, const Type& type, std::vector<NodeId> inputs, NodeAttributes attrs,
                const SourceSpan& span, Scope* s) {
        step(span);
        checkType(type, span);
        if (nodes.size() >= options.maxNodes || type.bits() > options.maxBits - bitCount)
            fail("EElaborationLimit", span, "Logic graph budget exceeded");
        bitCount += type.bits();
        auto id = static_cast<NodeId>(nodes.size());
        nodes.push_back(
            {nodeKindFromName(op), type, std::move(inputs), std::move(attrs), span, s ? s->path : ""});
        return id;
    }
    Value constant(const Type& type, Integer value, const SourceSpan& span) {
        checkInteger(value, span);
        return {type, -1, normalized(std::move(value), type)};
    }
    NodeId materialize(Value v, const SourceSpan& span, Scope* s) {
        if (v.type.compileTime())
            fail("ETypeMismatch", span, "Compile-time integer cannot drive hardware");
        if (v.node >= 0)
            return v.node;
        requireConstant(v, span);
        return node("constant", v.type, {}, ConstantAttributes{*v.constant}, span, s);
    }
    Value operation(const std::string& op, const Type& type, const std::vector<Value>& args,
                    const SourceSpan& span, Scope* s, NodeAttributes attrs = {}) {
        for (auto& a : args)
            if (a.invalidConstant)
                return {type, -1, std::nullopt, a.invalidConstant};
        std::vector<NodeId> inputs;
        for (auto& a : args)
            inputs.push_back(materialize(a, span, s));
        return {type, node(op, type, std::move(inputs), std::move(attrs), span, s), std::nullopt};
    }
    Value slice(Value base, std::size_t offset, const Type& type, const SourceSpan& span, Scope* s) {
        if (base.constant)
            return constant(type, (*base.constant >> offset) & mask(type.bits()), span);
        return operation("slice", type, {base}, span, s, SliceAttributes{offset});
    }
    Value unary(const std::string& op, Value a, const SourceSpan& span, Scope* s) {
        Type result = a.type;
        if (op == "!") {
            if (a.type.kind != TypeKind::Bit)
                fail("EOperatorDomain", span, "! requires bit");
        } else if (op == "~") {
            if (!a.type.digital())
                fail("EOperatorDomain", span, "~ requires digital value");
        } else if (op == "+" || op == "-") {
            if (!a.type.number() && !a.type.compileTime())
                fail("EOperatorDomain", span, "Sign requires number");
            if (op == "-" && a.type.number())
                result = scalar("int", a.type.width + 1);
        }
        checkType(result, span);
        if (a.invalidConstant)
            return {result, -1, std::nullopt, a.invalidConstant};
        if (a.constant) {
            Integer value = *a.constant;
            if (op == "-")
                value = -value;
            else if (op == "!")
                value = (value == 0 ? 1 : 0);
            else if (op == "~")
                value = ~value;
            return constant(result, value, span);
        }
        if (op == "+")
            return a;
        return operation(op == "-" ? "neg" : op == "!" ? "logicalNot" : "bitNot", result, {a}, span, s);
    }
    Value binary(const std::string& op, Value a, Value b, const SourceSpan& span, Scope* s) {
        bool ct = a.type.compileTime() && b.type.compileTime();
        bool shift = op == "<<" || op == ">>";
        if (!ct && !shift && !(a.type == b.type))
            fail("ETypeMismatch", span, "Binary operands have different types");
        Type result = ct ? scalar("integer") : a.type;
        bool equality = op == "==" || op == "!=",
             relation = op == "<" || op == "<=" || op == ">" || op == ">=";
        bool logical = op == "&&" || op == "||", bits = op == "&" || op == "|" || op == "^";
        std::size_t shiftBy = 0;
        if (shift) {
            if (!a.type.word())
                fail("EOperatorDomain", span, "Shift requires a word");
            requireConstant(b, span);
            if (!b.type.compileTime() || *b.constant < 0)
                fail("EOperatorDomain", span, "Shift count must be a nonnegative compile-time integer");
            shiftBy = *b.constant >= a.type.width ? a.type.width : b.constant->convert_to<std::size_t>();
        } else if (logical) {
            if (a.type.kind != TypeKind::Bit)
                fail("EOperatorDomain", span, "Logical operators require bit");
        } else if (bits) {
            if (!a.type.digital())
                fail("EOperatorDomain", span, "Bitwise operators require digital values");
        } else if (equality || relation) {
            if (!ct && !(relation ? a.type.number() : (a.type.digital() || a.type.kind == TypeKind::Enum)))
                fail("EOperatorDomain", span, "Comparison is not supported for this type");
            result = scalar("bit", 1);
        } else {
            if (!ct && !a.type.number())
                fail("EOperatorDomain", span, "Arithmetic requires numeric type");
            if ((op == "*" || op == "/" || op == "%") && !ct)
                fail("EOperatorDomain", span, "Multiply/divide/modulo are compile-time only");
            if (!ct)
                result = scalar(op == "-" ? "int" : typeKindName(a.type.kind), a.type.width + 1);
        }
        checkType(result, span);
        if (logical && a.constant && ((op == "&&" && *a.constant == 0) || (op == "||" && *a.constant != 0)))
            return constant(result, op == "||", span);
        if (a.invalidConstant || b.invalidConstant)
            return {result, -1, std::nullopt, a.invalidConstant ? a.invalidConstant : b.invalidConstant};
        if (a.constant && b.constant) {
            Integer x = *a.constant, y = *b.constant, v;
            if (op == "+")
                v = x + y;
            else if (op == "-")
                v = x - y;
            else if (op == "*")
                v = x * y;
            else if (op == "/" || op == "%") {
                if (y == 0) {
                    auto error = std::make_shared<Diagnostic>("EOperatorDomain", span, "Division by zero");
                    error->instance = currentInstance;
                    auto file = files.find(span.file);
                    if (file != files.end())
                        error->sourceSha256 = file->second->source.sha256;
                    return {result, -1, std::nullopt, error};
                }
                if (op == "/")
                    v = x / y;
                else
                    v = x % y;
            } else if (op == "&")
                v = x & y;
            else if (op == "|")
                v = x | y;
            else if (op == "^")
                v = x ^ y;
            else if (op == "&&")
                v = (x != 0 && y != 0);
            else if (op == "||")
                v = (x != 0 || y != 0);
            else if (op == "==")
                v = x == y;
            else if (op == "!=")
                v = x != y;
            else if (op == "<")
                v = x < y;
            else if (op == "<=")
                v = x <= y;
            else if (op == ">")
                v = x > y;
            else if (op == ">=")
                v = x >= y;
            else if (op == "<<")
                v = x << shiftBy;
            else if (op == ">>")
                v = x >> shiftBy;
            return constant(result, v, span);
        }
        static const std::map<std::string, std::string> names = {
            {"+", "add"}, {"-", "sub"},         {"&", "and"},        {"|", "or"},
            {"^", "xor"}, {"&&", "logicalAnd"}, {"||", "logicalOr"}, {"==", "eq"},
            {"!=", "ne"}, {"<", "lt"},          {"<=", "le"},        {">", "gt"},
            {">=", "ge"}, {"<<", "shiftLeft"},  {">>", "shiftRight"}};
        if (shift)
            return operation(names.at(op), result, {a}, span, s, ShiftAttributes{shiftBy});
        return operation(names.at(op), result, {a, b}, span, s);
    }
    Value select(Value c, Value a, Value b, const SourceSpan& span, Scope* s) {
        if (c.type.kind != TypeKind::Bit)
            fail("ETypeMismatch", span, "Condition must have type bit");
        if (a.type.compileTime() && b.type.compileTime()) {
            a.type = scalar("integer");
            b.type = a.type;
        }
        if (!(a.type == b.type))
            fail("ETypeMismatch", span, "Selection branches have different types");
        if (a.type.kind == TypeKind::Level || a.type.kind == TypeKind::Clock ||
            (a.type.kind == TypeKind::Array && a.type.element->kind == TypeKind::Level))
            fail("EOperatorDomain", span, "Cannot select level or clock");
        if (c.invalidConstant)
            return {a.type, -1, std::nullopt, c.invalidConstant};
        if (c.constant)
            return *c.constant != 0 ? a : b;
        if (a.type.compileTime())
            fail("EConstantRequired", span, "Compile-time selection needs constant condition");
        return operation("mux", a.type, {c, a, b}, span, s);
    }
    Value builtin(const Expr& e, const std::vector<Value>& args, Scope* s);
    Value eval(ExprPtr e, Scope* s);
    Target target(ExprPtr e, Scope* s, bool writing, bool sequential = false);
    Scope* resolveScope(ExprPtr e, Scope* s);
    void expand(const Declaration& d, FileState* f, Scope* s, const std::map<std::string, Value>& args);
    void declareItems(const std::vector<Statement>& body, Scope* s);
    void process(const Statement& stmt, Scope* s);
    Writes control(const std::vector<Statement>& body, Scope* s, bool sequential);
    void attach(const Write& write);

  public:
    explicit Compiler(CompileOptions o) : options(std::move(o)) {}
    LogicGraph run();
};
Value Compiler::builtin(const Expr& e, const std::vector<Value>& a, Scope* s) {
    auto arity = [&](std::size_t n) {
        if (a.size() != n)
            fail("EName", e.span, "Wrong argument count for " + e.text);
    };
    auto size = [&](std::size_t i) {
        auto n = natural(a.at(i), e.span, 4096);
        if (n == 0)
            fail("ETypeMismatch", e.span, "Width must be positive");
        return n;
    };
    const auto& op = e.text;
    if (op == "u" || op == "s" || op == "b") {
        arity(2);
        auto width = size(0);
        if (!a[1].type.compileTime())
            fail("ETypeMismatch", e.span, "Constant constructor requires mathematical integer");
        auto t = scalar(op == "u" ? "uint" : op == "s" ? "int" : "bits", width);
        if (a[1].invalidConstant)
            return {t, -1, std::nullopt, a[1].invalidConstant};
        requireConstant(a[1], e.span);
        auto v = *a[1].constant;
        Integer min = t.kind == TypeKind::Int ? -(Integer(1) << (width - 1)) : Integer(0);
        Integer max = t.kind == TypeKind::Int ? (Integer(1) << (width - 1)) - 1 : mask(width);
        if (v < min || v > max)
            fail("ELiteralRange", e.span, "Constant is outside the declared width");
        return constant(t, v, e.span);
    }
    if (op == "strength") {
        arity(1);
        auto n = natural(a[0], e.span, 15);
        return constant(scalar("level", 4), n, e.span);
    }
    if (op == "ceilLog2") {
        arity(1);
        requireConstant(a[0], e.span);
        if (!a[0].type.compileTime() || *a[0].constant <= 0)
            fail("ELiteralRange", e.span, "ceilLog2 needs positive integer");
        Integer v = *a[0].constant - 1;
        return constant(scalar("nat"), v == 0 ? 0 : boost::multiprecision::msb(v) + 1, e.span);
    }
    if (op == "select") {
        arity(3);
        return select(a[0], a[1], a[2], e.span, s);
    }
    if (op == "asBits" || op == "asUint" || op == "asInt") {
        arity(1);
        if (!a[0].type.digital() || (op == "asBits" && a[0].type.kind == TypeKind::Bits))
            fail("EOperatorDomain", e.span, "Invalid reinterpretation");
        auto t = scalar(op == "asBits" ? "bits" : op == "asUint" ? "uint" : "int", a[0].type.width);
        if (a[0].constant)
            return constant(t, *a[0].constant, e.span);
        return operation("reinterpret", t, a, e.span, s);
    }
    if (op == "widen" || op == "low") {
        arity(2);
        auto width = size(1);
        auto t = a[0].type;
        if (op == "widen") {
            if (!t.number() || width < t.width)
                fail("EOperatorDomain", e.span, "widen requires a wider numeric type");
            t.width = width;
        } else {
            if (!t.word() || width > t.width)
                fail("EOperatorDomain", e.span, "low width is out of range");
            t = scalar("bits", width);
        }
        if (a[0].constant)
            return constant(t, *a[0].constant, e.span);
        return operation(op, t, {a[0]}, e.span, s);
    }
    if (op == "wrapAdd" || op == "wrapSub") {
        arity(2);
        if (!a[0].type.number() || !(a[0].type == a[1].type))
            fail("ETypeMismatch", e.span, "wrap arithmetic needs equal numeric types");
        if (a[0].constant && a[1].constant) {
            Integer v = *a[0].constant;
            if (op == "wrapAdd")
                v += *a[1].constant;
            else
                v -= *a[1].constant;
            return constant(a[0].type, v, e.span);
        }
        return operation(op, a[0].type, a, e.span, s);
    }
    if (op == "concat" || op == "repeatBits") {
        if (op == "concat" && a.size() < 2)
            fail("EName", e.span, "concat needs at least two arguments");
        std::vector<Value> parts = a;
        if (op == "repeatBits") {
            arity(2);
            auto count = size(1);
            if (a[0].type.bits() > 4096 / count)
                fail("EElaborationLimit", e.span, "Repeated word exceeds width budget");
            parts.assign(count, a[0]);
        }
        std::size_t width = 0;
        bool allConstant = true;
        Integer v = 0;
        for (auto& part : parts) {
            if (part.type.kind != TypeKind::Bit && part.type.kind != TypeKind::Bits)
                fail("EOperatorDomain", e.span, "concat/repeatBits require bit or bits");
            width += part.type.width;
            allConstant &= part.constant.has_value();
            if (part.constant) {
                v <<= part.type.width;
                v |= *part.constant;
            }
        }
        auto t = scalar("bits", width);
        checkType(t, e.span);
        if (allConstant)
            return constant(t, v, e.span);
        return operation("concat", t, parts, e.span, s);
    }
    auto [f, d] = declaration(s->file, op, e.span);
    if (d->kind != "functionDecl")
        fail("EName", e.span, "Expected a function: " + op);
    if (!activeFunctions.emplace(f, d->name).second)
        fail("ERecursiveDesign", e.span, "Recursive function");
    if (a.size() != d->parameters.size())
        fail("EName", e.span, "Function argument count mismatch");
    auto locals = scope(f, f->globals, s->path, s->modulePath);
    bool compileFunction = false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        auto& p = d->parameters[i];
        auto t = resolveType(p.type, locals);
        if (t.kind == TypeKind::Level || t.kind == TypeKind::Clock)
            fail("ETypeMismatch", p.span, "Function parameters cannot be level or clock");
        compileFunction |= t.compileTime();
        bind(locals, p.name, coerce(a[i], t, p.span), "constant", p.span);
    }
    if (compileFunction)
        for (auto& v : a)
            requireConstant(v, e.span);
    for (auto& st : d->body) {
        auto v = eval(st.exprs.at(0), locals);
        bind(locals, st.name, v, "let", st.span);
    }
    auto result = coerce(eval(d->value, locals), resolveType(d->type, locals), d->span);
    if (result.type.kind == TypeKind::Level || result.type.kind == TypeKind::Clock)
        fail("ETypeMismatch", d->span, "Invalid function return type");
    activeFunctions.erase({f, d->name});
    return result;
}
Scope* Compiler::resolveScope(ExprPtr e, Scope* s) {
    if (e->kind == "name")
        return findInstance(s, e->text);
    if (e->kind == "index") {
        Scope* owner = s;
        std::string name;
        if (e->args[0]->kind == "name")
            name = e->args[0]->text;
        else if (e->args[0]->kind == "member") {
            owner = resolveScope(e->args[0]->args[0], s);
            name = e->args[0]->text;
        }
        if (!owner)
            return nullptr;
        for (auto p = owner; p; p = p->parent) {
            if (p->modulePath != s->modulePath)
                break;
            auto it = p->generates.find(name);
            if (it != p->generates.end()) {
                auto index = natural(eval(e->args[1], s), e->span);
                auto found = it->second.find(index);
                if (found == it->second.end())
                    fail("EWidthRange", e->span, "Generated index is outside range");
                return found->second;
            }
        }
    }
    if (e->kind == "member") {
        auto parent = resolveScope(e->args[0], s);
        if (!parent)
            return nullptr;
        if (parent->modulePath != s->modulePath)
            fail("EDriveDirection", e->span, "Cannot access a child module's internals");
        auto it = parent->instances.find(e->text);
        if (it != parent->instances.end())
            return it->second;
    }
    return nullptr;
}
Target Compiler::target(ExprPtr e, Scope* s, bool writing, bool sequential) {
    if (e->kind == "index" || e->kind == "slice") {
        auto t = target(e->args[0], s, writing, sequential);
        auto index = natural(eval(e->args[1], s), e->span);
        if (t.type.kind == TypeKind::Array) {
            if (e->kind == "slice" || index >= t.type.length)
                fail("EWidthRange", e->span, "Array index outside range");
            Type element = *t.type.element;
            t.offset += index * element.bits();
            t.type = element;
            t.width = element.bits();
        } else {
            if (!t.type.word())
                fail("EWidthRange", e->span, "Only words and arrays can be indexed");
            auto end = e->kind == "slice" ? natural(eval(e->args[2], s), e->span) : index + 1;
            if (index >= end || end > t.type.width)
                fail("EWidthRange", e->span, "Bit slice outside range");
            t.offset += index;
            t.width = end - index;
            t.type = scalar(e->kind == "slice" ? "bits" : "bit", t.width);
        }
        return t;
    }
    Symbol* symbol = nullptr;
    bool child = false;
    if (e->kind == "name")
        symbol = lookup(s, e->text);
    else if (e->kind == "member") {
        auto owner = resolveScope(e->args[0], s);
        if (!owner)
            fail("EName", e->span, "Unknown instance or generated scope");
        auto it = owner->symbols.find(e->text);
        if (it != owner->symbols.end())
            symbol = &it->second;
        child = owner->modulePath != s->modulePath;
    }
    if (!symbol || symbol->value.node < 0)
        fail("EName", e->span, "Expected hardware signal: " + e->text);
    const auto& role = symbol->role;
    if (sequential) {
        if (child || role != "register" || e->kind != "name")
            fail("ENextTarget", e->span, "next must target a local whole register");
    } else if (writing) {
        if (child ? role != "input" : (role != "output" && role != "wire"))
            fail("EDriveDirection", e->span, "Illegal connection target");
    } else if (child && role != "output")
        fail("EDriveDirection", e->span, "Only child outputs can be read");
    return {symbol->value.node, 0, symbol->value.type.bits(), symbol->value.type, role, e->span};
}
Value Compiler::eval(ExprPtr e, Scope* s) {
    ScopeGuard scopeGuard(*this, s);
    if (!e)
        throw std::logic_error("Missing expression");
    DepthGuard guard(*this, e->span);
    step(e->span);
    if (e->kind == "integer")
        return constant(scalar("integer"), integerLiteral(e->text), e->span);
    if (e->kind == "boolean")
        return constant(scalar("bit", 1), e->text == "true" ? 1 : 0, e->span);
    if (e->kind == "name") {
        if (auto sym = lookup(s, e->text))
            return sym->value;
        auto split = e->text.rfind("::");
        if (split != std::string::npos) {
            auto prefix = e->text.substr(0, split), tail = e->text.substr(split + 2);
            auto alias = s->file->imports.find(prefix);
            if (alias != s->file->imports.end()) {
                auto sym = lookup(alias->second->globals, tail);
                if (sym)
                    return sym->value;
            }
            auto [f, d] = declaration(s->file, prefix, e->span);
            if (d->kind == "enumDecl") {
                TypeSyntax ts;
                ts.name = d->name;
                ts.span = e->span;
                auto t = resolveType(ts, f->globals);
                auto i = std::find(t.members.begin(), t.members.end(), tail);
                if (i == t.members.end())
                    fail("EName", e->span, "Unknown enum member");
                return constant(t, std::distance(t.members.begin(), i), e->span);
            }
        }
        fail("EName", e->span, "Unknown value: " + e->text);
    }
    if (e->kind == "member") {
        auto t = target(e, s, false);
        return {t.type, t.node, std::nullopt};
    }
    if (e->kind == "index" || e->kind == "slice") {
        auto base = eval(e->args[0], s);
        auto index = natural(eval(e->args[1], s), e->span);
        if (base.type.kind == TypeKind::Array) {
            if (e->kind == "slice" || index >= base.type.length)
                fail("EWidthRange", e->span, "Array index outside range");
            return slice(base, index * base.type.element->bits(), *base.type.element, e->span, s);
        }
        if (!base.type.word())
            fail("EWidthRange", e->span, "Only words and arrays can be indexed");
        auto end = e->kind == "slice" ? natural(eval(e->args[2], s), e->span) : index + 1;
        if (index >= end || end > base.type.width)
            fail("EWidthRange", e->span, "Bit slice outside range");
        return slice(base, index, scalar(e->kind == "slice" ? "bits" : "bit", end - index), e->span, s);
    }
    if (e->kind == "unary")
        return unary(e->text, eval(e->args[0], s), e->span, s);
    if (e->kind == "binary") {
        auto a = eval(e->args[0], s);
        auto b = eval(e->args[1], s);
        return binary(e->text, a, b, e->span, s);
    }
    if (e->kind == "select") {
        auto c = eval(e->args[0], s);
        auto a = eval(e->args[1], s);
        auto b = eval(e->args[2], s);
        return select(c, a, b, e->span, s);
    }
    std::vector<Value> args;
    for (auto& a : e->args)
        args.push_back(eval(a, s));
    if (e->kind == "call")
        return builtin(*e, args, s);
    if (e->kind == "array") {
        Type t = scalar("array");
        t.element = std::make_shared<Type>(args.at(0).type);
        t.length = args.size();
        if (t.element->compileTime() || t.element->kind == TypeKind::Clock ||
            t.element->kind == TypeKind::Array)
            fail("ETypeMismatch", e->span, "Invalid array element");
        bool ct = true;
        Integer value = 0;
        for (std::size_t i = 0; i < args.size(); ++i) {
            coerce(args[i], *t.element, e->span);
            ct &= args[i].constant.has_value();
            if (args[i].constant)
                value |= (*args[i].constant & mask(t.element->bits())) << (i * t.element->bits());
        }
        checkType(t, e->span);
        if (ct)
            return constant(t, value, e->span);
        return operation("array", t, args, e->span, s);
    }
    throw std::logic_error("Unknown AST expression: " + e->kind);
}
void Compiler::expand(const Declaration& d, FileState* f, Scope* s,
                      const std::map<std::string, Value>& args) {
    DepthGuard guard(*this, d.span);
    if (d.kind == "cellDecl")
        fail("EModelMissing", d.span, "Physical cell model/asset integration is not implemented");
    if (d.kind != "moduleDecl")
        fail("EName", d.span, "Compilation target must be a module");
    if (!activeModules.emplace(f, d.name).second)
        fail("ERecursiveDesign", d.span, "Recursive module instantiation");
    if (++instanceCount > options.maxInstances)
        fail("EElaborationLimit", d.span, "Instance budget exceeded");
    std::map<std::string, Integer> parameters;
    std::set<std::string> consumed;
    for (auto& p : d.parameters) {
        auto it = args.find(p.name);
        Value v;
        if (it != args.end()) {
            v = it->second;
            consumed.insert(p.name);
        } else {
            if (!p.value)
                fail("EName", p.span, "Missing module parameter: " + p.name);
            v = eval(p.value, s);
        }
        v = coerce(v, resolveType(p.type, s), p.span);
        requireConstant(v, p.span);
        bind(s, p.name, v, "constant", p.span);
        parameters[p.name] = *v.constant;
    }
    if (consumed.size() != args.size())
        fail("EName", d.span, "Unknown module parameter");
    instances.push_back({s->path, f->source.package + "::" + d.name, std::move(parameters), d.span});
    declareItems(d.body, s);
    activeModules.erase({f, d.name});
}
void Compiler::declareItems(const std::vector<Statement>& body, Scope* s) {
    ScopeGuard scopeGuard(*this, s);
    bool seenOn = false;
    for (auto& st : body) {
        step(st.span);
        auto k = st.kind;
        if (k == "constDecl") {
            auto v = coerce(eval(st.exprs.at(0), s), resolveType(st.type, s), st.span);
            requireConstant(v, st.span);
            bind(s, st.name, v, "constant", st.span);
        } else if (k == "requireStmt") {
            auto v = eval(st.exprs.at(0), s);
            requireConstant(v, st.span);
            if (v.type.kind != TypeKind::Bit)
                fail("ETypeMismatch", st.span, "require expects bit");
            if (*v.constant == 0)
                fail("ERequire", st.span, "require failed in " + s->path);
        } else if (k == "input" || k == "output" || k == "wireDecl" || k == "regDecl") {
            auto t = resolveType(st.type, s);
            if (t.compileTime())
                fail("ETypeMismatch", st.span, "Compile-time type cannot be a signal");
            bool reg = k == "regDecl";
            if (reg && (t.kind == TypeKind::Level || t.kind == TypeKind::Clock ||
                        (t.kind == TypeKind::Array && t.element->kind == TypeKind::Level)))
                fail("ETypeMismatch", st.span, "Invalid register type");
            auto role = reg ? "register" : k == "wireDecl" ? "wire" : k;
            NodeAttributes attrs;
            if (!reg)
                attrs = SignalAttributes{s->path + "." + st.name, signalRoleFromName(role)};
            if (reg) {
                auto reset = coerce(eval(st.exprs.at(0), s), t, st.span);
                requireConstant(reset, st.span);
                attrs = RegisterAttributes{s->path + "." + st.name, *reset.constant};
            }
            auto id = node(reg                                             ? "register"
                           : k == "input" && s->parent == s->file->globals ? "input"
                                                                           : "signal",
                           t, {}, attrs, st.span, s);
            bind(s, st.name, {t, id, std::nullopt}, role, st.span);
        } else if (k == "instDecl") {
            nameFree(s, st.name, st.span);
            auto [f, d] = declaration(s->file, st.module.name, st.span);
            std::map<std::string, Value> args;
            for (auto& [name, expr] : st.module.args) {
                auto v = eval(expr, s);
                requireConstant(v, expr->span);
                if (!args.emplace(name, v).second)
                    fail("EName", expr->span, "Duplicate argument: " + name);
            }
            auto child = scope(f, f->globals, s->path + "." + st.name, s->path + "." + st.name);
            s->instances[st.name] = child;
            expand(*d, f, child, args);
            // Imported module scopes also have globals as parent; only root inputs are sources.
            for (auto& [name, sym] : child->symbols)
                if (sym.role == "input")
                    nodes.at(sym.value.node).op = NodeKind::Signal;
        } else if (k == "generateBlock") {
            nameFree(s, st.name, st.span);
            auto begin = natural(eval(st.exprs.at(0), s), st.span),
                 end = natural(eval(st.exprs.at(1), s), st.span);
            if (end < begin)
                fail("EWidthRange", st.span, "Reversed generate range");
            s->generates.emplace(st.name, std::map<std::size_t, Scope*>{});
            for (auto i = begin; i < end; ++i) {
                step(st.span);
                auto child =
                    scope(s->file, s, s->path + "." + st.name + "[" + std::to_string(i) + "]", s->modulePath);
                bind(child, st.indexName, constant(scalar("nat"), i, st.span), "constant", st.span);
                s->generates.at(st.name)[i] = child;
                declareItems(st.body, child);
            }
        } else {
            if (k == "onBlock") {
                if (seenOn)
                    fail("EClockDomain", st.span, "Only one on block per module");
                seenOn = true;
            }
            pending.emplace_back(&st, s);
        }
    }
}
Writes Compiler::control(const std::vector<Statement>& body, Scope* outer, bool sequential) {
    ScopeGuard scopeGuard(*this, outer);
    DepthGuard guard(*this, body.empty() ? location : body.front().span);
    auto s = scope(outer->file, outer, outer->path, outer->modulePath);
    Writes writes;
    auto mergeUnique = [&](const Writes& more) {
        for (auto& [key, w] : more)
            if (!writes.emplace(key, w).second)
                fail(sequential ? "ENextConflict" : "EMultipleDriver", w.span,
                     "Multiple assignments on one control path", {writes.at(key).span});
    };
    auto parts = [&](const Target& t, const Value& value, const SourceSpan& span) {
        Writes result;
        if (sequential) {
            result.emplace(std::make_pair(t.node, t.offset), Write{t, value, span});
            return result;
        }
        auto leaf = t.type.kind == TypeKind::Array ? *t.type.element : t.type;
        auto atomWidth = leaf.word() ? std::size_t(1) : leaf.bits();
        auto atomType = leaf.word() ? scalar("bit", 1) : leaf;
        for (std::size_t off = 0; off < t.width; off += atomWidth) {
            Target part{t.node, t.offset + off, atomWidth, atomType, t.role, span};
            auto v =
                t.width == atomWidth && t.type == atomType ? value : slice(value, off, atomType, span, s);
            result.emplace(std::make_pair(t.node, part.offset), Write{part, v, span});
        }
        return result;
    };
    auto choose = [&](Value condition, const Writes& yes, const Writes& no, const SourceSpan& span) {
        Writes result;
        std::set<std::pair<NodeId, std::size_t>> keys;
        for (auto& [key, w] : yes)
            keys.insert(key);
        for (auto& [key, w] : no)
            keys.insert(key);
        for (auto key : keys) {
            auto yi = yes.find(key), ni = no.find(key);
            if (!sequential && (yi == yes.end() || ni == no.end()))
                fail("EUndriven", span, "Combinational branch leaves a signal undriven");
            Write w = yi != yes.end() ? yi->second : ni->second;
            Value hold{nodes.at(w.target.node).type, w.target.node, std::nullopt};
            w.value = select(condition, yi != yes.end() ? yi->second.value : hold,
                             ni != no.end() ? ni->second.value : hold, span, s);
            result.emplace(key, w);
        }
        return result;
    };
    for (auto& st : body) {
        step(st.span);
        if (st.kind == "letStmt") {
            bind(s, st.name, eval(st.exprs.at(0), s), "let", st.span);
            continue;
        }
        if (st.kind == "assignment" || st.kind == "nextStmt") {
            ExprPtr lhs = st.kind == "nextStmt" ? std::make_shared<Expr>(Expr{"name", st.name, {}, st.span})
                                                : st.exprs.at(0);
            auto t = target(lhs, s, true, sequential);
            auto v = coerce(eval(st.exprs.at(st.kind == "nextStmt" ? 0 : 1), s), t.type, st.span);
            mergeUnique(parts(t, v, st.span));
            continue;
        }
        if (st.kind == "if") {
            auto condition = eval(st.exprs.at(0), s);
            if (condition.type.kind != TypeKind::Bit)
                fail("ETypeMismatch", st.span, "if requires bit");
            auto yes = control(st.body, s, sequential), no = control(st.otherwise, s, sequential);
            mergeUnique(choose(condition, yes, no, st.span));
            continue;
        }
        if (st.kind == "match") {
            auto subject = eval(st.exprs.at(0), s);
            if (!subject.type.digital() && subject.type.kind != TypeKind::Enum)
                fail("EOperatorDomain", st.span, "Invalid match type");
            std::vector<std::pair<Value, Writes>> cases;
            Writes fallback;
            bool hasDefault = false;
            std::set<std::string> labels;
            for (auto& branch : st.body) {
                auto ws = control(branch.body, s, sequential);
                if (branch.exprs.empty()) {
                    hasDefault = true;
                    fallback = std::move(ws);
                } else {
                    auto label = coerce(eval(branch.exprs[0], s), subject.type, branch.span);
                    requireConstant(label, branch.span);
                    if (!labels.insert(label.constant->str()).second)
                        fail("EName", branch.span, "Duplicate match case");
                    cases.emplace_back(binary("==", subject, label, branch.span, s), std::move(ws));
                }
            }
            bool exhaustive =
                (subject.type.kind == TypeKind::Bit && labels.size() == 2) ||
                (subject.type.kind == TypeKind::Enum && labels.size() == subject.type.members.size());
            if (!hasDefault && !exhaustive)
                fail("EUndriven", st.span, "match requires default or complete bit/enum cases");
            if (!hasDefault) {
                fallback = std::move(cases.back().second);
                cases.pop_back();
            }
            for (auto it = cases.rbegin(); it != cases.rend(); ++it)
                fallback = choose(it->first, it->second, fallback, st.span);
            mergeUnique(fallback);
            continue;
        }
        fail("ECapability", st.span, "Unsupported control statement: " + st.kind);
    }
    return writes;
}
void Compiler::attach(const Write& w) {
    for (std::size_t bit = w.target.offset; bit < w.target.offset + w.target.width; ++bit) {
        auto key = std::make_pair(w.target.node, bit);
        if (!driven.emplace(key, w.span).second)
            fail("EMultipleDriver", w.span, "Overlapping signal drivers", {driven.at(key)});
    }
    connections.push_back(w);
}
void Compiler::process(const Statement& st, Scope* s) {
    ScopeGuard scopeGuard(*this, s);
    if (st.kind == "connectStmt") {
        auto t = target(st.exprs.at(0), s, true);
        auto v = coerce(eval(st.exprs.at(1), s), t.type, st.span);
        attach({t, v, st.span});
    } else if (st.kind == "combBlock") {
        for (auto& [key, w] : control(st.body, s, false))
            attach(w);
    } else if (st.kind == "unusedStmt") {
        auto t = target(st.exprs.at(0), s, false);
        if (t.role != "output" || nodes.at(t.node).instance == s->modulePath)
            fail("EDriveDirection", st.span, "unused expects a child output");
    } else if (st.kind == "onBlock") {
        auto clockTarget = target(st.exprs.at(0), s, false);
        if (clockTarget.role != "input" && clockTarget.role != "wire")
            fail("EClockDomain", st.span, "on requires a module input or wire clock alias");
        auto clk = eval(st.exprs.at(0), s);
        if (clk.type.kind != TypeKind::Clock)
            fail("EClockDomain", st.span, "on requires clock input or alias");
        auto reset = eval(st.exprs.at(1), s);
        if (reset.type.kind != TypeKind::Bit)
            fail("ETypeMismatch", st.span, "reset requires bit");
        auto ws = control(st.body, s, true);
        for (auto& [key, w] : ws) {
            if (!ownedRegisters.insert(w.target.node).second)
                fail("ENextConflict", st.span, "Register has multiple owners");
            auto nextId = materialize(w.value, w.span, s), clockId = materialize(clk, st.span, s),
                 resetId = materialize(reset, st.span, s);
            nodes.at(w.target.node).inputs = {nextId, clockId, resetId};
        }
    } else
        fail("ECapability", st.span, "Unsupported module statement: " + st.kind);
}
LogicGraph Compiler::run() {
    if (options.maxNodes == 0 || options.maxNodes > 1000000 || options.maxBits == 0 ||
        options.maxBits > 10000000 || options.maxDepth == 0 || options.maxDepth > 256 ||
        options.maxSteps == 0 || options.maxSteps > 1000000000 || options.maxInstances == 0 ||
        options.maxInstances > 1000000 || options.maxSourceBytes == 0 ||
        options.maxSourceBytes > 64 * 1024 * 1024)
        fail("EElaborationLimit", {}, "Unsupported compiler budget");
    options.input = std::filesystem::absolute(options.input);
    root = std::filesystem::weakly_canonical(options.projectRoot.empty() ? options.input.parent_path()
                                                                         : options.projectRoot);
    auto f = loadFile(options.input);
    auto [targetFile, d] = declaration(f, options.top, {f->source.path});
    location = d->span;
    auto top = scope(targetFile, targetFile->globals, d->name, d->name);
    std::map<std::string, Value> args;
    for (auto& [name, text] : options.parameters) {
        auto value = text == "true" || text == "false"
                         ? constant(scalar("bit", 1), text == "true", d->span)
                         : constant(scalar("integer"), integerLiteral(text), d->span);
        args.emplace(name, value);
    }
    expand(*d, targetFile, top, args);
    // Pending statements point to stable ASTs, so forward signal references now resolve.
    for (auto& [st, s] : pending)
        process(*st, s);
    std::vector<LogicConnection> edges;
    for (auto& w : connections) {
        auto source = materialize(w.value, w.span, nullptr);
        if (w.value.node < 0)
            nodes.at(source).instance = nodes.at(w.target.node).instance;
        edges.push_back({w.target.node, w.target.offset, source, 0, w.target.width, w.target.type, w.span});
    }
    for (std::size_t id = 0; id < nodes.size(); ++id) {
        auto& n = nodes[id];
        currentInstance = n.instance;
        if (n.op == NodeKind::Signal)
            for (std::size_t bit = 0; bit < n.type.bits(); ++bit)
                if (!driven.contains({static_cast<NodeId>(id), bit}))
                    fail("EUndriven", n.span,
                         "Undriven signal bit: " + std::get<SignalAttributes>(n.attrs).name + "[" +
                             std::to_string(bit) + "]");
        if (n.op == NodeKind::Register && !ownedRegisters.contains(static_cast<NodeId>(id)))
            fail("EUnownedRegister", n.span, "Register has no next owner");
    }
    for (auto& st : d->body)
        if (st.kind == "input" || st.kind == "output") {
            auto& sym = top->symbols.at(st.name);
            ports.push_back(
                {st.name, st.kind == "input" ? PortDirection::Input : PortDirection::Output, sym.value.node});
        }
    std::size_t clockCount = 0;
    for (auto& port : ports)
        if (port.direction == PortDirection::Input && nodes.at(port.node).type.kind == TypeKind::Clock)
            ++clockCount;
    if (clockCount > 1) {
        currentInstance = top->path;
        fail("EClockDomain", d->span, "Only one top-level clock is allowed");
    }
    LogicGraph graph;
    graph.top = targetFile->source.package + "::" + d->name;
    graph.limits = options;
    for (auto& [path, state] : files)
        graph.sources.push_back({path, state->source.sha256, state->source.byteLength});
    graph.instances = std::move(instances);
    graph.ports = std::move(ports);
    graph.nodes = std::move(nodes);
    graph.connections = std::move(edges);
    try {
        validateLogicGraph(graph);
    } catch (Diagnostic& error) {
        auto file = files.find(error.span.file);
        if (file != files.end())
            error.sourceSha256 = file->second->source.sha256;
        for (auto& n : graph.nodes)
            if (n.span.file == error.span.file && n.span.start == error.span.start) {
                error.instance = n.instance;
                break;
            }
        throw;
    }
    return graph;
}
} // namespace
LogicGraph compileFile(const CompileOptions& options) {
    return Compiler(options).run();
}
} // namespace verimc
