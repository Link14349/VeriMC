#include "simulator/simulator.hpp"
#include <fstream>
#include <iostream>

using namespace simulator;

// Dumps every 26.2 block type with the support level the kernel actually assigns, so the
// coverage inventory is generated from the registry rather than maintained by hand.
int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "Usage: exportSupportInventory out.json\n"; return 2; }
    BlockRegistry registry;
    Json rows = Json::array();
    for (std::size_t index = 0; index < registry.typeCount(); ++index) {
        const auto& type = registry.blockType(static_cast<std::uint16_t>(index));
        rows.push_back({{"name", type.name}, {"className", type.className},
                        {"device", static_cast<unsigned>(type.device)}, {"supportLevel", type.supportLevel}});
    }
    std::ofstream out(argv[1]);
    if (!out) { std::cerr << "Cannot write " << argv[1] << '\n'; return 2; }
    out << Json{{"version", "26.2"}, {"types", std::move(rows)}}.dump();
    std::cout << "Exported " << registry.typeCount() << " block types\n";
    return 0;
}
