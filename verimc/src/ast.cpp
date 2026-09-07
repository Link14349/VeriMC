#include "verimc/ast.hpp"
#include "verimcGrammarLexer.h"
#include "verimcGrammarParser.h"
#include <iomanip>
#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <sstream>

namespace verimc {
using Json = nlohmann::json;
std::string sha256(const std::string& text) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int size = 0;
    if (EVP_Digest(text.data(), text.size(), digest, &size, EVP_sha256(), nullptr) != 1)
        throw std::runtime_error("SHA-256 failed");
    std::ostringstream out;
    for (unsigned int i = 0; i < size; ++i)
        out << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(digest[i]);
    return out.str();
}
namespace {
// Transient syntax view; the typed AST below owns strings and never keeps ANTLR pointers.
struct Syntax {
    std::string kind, text;
    SourceSpan span;
    std::vector<Syntax> children;
    const Syntax* first(const std::string& k) const {
        for (auto& c : children)
            if (c.kind == k)
                return &c;
        return nullptr;
    }
    const Syntax& get(const std::string& k) const {
        auto p = first(k);
        if (!p)
            throw std::logic_error("AST rule missing: " + kind + "/" + k);
        return *p;
    }
    std::vector<const Syntax*> all(const std::string& k) const {
        std::vector<const Syntax*> out;
        for (auto& c : children)
            if (c.kind == k)
                out.push_back(&c);
        return out;
    }
};
class ErrorListener final : public antlr4::BaseErrorListener {
    std::string path;
    const std::vector<std::size_t>& offsets;

