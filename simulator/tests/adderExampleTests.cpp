#include "simulator/simulator.hpp"
#include <array>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>

using namespace simulator;

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Usage: adderExampleTests <adder8.json>");
        std::ifstream input(argv[1]);
        const auto project = Json::parse(input);
        const auto& ports = project.at("example").at("ports");
        BlockRegistry registry;
        Simulator sim(registry);
        sim.loadProject(project);
        unsigned cases = 0;
        Tick longestSettling = 0;
        auto settle = [&] {
            const auto start = sim.currentTick;
            const auto target = start + 2000;
            while (sim.currentTick < target && !sim.breakRequested) sim.advanceTo(target);
            if (sim.breakRequested || sim.faulted || sim.pendingEvents())
                throw std::runtime_error("Adder failed to settle: " + sim.pauseReason);
            if (sim.world.size() != project.at("blocks").size())
                throw std::runtime_error("A block disappeared during simulation");
            for (const auto& edge : sim.getTrace())
                if (edge.tick >= start) longestSettling = std::max(longestSettling, edge.tick - start);
            sim.clearTrace();
            sim.takeChanges();
        };
        auto setLever = [&](const Json& location, bool value) {
            const auto pos = location.get<BlockPos>();
            if (sim.at(pos).powered != value) sim.interact(pos);
        };
        auto check = [&](unsigned a, unsigned b, unsigned carryIn, bool setInputs = true) {
            if (setInputs) {
                for (unsigned bit = 0; bit < 8; ++bit) {
                    setLever(ports.at("a").at(bit), (a >> bit) & 1u);
                    setLever(ports.at("b").at(bit), (b >> bit) & 1u);
                }
                setLever(ports.at("carryIn"), carryIn != 0);
            }
            settle();
            unsigned result = 0;
            for (unsigned bit = 0; bit < 8; ++bit) {
                if (sim.displayValue(ports.at("sum").at(bit).get<BlockPos>())) result |= 1u << bit;
                const unsigned mask = (1u << (bit + 1)) - 1;
                const bool expectedCarry = ((a & mask) + (b & mask) + carryIn) > mask;
                if ((sim.displayValue(ports.at("carry").at(bit).get<BlockPos>()) != 0) != expectedCarry)
                    throw std::runtime_error("Wrong carry C" + std::to_string(bit + 1) + " at " + std::to_string(a) + "+" + std::to_string(b) + "+" + std::to_string(carryIn));
            }
            if (sim.displayValue(ports.at("carry").at(7).get<BlockPos>())) result |= 256;
            if (result != a + b + carryIn)
                throw std::runtime_error("Wrong sum: " + std::to_string(a) + "+" + std::to_string(b) + "+" + std::to_string(carryIn) + "=" + std::to_string(result));
            ++cases;
        };
        check(37, 19, 0, false);
        // All eight local full-adder truth-table rows, at each physical bit.
        for (unsigned bit = 0; bit < 8; ++bit)
            for (unsigned pattern = 0; pattern < 8; ++pattern) {
                const unsigned carry = pattern & 1;
                const unsigned a = ((pattern >> 2) << bit) | (carry && bit ? (1u << bit) - 1 : 0);
                const unsigned b = (((pattern >> 1) & 1) << bit) | (carry && bit ? 1 : 0);
                check(a, b, bit == 0 ? carry : 0);
            }
        // Boundaries, overflow, alternating bits, and long carry chains.
        constexpr std::array<unsigned, 10> boundaries{0, 1, 2, 15, 16, 85, 127, 128, 170, 255};
        for (auto a : boundaries) for (auto b : boundaries)
            for (unsigned carry = 0; carry < 2; ++carry) check(a, b, carry);
        for (unsigned a = 0; a < 256; ++a) check(a, 255 - a, 1);
        std::mt19937 random(20260907);
        for (unsigned index = 0; index < 256; ++index) {
            const auto a = static_cast<unsigned>(random()) & 255u;
            const auto b = static_cast<unsigned>(random()) & 255u;
            const auto carry = static_cast<unsigned>(random()) & 1u;
            check(a, b, carry);
        }
        // Check falling transitions and return to the shipped input state.
        check(255, 255, 1); check(0, 0, 0); check(37, 19, 0);
        std::cout << "PASS " << cases << " input transitions; every sum and carry; "
                  << sim.world.size() << " blocks; maximum observed output settling "
                  << longestSettling << " gt\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
