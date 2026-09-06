#include "simulator/simulator.hpp"
#include <iostream>
#include <functional>
#include <fstream>

using namespace simulator;
namespace {
void expect(bool condition, const std::string& message) { if (!condition) throw std::runtime_error(message); }
void floor(Simulator& s, int start = -4, int end = 20) { for (int x = start; x <= end; ++x) for (int z = -4; z <= 4; ++z) s.world.set({x, 0, z}, s.registry.state("stone")); }
}
int main() {
    BlockRegistry r; int passed = 0, failed = 0;
    auto test = [&](const std::string& name, const std::function<void()>& run) { try { run(); ++passed; std::cout << "PASS " << name << '\n'; } catch (const std::exception& e) { ++failed; std::cerr << "FAIL " << name << ": " << e.what() << '\n'; } };
    test("all 26.2 property combinations round-trip", [&] {
        std::ifstream file(std::string(SIMULATOR_DATA_DIR) + "/blockStates.json"); auto source = Json::parse(file);
        for (const auto& block : source["blocks"]) for (const auto& state : block["states"]) expect(r.state(block["name"], state["properties"]) == state["id"].get<StateId>(), "state mismatch: " + block["name"].get<std::string>());
    });
    test("Java 26.2 GameTest differential trace", [&] {
        std::ifstream file(std::string(SIMULATOR_DATA_DIR) + "/../tests/fixtures/java26_2Redstone.json"); expect(static_cast<bool>(file), "missing vanilla reference fixture");
        auto fixture = Json::parse(file); auto origin = fixture["origin"].get<BlockPos>(); Simulator s(r);
        auto absolute = [&](const Json& p) { auto pos = p.get<BlockPos>(); return BlockPos{origin.x + pos.x, origin.y + pos.y, origin.z + pos.z}; };
        for (const auto& frame : fixture["frames"]) {
            Tick tick = frame["tick"]; s.advanceTo(tick);
            for (const auto& command : fixture["commands"]) if (command["tick"] == tick) s.setBlock(absolute(command["pos"]), command["stateId"]);
            for (std::size_t i = 0; i < fixture["watch"].size(); ++i) {
                auto p = absolute(fixture["watch"][i]); auto expected = frame["states"][i].get<StateId>();
                expect(s.world.get(p) == expected, "tick " + std::to_string(tick) + " position " + fixture["watch"][i].dump() + " expected " + r.describe(expected).dump() + " got " + r.describe(s.world.get(p)).dump());
                if (frame["analogs"][i] != -1) expect(s.analogOutput(p) == frame["analogs"][i].get<int>(), "comparator block entity mismatch");
            }
        }
    });
    test("negative chunk boundaries and empty chunk reclamation", [&] { World w; for (int i = -33; i < 33; ++i) w.set({i, i - 16, -i}, 7); expect(w.size() == 66, "count"); for (int i = -33; i < 33; ++i) expect(w.get({i, i -16, -i}) == 7, "negative coordinate"); for (int i = -33; i < 33; ++i) w.set({i, i - 16, -i}, 0); expect(w.chunkCount() == 0, "empty chunks leaked"); });
    test("wire attenuation and zero-delay settling", [&] { Simulator s(r); floor(s); for (int x = 1; x <= 17; ++x) s.place({x, 1, 0}, r.state("redstone_wire")); s.place({0, 1, 0}, r.state("redstone_block")); for (int x = 1; x <= 17; ++x) expect(s.at({x, 1, 0}).power == std::max(16 - x, 0), "wire at " + std::to_string(x)); expect(s.currentTick == 0, "wire introduced tick delay"); s.setBlock({0, 1, 0}, 0); for (int x = 1; x <= 17; ++x) expect(s.at({x, 1, 0}).power == 0, "wire failed to decay"); });
    test("strong power relays through a solid cube", [&] { Simulator s(r); floor(s); s.place({1, 1, 0}, r.state("stone")); s.place({2, 1, 0}, r.state("redstone_wire")); s.place({1, 2, 0}, r.state("lever", {{"face", "floor"}})); s.interact({1, 2, 0}); expect(s.at({2, 1, 0}).power == 15, "lever strong output"); s.interact({1, 2, 0}); expect(s.at({2, 1, 0}).power == 0, "strong output off"); });
    test("repeater stretches a one-game-tick input to configured delay", [&] { Simulator s(r); floor(s); s.place({1, 1, 0}, r.state("repeater", {{"facing", "west"}, {"delay", "2"}})); s.place({2, 1, 0}, r.state("redstone_wire")); s.place({0, 1, 0}, r.state("redstone_block")); s.advanceTo(1); s.setBlock({0, 1, 0}, 0); s.advanceTo(3); expect(!s.at({1, 1, 0}).powered, "early output"); s.advanceTo(4); expect(s.at({1, 1, 0}).powered && s.at({2, 1, 0}).power == 15, "pulse disappeared"); s.advanceTo(7); expect(s.at({1, 1, 0}).powered, "pulse too short"); s.advanceTo(8); expect(!s.at({1, 1, 0}).powered, "pulse did not end"); });
    test("side repeater locks state", [&] { Simulator s(r); floor(s); s.place({1, 1, 0}, r.state("repeater", {{"facing", "west"}})); s.place({1, 1, 1}, r.state("repeater", {{"facing", "south"}, {"powered", "true"}})); expect(s.at({1, 1, 0}).locked, "lock not set by side diode"); s.place({0, 1, 0}, r.state("redstone_block")); s.advanceTo(10); expect(!s.at({1, 1, 0}).powered, "locked repeater changed"); s.setBlock({1, 1, 1}, 0); s.advanceTo(12); expect(s.at({1, 1, 0}).powered, "unlock did not resume"); });
    test("observer shape update has two-tick latency and pulse", [&] { Simulator s(r); s.place({0, 0, 0}, r.state("observer", {{"facing", "east"}})); s.place({1, 0, 0}, r.state("stone")); s.advanceTo(1); expect(!s.at({0, 0, 0}).powered, "early observer"); s.advanceTo(2); expect(s.at({0, 0, 0}).powered, "observer failed"); s.advanceTo(4); expect(!s.at({0, 0, 0}).powered, "observer pulse duration"); });
    test("torch delay and delayed lamp switch-off", [&] { Simulator s(r); floor(s); s.place({0, 1, 0}, r.state("redstone_torch")); s.place({0, 2, 0}, r.state("redstone_lamp")); expect(s.at({0, 2, 0}).lit, "lamp off"); s.place({1, 0, 0}, r.state("lever", {{"face", "wall"}, {"facing", "east"}})); s.interact({1, 0, 0}); s.advanceTo(1); expect(s.at({0, 1, 0}).lit, "torch delay"); s.advanceTo(2); expect(!s.at({0, 1, 0}).lit, "torch input"); s.advanceTo(5); expect(s.at({0, 2, 0}).lit, "lamp delay"); s.advanceTo(6); expect(!s.at({0, 2, 0}).lit, "lamp remains lit"); });
    test("copper bulb toggles on each rising edge", [&] { Simulator s(r); s.place({0, 0, 0}, r.state("copper_bulb")); s.place({1, 0, 0}, r.state("redstone_block")); expect(s.at({0, 0, 0}).lit, "first edge"); s.setBlock({1, 0, 0}, 0); expect(s.at({0, 0, 0}).lit, "falling edge toggled"); s.place({1, 0, 0}, r.state("redstone_block")); expect(!s.at({0, 0, 0}).lit, "second edge"); });
    test("comparator analog source and subtraction", [&] { Simulator s(r); floor(s); s.place({0, 1, 0}, r.state("copper_bulb", {{"lit", "true"}})); s.place({1, 1, 0}, r.state("comparator", {{"facing", "west"}})); s.advanceTo(2); expect(s.analogOutput({1, 1, 0}) == 15, "analog source"); s.place({1, 1, 1}, r.state("redstone_block")); s.interact({1, 1, 0}); expect(s.analogOutput({1, 1, 0}) == 0 && !s.at({1, 1, 0}).powered, "subtract equality"); });
    test("button duration, trace and exact checkpoint continuation", [&] { Simulator s(r); floor(s); s.place({0, 1, 0}, r.state("stone_button", {{"face", "floor"}})); s.addProbe({0, 1, 0}, "button"); s.interact({0, 1, 0}); s.advanceTo(7); auto saved = s.saveProject("test", true); Simulator other(r); other.loadProject(saved); s.advanceTo(20); other.advanceTo(20); expect(!s.at({0, 1, 0}).powered, "button did not release"); expect(s.saveProject("test", true) == other.saveProject("test", true), "checkpoint lost runtime state"); expect(s.exportVcd().find("#1000") != std::string::npos, "VCD game-tick scale"); });
    test("invalid project import leaves current world intact", [&] { Simulator s(r); s.place({0, 0, 0}, r.state("stone")); auto before = s.saveProject("test", true); auto bad = before; bad["blocks"][0]["name"] = "minecraft:not_a_block"; bool threw = false; try { s.loadProject(bad); } catch (...) { threw = true; } expect(threw && before == s.saveProject("test", true), "load not atomic"); });
    test("piston motion checkpoint continues across event phases", [&] { Simulator s(r); s.place({0,0,0},r.state("sticky_piston",{{"facing","east"}})); s.place({1,0,0},r.state("stone")); s.place({-1,0,0},r.state("redstone_block")); s.advanceTo(1); expect(s.at({2,0,0}).device == Device::movingPiston, "missing moving block"); auto snapshot=s.saveProject("moving",true); Simulator restored(r); restored.loadProject(snapshot); s.advanceTo(4); restored.advanceTo(4); expect(s.saveProject("moving",true)==restored.saveProject("moving",true),"motion checkpoint diverged"); expect(s.at({2,0,0}).device==Device::solid,"push failed"); s.setBlock({-1,0,0},0); s.advanceTo(8); expect(s.world.get({1,0,0})==r.state("stone") && s.world.get({2,0,0})==0,"sticky pull failed"); });
    test("update budget abort cannot resume after discarding updates", [&] { Simulator s(r); s.world.set({1,-1,0},r.state("stone")); s.place({1,0,0},r.state("redstone_wire")); auto before = s.clone(); s.updateBudget=1; bool threw=false; try { s.place({0,0,0},r.state("redstone_block")); } catch (...) { threw=true; } expect(threw&&s.faulted,"budget did not abort"); s.breakRequested=false; threw=false; try { s.advanceTo(1); } catch (...) { threw=true; } expect(threw,"faulted run resumed"); s.restore(*before); expect(!s.faulted&&s.world.size()==before->world.size(),"snapshot failed to recover"); });
    std::cout << passed << " passed, " << failed << " failed\n"; return failed ? 1 : 0;
}
