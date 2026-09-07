#include "irFixtures.hpp"
#include "verimc/vmclJson.hpp"
#include <fstream>
#include <iostream>
#include <sstream>

int main(int argc, char** argv) {
    try {
        using namespace verimc;
        using namespace verimc::test;
        require(argc == 2, "Expected v1 fixture");
        std::ifstream input(argv[1]);
        std::ostringstream bytes;
        bytes << input.rdbuf();
        require(input.good(), "Cannot read fixture");
        auto legacy = readVmclJson(bytes.str());
        require(writeVmclJson(legacy) == bytes.str(), "Legacy v1 bytes changed after IR import/export");
        auto g = graph();
        Integer large = (Integer(1) << 255) + 17;
        g.nodes = {node(NodeKind::Constant, scalar(TypeKind::UInt, 256), {}, ConstantAttributes{large}),
                   node(NodeKind::Constant, scalar(TypeKind::Int, 256), {}, ConstantAttributes{-large + 20})};
        g.instances.front().parameters["large"] = large;
        auto text = writeVmclJson(g);
        auto restored = readVmclJson(text);
        require(std::get<ConstantAttributes>(restored.nodes.front().attrs).value == large,
                "Large integer roundtrip");
        require(std::get<ConstantAttributes>(restored.nodes.at(1).attrs).value == -large + 20,
                "Signed integer roundtrip");
        require(restored.instances.front().parameters.at("large") == large, "Parameter roundtrip");
        require(writeVmclJson(restored) == text, "Nondeterministic roundtrip");
        restored.nodes.front().attrs = ShiftAttributes{1};
        rejected([&] { writeVmclJson(restored); }); // Writer validates caller-created IR too.
        auto malformed = text;
        auto position = malformed.find(large.str());
        malformed.insert(position, "0");
        rejected([&] { readVmclJson(malformed); });
        rejected([&] { readVmclJson("{\"format\":1,\"format\":2}"); });
        rejected([&] { readVmclJson("[broken"); });
        std::cout << "Legacy v1 compatibility and typed roundtrips passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