  public:
    ErrorListener(std::string p, const std::vector<std::size_t>& o) : path(std::move(p)), offsets(o) {}
    void syntaxError(antlr4::Recognizer* recognizer, antlr4::Token* token, size_t line, size_t column,
                     const std::string& message, std::exception_ptr) override {
        auto lexer = dynamic_cast<antlr4::Lexer*>(recognizer);
        auto index = std::min(token   ? token->getStartIndex()
                              : lexer ? lexer->tokenStartCharIndex
                                      : 0,
                              offsets.size() - 1);
        throw Diagnostic("ESyntax", {path, offsets[index], offsets[index], line, column + 1}, message);
    }
};
Syntax copyTree(antlr4::tree::ParseTree* tree, verimcGrammarParser& parser, const std::string& path,
                const std::vector<std::size_t>& offsets) {
    Syntax n;
    auto ctx = dynamic_cast<antlr4::ParserRuleContext*>(tree);
    auto terminal = dynamic_cast<antlr4::tree::TerminalNode*>(tree);
    auto token = ctx ? ctx->getStart() : terminal->getSymbol();
    auto last = ctx ? ctx->getStop() : token;
    n.span = {path, offsets[std::min(token->getStartIndex(), offsets.size() - 1)],
              offsets[std::min(last->getStopIndex() + 1, offsets.size() - 1)], token->getLine(),
              token->getCharPositionInLine() + 1};
    n.kind = ctx ? parser.getRuleNames()[ctx->getRuleIndex()] : "token";
    if (!ctx)
        n.text = tree->getText();
    for (auto c : tree->children)
        n.children.push_back(copyTree(c, parser, path, offsets));
    return n;
}
std::string spelling(const Syntax& s) {
    if (s.kind == "token")
        return s.text;
    std::string out;
    for (auto& c : s.children)
        out += spelling(c);
    return out;
}
ExprPtr expression(const Syntax& s);
ExprPtr makeExpr(const Syntax& s, std::string kind, std::string text, std::vector<ExprPtr> args = {}) {
    return std::make_shared<Expr>(Expr{std::move(kind), std::move(text), std::move(args), s.span});
}
ExprPtr suffixes(const Syntax& s, ExprPtr base) {
    for (auto p : s.all("referenceSuffix")) {
        if (p->first("name"))
            base = makeExpr(*p, "member", spelling(p->get("name")), {base});
        else {
            auto es = p->all("expr");
            std::vector<ExprPtr> args{base};
            for (auto e : es)
                args.push_back(expression(*e));
            base = makeExpr(*p, es.size() == 1 ? "index" : "slice", "", std::move(args));
        }
    }
    return base;
}
ExprPtr expression(const Syntax& s) {
    if (s.kind == "valueName" || s.kind == "qualifiedName" || s.kind == "name")
        return makeExpr(s, "name", spelling(s));
    if (s.kind == "reference")
        return suffixes(s, expression(s.get("name")));
    if (s.kind == "postfix")
        return suffixes(s, expression(s.get("atom")));
    if (s.kind == "call" || (s.kind == "sizeAtom" && s.first("qualifiedName"))) {
        std::vector<ExprPtr> args;
        for (auto& c : s.children)
            if (c.kind == "expr" || c.kind == "sizeExpr")
                args.push_back(expression(c));
        return makeExpr(s, "call", spelling(s.get("qualifiedName")), std::move(args));
    }
    if (s.kind == "arrayExpr") {
        std::vector<ExprPtr> args;
        for (auto p : s.all("expr"))
            args.push_back(expression(*p));
        return makeExpr(s, "array", "", std::move(args));
    }
    if (s.kind == "token")
        return makeExpr(s, (s.text == "true" || s.text == "false") ? "boolean" : "integer", s.text);
    if (s.children.size() == 1)
        return expression(s.children[0]);
    if (s.children[0].kind == "token" && s.children[0].text == "(")
        return expression(s.children[1]);
    if (s.kind == "unary" || s.kind == "sizeUnary")
        return makeExpr(s, "unary", spelling(s.children[0]), {expression(s.children[1])});
    if (s.kind == "conditional")
        return makeExpr(s, "select", "",
                        {expression(s.children[0]), expression(s.children[2]), expression(s.children[4])});
    auto left = expression(s.children[0]);
    for (std::size_t i = 1; i < s.children.size(); i += 2)
        left = makeExpr(s, "binary", spelling(s.children[i]), {left, expression(s.children[i + 1])});
    return left;
}
TypeSyntax typeSyntax(const Syntax& s) {
    TypeSyntax t;
    t.span = s.span;
    t.name = s.first("qualifiedName") ? spelling(s.get("qualifiedName")) : spelling(s.children[0]);
    if (t.name == "[") {
        t.name = "array";
        t.element = std::make_shared<TypeSyntax>(typeSyntax(s.get("type")));
    }
    if (auto p = s.first("sizeExpr"))
        t.size = expression(*p);
    return t;
}
ModuleRef moduleRef(const Syntax& s) {
    ModuleRef r;
    r.name = spelling(s.get("qualifiedName"));
    r.span = s.span;
    for (auto p : s.all("namedArg"))
        r.args.emplace_back(spelling(p->get("name")), expression(p->get("expr")));
    return r;
}
Statement statement(const Syntax& s);
std::vector<Statement> statements(const Syntax& s) {
    std::vector<Statement> out;
    for (auto& c : s.children)
        if (c.kind != "token")
            out.push_back(statement(c));
    return out;
}
Statement statement(const Syntax& s) {
    if (s.kind == "moduleItem" || s.kind == "generateItem" || s.kind == "combStmt" || s.kind == "seqStmt")
        return statement(s.children[0]);
    Statement v;
    v.kind = s.kind;
    v.span = s.span;
    if (auto p = s.first("name"))
        v.name = spelling(*p);
    if (auto p = s.first("type"))
        v.type = typeSyntax(*p);
    if (auto p = s.first("moduleRef"))
        v.module = moduleRef(*p);
    if (auto p = s.first("reference"))
        v.exprs.push_back(expression(*p));
    for (auto p : s.all("expr"))
        v.exprs.push_back(expression(*p));
    if (s.kind == "portDecl")
        v.kind = spelling(s.children[0]);
    if (s.kind == "combIf" || s.kind == "seqIf") {
        v.kind = "if";
        auto key = s.kind == "combIf" ? "combBody" : "seqBody";
        auto bodies = s.all(key);
        v.body = statements(*bodies[0]);
        if (bodies.size() > 1) {
            v.otherwise = statements(*bodies[1]);
            v.hasElse = true;
        } else if (auto p = s.first(s.kind)) {
            v.otherwise = {statement(*p)};
            v.hasElse = true;
        }
    } else if (s.kind == "combMatch" || s.kind == "seqMatch") {
        v.kind = "match";
        for (auto& c : s.children)
            if (c.kind == "combCase" || c.kind == "seqCase" || c.kind == "combDefault" ||
                c.kind == "seqDefault")
                v.body.push_back(statement(c));
    } else if (s.kind == "generateBlock") {
        v.indexName = spelling(*s.all("name")[1]);
        for (auto p : s.all("generateItem"))
            v.body.push_back(statement(*p));
    } else if (auto p = s.first("combBody"))
        v.body = statements(*p);
    else if (auto seq = s.first("seqBody"))
        v.body = statements(*seq);
    return v;
}
Parameter parameter(const Syntax& s) {
    Parameter p;
    p.name = spelling(s.get("name"));
    p.span = s.span;
    p.type = typeSyntax(s.first("type") ? s.get("type") : s.get("compileType"));
    if (auto e = s.first("expr"))
        p.value = expression(*e);
    return p;
}
} // namespace
static SourceFile parseSourceImpl(const std::string& path, const std::string& text, std::size_t maxDepth) {
    // Validate UTF-8 via JSON, then map ANTLR's UTF-32 token indices to byte offsets.
    try {
        (void)Json(text).dump();
    } catch (const Json::exception&) {
        throw Diagnostic("ESyntax", {path}, "Source must be valid UTF-8");
    }
    std::vector<std::size_t> offsets;
    for (std::size_t i = 0; i < text.size(); ++i)
        if ((static_cast<unsigned char>(text[i]) & 0xc0) != 0x80)
            offsets.push_back(i);
    offsets.push_back(text.size());
    antlr4::ANTLRInputStream input(text);
    verimcGrammarLexer lexer(&input);
    ErrorListener listener(path, offsets);
    lexer.removeErrorListeners();
    lexer.addErrorListener(&listener);
    antlr4::CommonTokenStream tokens(&lexer);
    tokens.fill();
    // Bound grammar recursion before entering the generated recursive parser.
    std::size_t nesting = 0, unaryRun = 0, ternaries = 0, tokenCount = 0, operators = 0, branchCount = 0;
    for (auto t : tokens.getTokens()) {
        auto word = t->getText();
        if (++tokenCount > 200000)
            throw Diagnostic("EElaborationLimit", {path}, "Token budget exceeded");
        if (word == "if" || word == "match") {
            if (++branchCount > maxDepth)
                throw Diagnostic("EElaborationLimit", {path}, "Control nesting budget exceeded");
        }
        if (word == "module" || word == "fn")
            branchCount = 0;
        if (word == "+" || word == "-" || word == "*" || word == "/" || word == "%" || word == "&&" ||
            word == "||" || word == "^" || word == "&" || word == "|" || word == "::" || word == ".") {
            if (++operators > maxDepth)
                throw Diagnostic("EElaborationLimit", {path}, "Expression complexity budget exceeded");
        }
        if (word == ";" || word == "{")
            operators = 0;
        if (word == "(" || word == "[" || word == "{") {
            if (++nesting > maxDepth)
                throw Diagnostic("EElaborationLimit", {path}, "Syntax nesting budget exceeded");
        } else if (word == ")" || word == "]" || word == "}") {
            if (nesting)
                --nesting;
        }
        if (word == "!" || word == "~" || word == "-" || word == "+") {
            if (++unaryRun > maxDepth)
                throw Diagnostic("EElaborationLimit", {path}, "Unary nesting budget exceeded");
        } else
            unaryRun = 0;
        if (word == "?") {
            if (++ternaries > maxDepth)
                throw Diagnostic("EElaborationLimit", {path}, "Conditional budget exceeded");
        }
        if (word == ";" || word == "{")
            ternaries = 0;
        if (t->getType() == verimcGrammarLexer::STRING) {
            try {
                (void)Json::parse(word);
            } catch (const Json::exception&) {
                throw Diagnostic("ESyntax", {path}, "Invalid JSON string / Unicode escape");
            }
        }
    }
    verimcGrammarParser parser(&tokens);
    parser.removeErrorListeners();
    parser.addErrorListener(&listener);
    auto root = copyTree(parser.sourceFile(), parser, path, offsets);
    SourceFile file;
    file.path = path;
    file.sha256 = sha256(text);
    file.byteLength = text.size();
    file.package = spelling(root.get("qualifiedName"));
    if (Json::parse(root.children[root.children[0].text == "\xEF\xBB\xBF" ? 2 : 1].text) != "0.1")
        throw Diagnostic("ELanguageVersion", {path}, "Only language 0.1 is supported");
    for (auto p : root.all("importDecl"))
        file.imports.emplace_back(spelling(p->children[3]),
                                  Json::parse(p->children[1].text).get<std::string>());
    for (auto wrapper : root.all("declaration")) {
        auto& s = wrapper->children[0];
        Declaration d;
        d.kind = s.kind;
        d.span = s.span;
        if (auto p = s.first("name"))
            d.name = spelling(*p);
        else
            d.name = s.children[1].text;
        if (auto p = s.first("type"))
            d.type = typeSyntax(*p);
        if (auto p = s.first("expr"))
            d.value = expression(*p);
        if (auto p = s.first("parameters"))
            for (auto a : p->all("parameter"))
                d.parameters.push_back(parameter(*a));
        if (auto p = s.first("functionParams"))
            for (auto a : p->all("functionParam"))
                d.parameters.push_back(parameter(*a));
        if (d.kind == "moduleDecl")
            for (auto p : s.all("moduleItem"))
                d.body.push_back(statement(*p));
        if (d.kind == "functionDecl")
            for (auto p : s.all("letStmt"))
                d.body.push_back(statement(*p));
        if (d.kind == "enumDecl")
            for (std::size_t i = 3; i + 1 < s.children.size(); i += 2)
                d.members.push_back(s.children[i].text);
        file.declarations.push_back(std::move(d));
    }
    return file;
}
SourceFile parseSource(const std::string& path, const std::string& text, std::size_t maxDepth) {
    try {
        return parseSourceImpl(path, text, maxDepth);
    } catch (Diagnostic& error) {
        error.sourceSha256 = sha256(text);
        throw;
    }
}
} // namespace verimc
