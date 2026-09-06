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
    for (const auto* fixtureName : {"java26_2Redstone", "java26_2Devices", "java26_2Containers", "java26_2Hoppers", "java26_2Torches", "java26_2Rails"}) test(std::string("Java 26.2 differential: ") + fixtureName, [&] {
        std::ifstream file(std::string(SIMULATOR_DATA_DIR) + "/../tests/fixtures/" + fixtureName + ".json"); expect(static_cast<bool>(file), "missing vanilla reference fixture");
        auto fixture = Json::parse(file); auto origin = fixture["origin"].get<BlockPos>(); Simulator s(r);
        auto absolute = [&](const Json& p) { auto pos = p.get<BlockPos>(); return BlockPos{origin.x + pos.x, origin.y + pos.y, origin.z + pos.z}; };
        for (const auto& frame : fixture["frames"]) {
            Tick tick = frame["tick"]; s.advanceTo(tick);
            for (const auto& command : fixture["commands"]) if (command["tick"] == tick) {
                auto p = absolute(command["pos"]);
                if (command.contains("stateId")) s.setBlock(p, command["stateId"]);
                else if (command.contains("interact")) s.interact(p);
                else s.stimulate(p, command["stimulus"]);
            }
            for (std::size_t i = 0; i < fixture["watch"].size(); ++i) {
                auto p = absolute(fixture["watch"][i]); auto expected = frame["states"][i].get<StateId>();
                expect(s.world.get(p) == expected, "tick " + std::to_string(tick) + " position " + fixture["watch"][i].dump() + " expected " + r.describe(expected).dump() + " got " + r.describe(s.world.get(p)).dump());
                if (frame["analogs"][i] != -1) expect(s.analogOutput(p) == frame["analogs"][i].get<int>(), "analog mismatch at " + std::to_string(tick) + " " + fixture["watch"][i].dump());
                if (frame.contains("inventories")) expect(s.inventoryJson(p, false) == frame["inventories"][i], "inventory mismatch at " + std::to_string(tick) + " " + fixture["watch"][i].dump() + " expected " + frame["inventories"][i].dump() + " got " + s.inventoryJson(p, false).dump());
            }
        }
    });
    test("negative chunk boundaries and empty chunk reclamation", [&] { World w; for (int i = -33; i < 33; ++i) w.set({i, i - 16, -i}, 7); expect(w.size() == 66, "count"); for (int i = -33; i < 33; ++i) expect(w.get({i, i -16, -i}) == 7, "negative coordinate"); for (int i = -33; i < 33; ++i) w.set({i, i - 16, -i}, 0); expect(w.chunkCount() == 0, "empty chunks leaked"); });
    test("world cache survives rehash, deletion, copying and ownership transfer", [&] {
        World world; const BlockPos point{-1, -17, 31};
        expect(world.get(point) == 0, "initial empty read"); world.set(point, 7);
        expect(world.get(point) == 7, "cached miss survived insertion");
        World copy = world; world.set(point, 9);
        expect(copy.get(point) == 7 && world.get(point) == 9, "copy shared mutable storage");
        for (int i = 0; i < 4096; ++i) world.set({i * 16, 32, -48}, 5);
        expect(world.get(point) == 9, "rehash invalidated chunk");
        World moved = std::move(world);
        expect(world.size() == 0 && world.get(point) == 0 && moved.get(point) == 9, "moved-from world retained cached pointers");
        moved.set(point, 0); expect(moved.get(point) == 0, "deleted chunk read after free");
        moved.set(point, 8); moved.get(point); moved.clear(); expect(moved.get(point) == 0, "clear retained cache");
        moved = std::move(copy); expect(moved.get(point) == 7 && copy.get(point) == 0, "move assignment retained old cache");
        copy = moved; moved.set(point, 1); expect(copy.get(point) == 7, "copy assignment alias");
    });
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
    test("daylight numerical agreement and dirty-only polling", [&] {
        std::ifstream file(std::string(SIMULATOR_DATA_DIR) + "/../tests/fixtures/java26_2Daylight.json");
        auto fixture = Json::parse(file); Simulator s(r); BlockPos pos{0,0,0}; s.place(pos, r.state("daylight_detector"));
        for (const auto& sample : fixture.at("samples")) for (int sky = 0; sky <= 15; ++sky) {
            s.stimulate(pos, {{"skyBrightness", sky}, {"sunAngle", sample.at("angle")}});
            s.interact(pos); expect(s.at(pos).power == 15 - sky, "inverted daylight"); s.interact(pos);
            expect(s.at(pos).power == sample.at("outputs").at(static_cast<std::size_t>(sky)).get<int>(), "daylight float/table mismatch");
        }
        s.advanceTo(20); expect(s.pendingEvents() == 0, "constant sky caused perpetual idle polls");
        s.stimulate(pos, {{"skyBrightness", 0}, {"sunAngle", 0}}); s.advanceTo(39);
        expect(s.at(pos).power == 15, "daylight skipped 20-gt boundary"); s.advanceTo(40); expect(s.at(pos).power == 0, "daylight dirty input missed");
    });
    test("paired door placement, upper-half editing and support loss", [&] {
        Simulator s(r); floor(s); BlockPos lower{0,1,0}, upper{0,2,0};
        s.place(lower,r.state("oak_door")); expect(s.at(upper).device==Device::door,"door upper half absent");
        s.interact(upper); expect(r.property(s.world.get(lower),"open")=="true","upper-half operation not synchronized");
        s.setBlock({0,0,0},0); expect(s.world.get(lower)==0&&s.world.get(upper)==0,"unsupported door remained");
    });
    test("environment checkpoint preserves pressure release and lectern pulse", [&] {
        Simulator s(r); floor(s); s.place({0,1,0},r.state("heavy_weighted_pressure_plate")); s.place({3,1,0},r.state("lectern"));
        s.stimulate({0,1,0},{{"entities",11}}); expect(s.at({0,1,0}).power==2,"weighted count rounding");
        s.stimulate({3,1,0},{{"pages",15}}); s.stimulate({3,1,0},{{"page",7}}); expect(s.analogOutput({3,1,0})==8,"page progress");
        s.advanceTo(1); s.stimulate({0,1,0},{{"entities",0}}); auto saved=s.saveProject("environment",true); Simulator restored(r); restored.loadProject(saved);
        s.advanceTo(10); restored.advanceTo(10); expect(s.at({0,1,0}).power==0&&!s.at({3,1,0}).powered,"environment release timing");
        expect(s.saveProject("environment",true)==restored.saveProject("environment",true),"environment checkpoint diverged: " + Json::diff(s.saveProject("environment",true),restored.saveProject("environment",true)).dump());
        auto invalid=saved; for(auto& row:invalid["blockData"]) if(row["values"].contains("pages")) row["values"]["page"]=500;
        auto before=restored.saveProject("before",true); bool threw=false; try {restored.loadProject(invalid);}catch(...){threw=true;}
        expect(threw&&restored.saveProject("before",true)==before,"invalid runtime import was not atomic");
    });
    test("paired inventory capacity, split order and snapshot round-trip", [&] {
        Simulator s(r); floor(s); s.place({0,1,0},r.state("chest")); s.place({1,1,0},r.state("chest"));
        expect(s.inspect({0,1,0})["inventorySize"]==54,"chests did not pair");
        Json inventory=Json::array(); for(int i=0;i<27;++i) inventory.push_back({{"slot",i},{"item","stone"},{"count",64}});
        inventory.push_back({{"slot",27},{"item","wooden_sword"},{"count",1}});
        s.stimulate({0,1,0},{{"inventory",inventory}}); expect(s.analogOutput({0,1,0})==8,"double-chest fullness");
        auto saved=s.saveProject("inventory",true); Simulator restored(r); restored.loadProject(saved);
        expect(saved==restored.saveProject("inventory",true),"inventory snapshot diverged");
        s.setBlock({1,1,0},0); expect(s.inspect({0,1,0})["inventorySize"]==27,"chest did not split");
        auto remaining=s.inspect({0,1,0})["inventory"]; expect(remaining.size()==1&&remaining[0]["item"]=="minecraft:wooden_sword","double-chest physical slot order");
        s.stimulate({0,1,0},{{"inventory",Json::array({{{"slot",0},{"count",0}}})}}); saved=s.saveProject("empty",true); restored.loadProject(saved);
        expect(saved==restored.saveProject("empty",true),"empty inventory snapshot diverged");
    });
    test("invalid inventory edits reject the whole batch", [&] {
        Simulator s(r); s.place({0,0,0},r.state("barrel")); auto before=s.saveProject("before",true);
        bool threw=false; try { s.stimulate({0,0,0},{{"inventory",Json::array({{{"slot",0},{"item","stone"},{"count",64}},{{"slot",1},{"item","wooden_sword"},{"count",2}}})}}); } catch(...) {threw=true;}
        expect(threw&&s.saveProject("before",true)==before,"partial invalid inventory edit");
        s.stimulate({0,0,0},{{"inventory",Json::array({{{"slot",0},{"item","ender_pearl"},{"count",16}}})}});
        expect(s.analogOutput({0,0,0})==1,"item-specific stack capacity");
    });
    test("idle hoppers skip empty ticks and wake on inventory or topology", [&] {
        Simulator s(r);
        for (int i = 0; i < 5000; ++i) s.place({i * 2, 0, 0}, r.state("hopper"));
        s.advanceTo(1); auto events = s.statistics.scheduledEvents;
        expect(events == 5000 && s.pendingEvents() == 0, "initial empty hopper checks");
        s.advanceTo(1000000);
        expect(s.statistics.scheduledEvents == events, "empty hoppers kept ticking");
        s.place({0, -1, 0}, r.state("barrel"));
        s.stimulate({0, 0, 0}, {{"inventory", Json::array({{{"slot", 0}, {"item", "stone"}, {"count", 2}}})}});
        s.advanceTo(1000001);
        expect(s.inventoryJson({0, -1, 0})[0]["count"] == 1, "sleeping hopper did not wake");
        s.place({1, 0, 0}, r.state("redstone_block")); s.advanceTo(1000020);
        expect(s.inventoryJson({0, -1, 0})[0]["count"] == 1, "locked hopper transferred");
        s.setBlock({1, 0, 0}, 0); s.advanceTo(1000021);
        expect(s.inventoryJson({0, -1, 0})[0]["count"] == 2, "unlock did not wake hopper");
    });
    test("hopper checkpoints preserve cooldown and persistent entity order", [&] {
        Simulator s(r);
        for (int x = 3; x >= 0; --x) s.place({x, 0, 0}, r.state("hopper", {{"facing", "east"}}));
        s.place({4, 0, 0}, r.state("barrel"));
        s.stimulate({0, 0, 0}, {{"inventory", Json::array({{{"slot", 0}, {"item", "stone"}, {"count", 8}}})}});
        auto circuit = s.saveProject("order"); Simulator design(r); design.loadProject(circuit);
        expect(design.saveProject("order")["entityOrder"] == circuit["entityOrder"], "circuit lost ticker registration order");
        for (int event = 0; event < 48 && s.pendingEvents(); ++event) {
            auto checkpoint = s.saveProject("hopper", true); Simulator restored(r); restored.loadProject(checkpoint);
            expect(restored.saveProject("hopper", true) == checkpoint, "checkpoint changed queue or inventory");
            s.stepEvent(); restored.stepEvent();
            expect(s.saveProject("hopper", true) == restored.saveProject("hopper", true), "resuming inside block entity phase diverged");
        }
        s.advanceTo(100); design.advanceTo(100);
        expect(s.inventoryJson({4, 0, 0}) == design.inventoryJson({4, 0, 0}), "design execution lost transfer order");
        expect(s.inventoryJson({4, 0, 0})[0]["count"] == 8, "chain lost or duplicated items");
        Simulator invalid(r); invalid.place({0,0,0},r.state("hopper"));
        auto before = invalid.saveProject("before", true), bad = before;
        bad["hoppers"][0]["wakeAt"] = 42;
        bool threw = false; try { invalid.loadProject(bad); } catch (...) { threw = true; }
        expect(threw && invalid.saveProject("before",true) == before, "inconsistent hopper checkpoint was accepted");
    });
    test("failed container extraction retains observable retry updates", [&] {
        Simulator s(r); s.place({0, 0, 0}, r.state("hopper", {{"facing", "east"}}));
        s.place({0, 1, 0}, r.state("barrel"));
        Json slots = Json::array(); for (int i = 0; i < 4; ++i) slots.push_back({{"slot", i}, {"item", "stone"}, {"count", 64}});
        slots.push_back({{"slot", 4}, {"item", "ender_pearl"}, {"count", 15}});
        s.stimulate({0, 0, 0}, {{"inventory", slots}});
        s.stimulate({0, 1, 0}, {{"inventory", Json::array({{{"slot", 0}, {"item", "wooden_sword"}, {"count", 1}}})}});
        s.addProbe({0, 1, 0}); s.advanceTo(8);
        expect(s.getTrace().size() > 8, "transient extraction notifications were lost");
        expect(s.inventoryJson({0, 1, 0})[0]["count"] == 1, "failed extraction lost item");
    });
    test("removed torch retains world burnout history across checkpoint", [&] {
        Simulator s(r); floor(s); const BlockPos torch{0,1,0}, lever{-1,0,0};
        s.place(torch, r.state("redstone_torch"));
        s.place(lever, r.state("lever", {{"face","wall"},{"facing","west"}}));
        for (int cycle = 0; cycle < 7; ++cycle) {
            s.interact(lever); s.advanceTo(s.currentTick + 2);
            s.interact(lever); s.advanceTo(s.currentTick + 2);
        }
        expect(s.currentTick == 28 && s.at(torch).lit, "seven-cycle preparation");
        s.setBlock(torch, 0); auto checkpoint = s.saveProject("removed torch", true);
        expect(checkpoint["torchToggles"].size() == 7, "removal discarded world history");
        Simulator restored(r); restored.loadProject(checkpoint);
        for (auto* sim : {&s, &restored}) {
            sim->place(torch, r.state("redstone_torch")); sim->interact(lever); sim->advanceTo(30);
            sim->interact(lever); sim->advanceTo(32);
            expect(!sim->at(torch).lit, "replacement bypassed burnout");
            sim->advanceTo(189); expect(!sim->at(torch).lit, "burnout restarted early");
            sim->advanceTo(190); expect(sim->at(torch).lit, "burnout did not recover");
        }
        expect(s.saveProject("torch", true) == restored.saveProject("torch", true), "torch history continuation diverged");
    });
    test("detector cart inventory selection, delayed release and checkpoint", [&] {
        Simulator s(r); floor(s); const BlockPos detector{0,1,0};
        s.place(detector, r.state("detector_rail"));
        s.place({0,1,1}, r.state("comparator", {{"facing","north"}}));
        Json full = Json::array(); for (int i = 0; i < 5; ++i) full.push_back({{"slot",i},{"item","stone"},{"count",64}});
        s.stimulate(detector, {{"carts", Json::array({{{"type","minecart"}},{{"type","chest_minecart"},{"inventory",Json::array({{{"slot",0},{"item","wooden_sword"},{"count",1}}})}},{{"type","hopper_minecart"},{"inventory",full}}})}});
        expect(s.cartCount(detector)==3 && s.analogOutput(detector)==1, "did not select first container cart");
        s.advanceTo(2); expect(s.analogOutput({0,1,1})==1, "comparator did not read cart");
        s.advanceTo(5); s.stimulate(detector, {{"carts",Json::array()}});
        expect(s.at(detector).powered && s.analogOutput(detector)==0, "cart departure should retain rail power until poll");
        auto checkpoint=s.saveProject("cart",true); Simulator restored(r); restored.loadProject(checkpoint);
        s.advanceTo(19); expect(s.at(detector).powered,"early detector release");
        s.advanceTo(22); restored.advanceTo(22);
        expect(!s.at(detector).powered && s.analogOutput({0,1,1})==0,"detector did not release");
        expect(s.saveProject("cart",true)==restored.saveProject("cart",true),"cart checkpoint diverged");
        auto before=s.saveProject("before",true); bool threw=false;
        try { s.stimulate(detector, {{"carts",Json::array({{{"type","hopper_minecart"},{"inventory",Json::array({{{"slot",5},{"item","stone"},{"count",1}}})}}})}}); } catch (...) {threw=true;}
        expect(threw&&s.saveProject("before",true)==before,"invalid cart inventory partially changed state");
    });
    test("powered rail state is observable without emitting redstone", [&] {
        Simulator s(r); floor(s); s.place({0,1,0},r.state("powered_rail")); s.place({-1,1,0},r.state("redstone_block"));
        expect(s.at({0,1,0}).powered&&s.displayValue({0,1,0})==15,"rail power not visible");
        for (auto direction:directions) expect(s.signal({0,1,0},direction)==0,"powered rail emitted external redstone");
    });
    std::cout << passed << " passed, " << failed << " failed\n"; return failed ? 1 : 0;
}
