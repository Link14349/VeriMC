#include "irFixtures.hpp"
#include "verimc/compiler.hpp"
#include <iostream>
#include <type_traits>

static_assert(std::is_same_v<decltype(verimc::compileFile(verimc::CompileOptions{})), verimc::LogicGraph>);
int main(int argc, char** argv) {
    try {
        using namespace verimc;
        test::require(argc == 2, "Expected example source");
        CompileOptions options;
        options.input = argv[1];
        options.top = "Counter";
        auto g = compileFile(options);
        validateLogicGraph(g);
        std::size_t registers = 0;
        for (auto& n : g.nodes)
            if (n.op == NodeKind::Register) {
                ++registers;
                test::require(n.type.kind == TypeKind::UInt && n.type.width == 4, "Counter register type");
                test::require(std::get<RegisterAttributes>(n.attrs).resetValue == 0, "Counter reset value");
                test::require(g.nodes.at(n.registerInput(RegisterInput::Clock)).type.kind == TypeKind::Clock,
                              "Counter clock");
                std::get<RegisterAttributes>(n.attrs).resetValue = 5;
            }
        test::require(registers == 1, "Counter register count");
        validateLogicGraph(g);
        std::cout << "Compiler API returns editable IR without JSON adapter\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
