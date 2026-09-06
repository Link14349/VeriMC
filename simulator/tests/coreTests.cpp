#include "simulator/simulator.hpp"
#include "simulator/legacyRandom.hpp"
#include <iostream>
#include <functional>
#include <fstream>

using namespace simulator;
namespace {
void expect(bool condition, const std::string& message) { if (!condition) throw std::runtime_error(message); }
void floor(Simulator& s, int start = -4, int end = 20) { for (int x = start; x <= end; ++x) for (int z = -4; z <= 4; ++z) s.world.set({x, 0, z}, s.registry.state("stone")); }
void tripwireLine(Simulator& s) {
    s.place({-1,1,0}, s.registry.state("stone")); s.place({7,1,0}, s.registry.state("stone"));
    s.place({0,1,0}, s.registry.state("tripwire_hook", {{"facing","east"}}));
    s.place({6,1,0}, s.registry.state("tripwire_hook", {{"facing","west"}}));
    for (int x = 1; x < 6; ++x) s.place({x,1,0}, s.registry.state("tripwire"));
    s.advanceTo(20);
}
}
int main() {
    BlockRegistry r; int passed = 0, failed = 0;
    auto test = [&](const std::string& name, const std::function<void()>& run) { try { run(); ++passed; std::cout << "PASS " << name << '\n'; } catch (const std::exception& e) { ++failed; std::cerr << "FAIL " << name << ": " << e.what() << '\n'; } };
    test("all 26.2 property combinations round-trip", [&] {
        std::ifstream file(std::string(SIMULATOR_DATA_DIR) + "/blockStates.json"); auto source = Json::parse(file);
        for (const auto& block : source["blocks"]) for (const auto& state : block["states"]) expect(r.state(block["name"], state["properties"]) == state["id"].get<StateId>(), "state mismatch: " + block["name"].get<std::string>());
    });
    test("Java 26.2 random primitives match exact bits, rejection counts and restored state", [&] {
        std::ifstream file(std::string(SIMULATOR_DATA_DIR) + "/../tests/fixtures/java26_2Random.json");
        const auto fixture=Json::parse(file);
        for (const auto& example:fixture.at("cases")) {
            LegacyRandom random(std::stoull(example.at("seedBits").get<std::string>(),nullptr,16));
            std::size_t index=0;
            for (const auto& call:example.at("calls")) {
                const auto op=call.at("op").get<std::string>();
                auto expectedBits=[&] {return std::stoull(call.at("bits").get<std::string>(),nullptr,16);};
                if (op=="int") expect(random.nextInt()==call.at("value"),"random int");
                else if (op=="bound") expect(random.nextInt(call.at("bound"))==call.at("value"),"random bounded int");
                else if (op=="long") expect(std::bit_cast<std::uint64_t>(random.nextLong())==expectedBits(),"random signed long");
                else if (op=="bool") expect(random.nextBoolean()==call.at("value"),"random bool");
                else if (op=="float") expect(std::bit_cast<std::uint32_t>(random.nextFloat())==expectedBits(),"random float bits");
                else if (op=="double") expect(std::bit_cast<std::uint64_t>(random.nextDouble())==expectedBits(),"random double bits");
                else if (op=="triangleDouble") expect(std::bit_cast<std::uint64_t>(random.triangle(3.75,.123))==expectedBits(),"random double triangle");
                else if (op=="triangleFloat") expect(std::bit_cast<std::uint32_t>(random.triangle(-.25F,.137F))==expectedBits(),"random float triangle");
                else throw std::runtime_error("unknown random fixture operation");
                expect(random.state()==call.at("state") && random.drawCount()==call.at("draws"),"random rejection draw/state mismatch");
                if (++index%37==0) {
                    const auto state=random.state(), draws=random.drawCount();
                    for (int i=0;i<7;++i) random.nextInt();
                    random.restore(state,draws);
                }
            }
        }
        LegacyRandom random; auto state=random.state(); bool threw=false;
        try {random.nextInt(0);} catch (...) {threw=true;}
        expect(threw && random.state()==state && random.drawCount()==0,"invalid bound consumed random state");
        threw=false; try {random.restore(1ULL<<48,10);} catch (...) {threw=true;}
        expect(threw && random.state()==state,"invalid random restore changed state");
    });
    test("Java 26.2 chunk scheduler: backlog, limits and callback queue membership", [&] {
        std::ifstream file(std::string(SIMULATOR_DATA_DIR) + "/../tests/fixtures/java26_2Scheduler.json");
        auto fixture = Json::parse(file); BlockTicks ticks;
        auto read = [](const Json& row) { return ScheduledEvent{row.at("tick"), row.at("priority"), row.at("order"), row.at("pos").get<BlockPos>(), row.at("type")}; };
        for (const auto& row : fixture.at("initial")) ticks.schedule(read(row));
        for (const auto& run : fixture.at("runs")) {
            ticks.collect(run.at("tick"), run.at("limit"));
            std::size_t index = 0;
            while (ticks.hasBatch()) {
                const auto event = ticks.pop();
                expect(index < run.at("events").size(), "extra collected tick");
                const auto& expected = run.at("events").at(index);
                expect(event.pos == expected.at("pos").get<BlockPos>() && event.type == expected.at("type"), "cross-chunk drain order differs");
                for (const auto& injection : run.at("injections")) if (injection.at("after") == index) ticks.schedule(read(injection.at("event")));
                std::string queued, collected;
                for (const auto& row : fixture.at("probes")) {
                    auto probe = read(row);
                    queued += ticks.hasScheduled(probe.pos, probe.type) ? '1' : '0';
                    collected += ticks.willTick(probe.pos, probe.type) ? '1' : '0';
                }
                expect(queued == expected.at("queued").get<std::string>() && collected == expected.at("collected").get<std::string>(), "hasScheduled / willTick membership differs");
                ++index;
            }
            expect(index == run.at("events").size() && ticks.size() == run.at("remaining"), "collection limit or deferred callbacks differ");
        }
    });
    test("dropper selection and random checkpoint agree with 600 original slot draws", [&] {
        std::ifstream file(std::string(SIMULATOR_DATA_DIR) + "/../tests/fixtures/java26_2DropperSlots.json");
        const auto fixture=Json::parse(file);
        for (const auto& example:fixture.at("cases")) {
            Simulator s(r); s.place({0,0,0},r.state("dropper",{{"facing","east"}})); s.place({1,0,0},r.state("barrel"));
            s.setRandomSeed(std::stoull(example.at("seedBits").get<std::string>(),nullptr,16));
            int index=0;
            for (const auto& call:example.at("calls")) {
                const int mask=call.at("mask"), chosen=call.at("slot"); Json inventory=Json::array();
                for (int slot=0;slot<9;++slot) inventory.push_back({{"slot",slot},{"item","stone"},{"count",(mask&(1<<slot))?1:0}});
                s.stimulate({0,0,0},{{"inventory",inventory}});s.place({-1,0,0},r.state("redstone_block"));s.advanceTo(s.currentTick+4);s.setBlock({-1,0,0},0);
                int remaining=0;for(const auto& stack:s.inventoryJson({0,0,0}))remaining|=1<<stack.at("slot").get<int>();
                expect(remaining==(chosen<0?mask:mask&~(1<<chosen)),"dropper chose a different slot");
                auto snapshot=s.saveProject("dropper",true);
                expect(snapshot["randomSource"]["state"]==call.at("state") && snapshot["randomSource"]["draws"]==std::to_string(call.at("draws").get<std::uint64_t>()),"dropper consumed different random draws");
                if (++index%17==0) {Simulator restored(r);restored.loadProject(snapshot);s.restore(restored);}
            }
            auto before=s.saveProject("random",true), invalid=before;invalid["randomSource"]["seed"]="-1";bool threw=false;
            try{s.loadProject(invalid);}catch(...){threw=true;}
            expect(threw && s.saveProject("random",true)==before,"invalid random seed import was not atomic");
        }
    });
    test("dropper external actions preserve paused batches, inventory and checkpoint continuation", [&] {
        Simulator s(r);
        for(int x : {0,8}) {
            s.place({x,0,0},r.state("dropper",{{"facing","east"}}));
            s.stimulate({x,0,0},{{"inventory",Json::array({{{"slot",0},{"item","stone"},{"count",2}}})}});
            s.place({x-1,0,0},r.state("redstone_block"));
        }
        s.advanceTo(100);
        expect(s.currentTick==4 && s.hasPendingActions() && s.pendingEvents()==1 && !s.faulted,"external output did not pause at atomic event boundary");
        auto checkpoint=s.saveProject("actions",true); Simulator restored(r);restored.loadProject(checkpoint);
        expect(checkpoint==restored.saveProject("actions",true),"pending action checkpoint changed");
        for(auto* sim : {&s,&restored}) {
            sim->breakRequested=false;expect(!sim->stepEvent() && sim->currentTick==4,"manual event bypassed pending output");
            sim->breakRequested=false;expect(sim->advanceTo(100)==0 && sim->currentTick==4,"idle advance bypassed pending output");
            bool threw=false;try{sim->saveProject();}catch(...){threw=true;}expect(threw,"design export silently discarded pending feedback");
            auto action=sim->pendingActionsJson()[0];expect(action["count"]==1 && action["item"]=="minecraft:stone","wrong emitted stack");
            expect(sim->inventoryJson(action["source"].get<BlockPos>())[0]["count"]==1,"emission failed to consume inventory");
            sim->resolveAction(action["id"]);sim->breakRequested=false;sim->advanceActive();
            expect(sim->currentTick==4 && sim->hasPendingActions() && sim->actionHistory().size()==2,"remaining batch was lost or ran past next output");
            sim->resolveAction(sim->pendingActionsJson()[0]["id"]);sim->breakRequested=false;sim->advanceTo(5);
            expect(!sim->hasPendingActions() && sim->currentTick==5,"resolved output did not resume");
        }
        expect(s.saveProject("actions",true)==restored.saveProject("actions",true),"random output diverged after checkpoint");
        const auto before=s.saveProject("before",true);
        for (const auto& patch : std::vector<Json>{{{"id",-1}},{{"id",1.5}},{{"kind","unknown"}},{{"count",0}},{{"item","minecraft:air"}},{{"velocity",Json::array({0,1})}},{{"tick",99}}}) {
            auto bad=before;bad["environmentActions"][0].update(patch);bool threw=false;
            try{s.loadProject(bad);}catch(...){threw=true;}
            expect(threw && before==s.saveProject("before",true),"bad action import partially replaced world");
        }
    });
    test("dropper emission matches original entity position, velocity bits and random state", [&] {
        std::ifstream file(std::string(SIMULATOR_DATA_DIR) + "/../tests/fixtures/java26_2DropperMotion.json");
        const auto fixture=Json::parse(file);
        for(const auto& row:fixture.at("cases")) {
            Simulator s(r);const auto pos=row.at("source").get<BlockPos>();const auto direction=parseDirection(row.at("facing"));
            s.place(pos,r.state("dropper",{{"facing",row.at("facing")}}));
            s.stimulate(pos,{{"inventory",Json::array({{{"slot",4},{"item","stone"},{"count",2}}})}});
            s.setRandomSeed(std::stoull(row.at("seedBits").get<std::string>(),nullptr,16));
            s.place(pos.relative(opposite(direction)),r.state("redstone_block"));s.advanceTo(4);
            expect(s.hasPendingActions(),"dropper did not emit external item");
            const auto action=s.pendingActionsJson()[0];
            for(const auto* field:{"position","velocity"})for(std::size_t i=0;i<3;++i)
                expect(std::bit_cast<std::uint64_t>(action[field][i].get<double>())==std::stoull(row[std::string(field)+"Bits"][i].get<std::string>(),nullptr,16),std::string("original dropper ")+field+" differs");
            expect(s.saveProject("dropper",true)["randomSource"]["state"]==row.at("randomState"),"ejection consumed different world randomness");
            expect(s.inventoryJson(pos)[0]["count"]==row.at("remaining") && action["count"]==row.at("count"),"emission inventory differs");
        }
    });
    test("block tick cap defers backlog and zero delay waits for next collection", [&] {
        BlockTicks ticks;
        for (std::uint64_t i = 0; i < 65537; ++i) ticks.schedule({1, 0, i, {static_cast<int>(i), 0, 0}, 1});
        ticks.collect(1);
        expect(ticks.batchEvents().size() == 65536 && ticks.queuedEvents().size() == 1, "vanilla per-tick limit");
        while (ticks.hasBatch()) ticks.pop();
        expect(ticks.nextTick() == 2, "overdue tick ran in the same collection");
        ticks.collect(2); expect(ticks.pop().order == 65536, "deferred tick lost");
        ticks.finishThrough(100); ticks.schedule({100, 0, 65537, {0, 0, 0}, 1});
        expect(ticks.nextTick() == 101, "zero delay after idle ran before next tick");
    });
    test("checkpoint resumes a collected batch with a future tick at the same position", [&] {
        Simulator s(r); BlockPos first{0,0,0}, second{1,0,0};
        s.world.set(first, r.state("observer", {{"facing", "east"}}));
        s.world.set(second, r.state("observer", {{"facing", "west"}}));
        s.schedule(first, 2); s.schedule(second, 2); s.stepEvent();
        auto saved = s.saveProject("batch", true);
        expect(saved.at("blockTickState").at("batch").size() == 1, "lost current batch");
        expect(s.hasScheduled(second), "observer could not queue while its current tick was collected");
        Simulator restored(r); restored.loadProject(saved);
        auto clone = s.clone();
        for (int i = 0; i < 12; ++i) {
            s.stepEvent(); restored.stepEvent(); clone->stepEvent();
            expect(s.saveProject("batch", true) == restored.saveProject("batch", true) && s.saveProject("batch", true) == clone->saveProject("batch", true), "batch checkpoint continuation diverged");
        }
        auto invalid = saved; invalid["blockTickState"]["batch"].push_back(invalid["blockTickState"]["batch"][0]);
        auto before = restored.saveProject("before", true); bool threw = false;
        try { restored.loadProject(invalid); } catch (...) { threw = true; }
        expect(threw && before == restored.saveProject("before", true), "invalid batch import changed world");
    });
    for (const auto* fixtureName : {"java26_2Redstone", "java26_2Devices", "java26_2Containers", "java26_2Hoppers", "java26_2Torches", "java26_2Rails", "java26_2TickBatches", "java26_2Tripwire", "java26_2Buttons", "java26_2Droppers", "java26_2Targets", "java26_2CopperChests", "java26_2Bookshelves"}) test(std::string("Java 26.2 differential: ") + fixtureName, [&] {
        std::ifstream file(std::string(SIMULATOR_DATA_DIR) + "/../tests/fixtures/" + fixtureName + ".json"); expect(static_cast<bool>(file), "missing vanilla reference fixture");
        auto fixture = Json::parse(file); auto origin = fixture["origin"].get<BlockPos>(); Simulator s(r);
        auto absolute = [&](const Json& p) { auto pos = p.get<BlockPos>(); return BlockPos{origin.x + pos.x, origin.y + pos.y, origin.z + pos.z}; };
        for (const auto& frame : fixture["frames"]) {
            Tick tick = frame["tick"]; s.advanceTo(tick);
            for (const auto& command : fixture["commands"]) if (command["tick"] == tick) {
                auto p = absolute(command["pos"]);
                if (command.contains("placedBy") || command.contains("playerPlace")) s.place(p, command["stateId"]);
                else if (command.contains("stateId")) s.setBlock(p, command["stateId"]);
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
    test("target world-coordinate rounding matches 4272 original hit strengths", [&] {
        std::ifstream file(std::string(SIMULATOR_DATA_DIR)+"/../tests/fixtures/java26_2TargetStrength.json");
        auto fixture=Json::parse(file);
        for(const auto& row:fixture.at("cases")) {
            Simulator s(r);const auto pos=row.at("source").get<BlockPos>();s.place(pos,r.state("target"));
            for(const auto& sample:row.at("samples")) {
                s.stimulate(pos,{{"face",row.at("face")},{"hit",sample.at("hit")},{"arrow",true}});
                expect(s.at(pos).power==sample.at("power"),"target differs near world-coordinate threshold");
                s.advanceTo(s.currentTick+20);expect(s.at(pos).power==0,"arrow pulse did not release");
            }
        }
    });
    test("target rejects direct strength edits and preserves pulse on invalid input and restore", [&] {
        Simulator s(r);const BlockPos pos{-3,2,-7};s.place(pos,r.state("target"));
        s.stimulate(pos,{{"face","up"},{"hit",Json::array({.5,1,.5})},{"arrow",false}});s.advanceTo(3);
        const auto before=s.saveProject("target",true);
        for(const auto& input:std::vector<Json>{{{"value",15}},{{"face","invalid"},{"hit",Json::array({.5,1,.5})}},{{"face","up"},{"hit",Json::array({-.01,1,.5})}},{{"face","up"},{"hit",Json::array({.5,1,.5})},{"arrow","false"}}}) {
            bool threw=false;try{s.stimulate(pos,input);}catch(...){threw=true;}
            expect(threw && before==s.saveProject("target",true),"invalid target input modified pulse");
        }
        Simulator restored(r);restored.loadProject(before);
        for(auto* sim:{&s,&restored}) {
            sim->stimulate(pos,{{"face","north"},{"hit",Json::array({0,.5,0})},{"arrow",true}});
            sim->advanceTo(7);expect(sim->at(pos).power==15,"repeat hit changed existing power");
            sim->advanceTo(8);expect(sim->at(pos).power==0,"repeat hit extended duration");
        }
        expect(s.saveProject("target",true)==restored.saveProject("target",true),"target checkpoint diverged");
    });
    test("negative chunk boundaries and empty chunk reclamation", [&] { World w; for (int i = -33; i < 33; ++i) w.set({i, i - 16, -i}, 7); expect(w.size() == 66, "count"); for (int i = -33; i < 33; ++i) expect(w.get({i, i -16, -i}) == 7, "negative coordinate"); for (int i = -33; i < 33; ++i) w.set({i, i - 16, -i}, 0); expect(w.chunkCount() == 0, "empty chunks leaked"); });
    test("tripwire ordinary break pulses, shears disarm, and hook support detaches", [&] {
        Simulator s(r); tripwireLine(s);
        expect(r.property(s.world.get({0,1,0}), "attached") == "true", "manual span did not attach");
        s.setBlock({3,1,0}, 0);
        expect(s.at({0,1,0}).powered && s.at({6,1,0}).powered, "ordinary break omitted pulse");
        s.advanceTo(29); expect(s.at({0,1,0}).powered, "break pulse ended early");
        s.advanceTo(30); expect(!s.at({0,1,0}).powered, "broken span remained powered");
        Simulator sheared(r); tripwireLine(sheared); sheared.addProbe({0,1,0}, "cut");
        sheared.stimulate({3,1,0}, {{"shear",true}});
        expect(sheared.world.get({3,1,0}) == 0 && !sheared.at({0,1,0}).powered, "shears produced a break pulse");
        for (const auto& edge : sheared.getTrace()) expect(edge.value == 0, "shears produced a transient pulse");
        Simulator detached(r); tripwireLine(detached); detached.setBlock({-1,1,0}, 0);
        expect(detached.world.get({0,1,0}) == 0 && r.property(detached.world.get({6,1,0}), "attached") == "false", "support destruction skipped hook notifications");
    });
    test("tripwire re-entry waits for zero-delay tick and restores contact phase", [&] {
        Simulator s(r); tripwireLine(s);
        s.stimulate({3,1,0}, {{"entities",1}}); s.stimulate({3,1,0}, {{"entities",0}});
        s.advanceTo(30); expect(!s.at({3,1,0}).powered && s.hasScheduled({3,1,0}), "release lost zero-delay guard");
        s.place({8,1,0}, r.state("piston", {{"facing","east"}}));
        s.stimulate({3,1,0}, {{"entities",1}});
        expect(!s.at({3,1,0}).powered, "re-entry bypassed pending recheck");
        auto saved = s.saveProject("contact", true); Simulator restored(r); restored.loadProject(saved);
        s.advanceTo(31); restored.advanceTo(31);
        expect(s.at({3,1,0}).powered && !s.at({8,1,0}).extended, "contact did not run between block events and block entities");
        s.advanceTo(34); restored.advanceTo(34);
        expect(s.at({8,1,0}).extended && s.saveProject("contact",true) == restored.saveProject("contact",true), "contact snapshot lost piston activation");
        expect(s.signal({3,1,0}, Direction::up) == 0 && s.displayValue({3,1,0}) == 15, "tripwire emitted electrical power directly");
        auto before = s.saveProject("before", true); bool threw = false;
        try { s.stimulate({3,1,0}, {{"entities",-1}}); } catch (...) { threw = true; }
        expect(threw && before == s.saveProject("before", true), "invalid contact input changed state");
    });
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
    test("trace backpressure completes atomic edges and resumes without loss", [&] {
        Simulator s(r); s.place({0,0,0}, r.state("observer", {{"facing","east"}}));
        for (int i = 0; i < 8; ++i) s.addProbe({0,0,0});
        s.schedule({0,0,0}, 2); s.traceCapacity = 10; s.retainTraceFrom(0);
        auto baseline = s.clone(); baseline->traceCapacity = 100;
        s.advanceTo(100);
        expect(s.currentTick == 2 && s.getTrace().size() == 16 && s.traceDropped == 0, "atomic edge batch dropped or time advanced");
        expect(s.traceBlocked() && s.breakRequested && !s.faulted, "soft capacity did not pause");
        const auto pending = s.pendingEvents(); const auto events = s.statistics.scheduledEvents;
        s.breakRequested = false; expect(!s.stepEvent() && s.pendingEvents() == pending && s.statistics.scheduledEvents == events, "blocked event was consumed");
        s.breakRequested = false; s.advanceTo(100); expect(s.currentTick == 2, "blocked advance bypassed protection");
        const auto snapshot = s.saveProject("trace", true);
        Simulator restored(r); restored.traceCapacity = 10; restored.loadProject(snapshot);
        expect(restored.saveProject("trace", true) == snapshot, "snapshot truncated atomic trace reserve");
        std::vector<TraceEdge> received(s.getTrace().begin(), s.getTrace().end());
        s.retainTraceFrom(16); expect(s.traceDropped == 6 && !s.traceBlocked(), "ACK failed to release history");
        s.breakRequested = false; s.advanceTo(100);
        for (std::uint64_t i = 16; i < s.traceDropped + s.getTrace().size(); ++i) received.push_back(s.getTrace()[i - s.traceDropped]);
        baseline->advanceTo(100);
        expect(received.size() == baseline->getTrace().size(), "edge count changed across pause");
        for (std::size_t i = 0; i < received.size(); ++i) {
            const auto& a = received[i]; const auto& b = baseline->getTrace()[i];
            expect(a.probeId == b.probeId && a.tick == b.tick && a.sequence == b.sequence && a.value == b.value, "edge changed across pause");
        }
        bool threw = false; try { s.retainTraceFrom(0); } catch (...) { threw = true; }
        expect(threw, "expired ACK cursor accepted");
    });
    test("shallow arrow release and re-press retain same-tick phase and checkpoint", [&] {
        Simulator s(r); s.world.set({0,-1,0},r.state("stone"));
        s.place({0,0,0},r.state("oak_button",{{"face","floor"}})); s.addProbe({0,0,0});
        s.stimulate({0,0,0},{{"arrows",1},{"pressedArrows",0}});
        expect(s.at({0,0,0}).powered,"arrow did not press");
        s.stepEvent(); expect(s.currentTick==30 && !s.at({0,0,0}).powered,"small pressed shape did not release");
        auto checkpoint=s.saveProject("arrow",true); Simulator restored(r); restored.loadProject(checkpoint);
        s.stepEvent(); restored.stepEvent();
        expect(s.currentTick==30 && s.at({0,0,0}).powered,"entity contact lost same-tick re-press");
        expect(s.saveProject("arrow",true)==restored.saveProject("arrow",true),"arrow contact phase checkpoint diverged");
        const auto& trace=s.getTrace();
        expect(trace.size()==4 && trace[2].tick==trace[3].tick && trace[2].value==0 && trace[3].value==15 && trace[2].sequence<trace[3].sequence,"same-tick edges merged");
        s.stimulate({0,0,0},{{"arrows",0}}); s.advanceTo(59); expect(s.at({0,0,0}).powered,"arrow removal released before poll");
        s.advanceTo(60); expect(!s.at({0,0,0}).powered,"arrow removal never released");
        auto before=s.saveProject("before",true); bool threw=false;
        try { s.stimulate({0,0,0},{{"arrows",0},{"pressedArrows",1}}); } catch (...) {threw=true;}
        expect(threw && s.saveProject("before",true)==before,"invalid footprint counts changed state");
    });
    test("stone ignores arrows and repeated clicks do not extend button pulses", [&] {
        for (const auto& name : {"stone_button", "polished_blackstone_button", "oak_button"}) {
            Simulator s(r); s.world.set({0,-1,0},r.state("stone")); s.place({0,0,0},r.state(name,{{"face","floor"}}));
            const bool wood=std::string(name)=="oak_button";
            s.stimulate({0,0,0},{{"arrows",1}}); expect(s.at({0,0,0}).powered==wood,"arrow material filter");
            s.interact({0,0,0}); s.advanceTo(10); s.interact({0,0,0}); s.stimulate({0,0,0},{{"arrows",0}});
            s.advanceTo(wood?30:20); expect(!s.at({0,0,0}).powered,"click reset release deadline");
        }
    });
    test("trace safety reserve faults explicitly and idle time cannot bypass pause", [&] {
        Simulator idle(r); idle.addProbe({0,0,0}); idle.traceCapacity = 1; idle.retainTraceFrom(0);
        idle.breakRequested = false; idle.advanceTo(100); expect(idle.currentTick == 0 && idle.breakRequested, "idle time bypassed full trace");
        Simulator s(r); s.place({0,0,0}, r.state("observer"));
        for (int i = 0; i < 8; ++i) s.addProbe({0,0,0});
        s.traceCapacity = 10; s.traceAtomicReserve = 2; s.retainTraceFrom(0); s.schedule({0,0,0},2);
        bool threw = false; try { s.stepEvent(); } catch (...) { threw = true; }
        expect(threw && s.faulted && s.traceDropped == 0, "reserve exhaustion silently lost edges");
        s.retainTraceFrom(s.getTrace().size()); s.breakRequested = false;
        threw = false; try { s.stepEvent(); } catch (...) { threw = true; }
        expect(threw, "faulted trace execution resumed");
    });
    test("active execution ends at the last event and preserves budgeted clock continuation", [&] {
        Simulator s(r); expect(s.advanceActive()==0 && s.currentTick==0,"empty active run advanced time");
        s.place({0,0,0},r.state("observer")); s.schedule({0,0,0},2);
        expect(s.advanceActive()==2 && s.currentTick==4 && s.pendingEvents()==0,"active run added trailing idle time");
        s.advanceActive(); expect(s.currentTick==4,"stable active run advanced again");
        s.advanceTo(1000); expect(s.currentTick==1000,"explicit time advance lost idle semantics");
        Simulator clock(r);
        clock.world.set({0,0,0},r.state("observer",{{"facing","east"}}));
        clock.world.set({1,0,0},r.state("observer",{{"facing","west"}}));
        clock.schedule({0,0,0},2); clock.schedule({1,0,0},2);
        expect(clock.advanceActive(3)==3 && clock.pendingEvents()>0,"active event budget lost clock work");
        auto saved=clock.saveProject("active",true); Simulator restored(r); restored.loadProject(saved);
        clock.advanceActive(17); restored.advanceActive(17);
        expect(clock.saveProject("active",true)==restored.saveProject("active",true),"active continuation diverged");
    });
    test("probe dependency indices survive deletion and snapshot cloning", [&] {
        Simulator s(r); s.place({0,0,0}, r.state("observer"));
        const auto removed = s.addProbe({0,0,0}), kept = s.addProbe({0,0,0});
        s.removeProbe(removed); auto cloned = s.clone();
        for (auto* sim : {&s, cloned.get()}) {
            sim->clearTrace(); sim->schedule({0,0,0},2); sim->advanceTo(4);
            expect(sim->getTrace().size() == 3, "shifted probe missed edges");
            for (const auto& edge : sim->getTrace()) expect(edge.probeId == kept, "sampled removed probe");
        }
        expect(s.saveProject("probe",true) == cloned->saveProject("probe",true), "cloned indices diverged");
    });
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
    test("copper chest pairing and variant changes retain inventory and open state across checkpoint", [&] {
        Simulator s(r);const BlockPos first{0,0,0},second{1,0,0};
        s.place(first,r.state("oxidized_copper_chest",{{"facing","north"}}));
        s.stimulate(first,{{"inventory",Json::array({{{"slot",0},{"item","stone"},{"count",8}}})}});
        s.place(second,r.state("waxed_exposed_copper_chest",{{"facing","north"}}));
        expect(r.type(s.world.get(first)).name=="minecraft:exposed_copper_chest" && r.type(s.world.get(second)).name=="minecraft:exposed_copper_chest","mixed wax pairing did not choose unwaxed lower oxidation");
        expect(s.inspect(first)["inventorySize"]==54 && s.inventoryJson(first)[0]["slot"]==27,"paired inventory order or preservation failed");
        s.stimulate(first,{{"viewers",2}});
        const auto saved=s.saveProject("copper",true);Simulator restored(r);restored.loadProject(saved);
        for(auto* sim:{&s,&restored}) {
            sim->setBlock(first,r.state("waxed_weathered_copper_chest",{{"facing","north"},{"type","left"}}));
            expect(r.type(sim->world.get(second)).name=="minecraft:waxed_weathered_copper_chest","shape update failed to synchronize variant");
            expect(sim->viewerCount(first)==2 && sim->viewerCount(second)==2 && sim->inventoryJson(first)[0]["count"]==8,"variant conversion erased inventory or viewers");
            sim->setBlock(second,r.state("chest",{{"facing","north"}}));
            expect(sim->inspect(first)["inventorySize"]==27 && sim->inventoryJson(first)[0]["count"]==8,"unrelated chest connected or erased retained half");
            expect(sim->viewerCount(second)==0,"unrelated replacement kept old viewer state");
        }
        expect(s.saveProject("copper",true)==restored.saveProject("copper",true),"copper conversion diverged after checkpoint");
    });
    test("bookshelf edits retain operation order, last-slot history and atomic validation", [&] {
        Simulator s(r);const BlockPos pos{0,0,0};s.place(pos,r.state("chiseled_bookshelf"));
        s.stimulate(pos,{{"inventory",Json::array({{{"slot",5},{"item","book"},{"count",1}},{{"slot",0},{"item","knowledge_book"},{"count",1}}})}});
        expect(s.analogOutput(pos)==1 && r.property(s.world.get(pos),"slot_5_occupied")=="true","bookshelf reordered edits or lost occupied state");
        const auto before=s.saveProject("shelf",true);
        for(const auto& inventory:std::vector<Json>{Json::array({{{"slot",2},{"item","book"},{"count",1}},{{"slot",3},{"item","stone"},{"count",1}}}),Json::array({{{"slot",1},{"item","book"},{"count",2}}})}) {
            bool threw=false;try{s.stimulate(pos,{{"inventory",inventory}});}catch(...){threw=true;}
            expect(threw && s.saveProject("shelf",true)==before,"invalid bookshelf batch partially modified slots");
        }
        s.stimulate(pos,{{"inventory",Json::array({{{"slot",0},{"count",0}},{{"slot",5},{"count",0}}})}});
        expect(s.inventoryJson(pos).empty() && s.analogOutput(pos)==6,"empty shelf forgot last interaction");
        s.stimulate(pos,{{"inventory",Json::array({{{"slot",2},{"count",0}}})}});expect(s.analogOutput(pos)==6,"removing empty slot changed history");
        auto saved=s.saveProject("emptyShelf",true);Simulator restored(r);restored.loadProject(saved);
        expect(saved==restored.saveProject("emptyShelf",true),"empty shelf checkpoint lost history");
        auto invalid=saved;invalid["blockData"][0]["values"]["lastInteractedSlot"]=6;bool threw=false;
        try{restored.loadProject(invalid);}catch(...){threw=true;}
        expect(threw && restored.saveProject("emptyShelf",true)==saved,"invalid shelf checkpoint was not atomic");
    });
    test("blocked bookshelf extraction sleeps without changing history and wakes on capacity", [&] {
        Simulator s(r);s.place({0,1,0},r.state("chiseled_bookshelf"));s.place({0,0,0},r.state("hopper",{{"facing","east"}}));
        s.stimulate({0,1,0},{{"inventory",Json::array({{{"slot",0},{"item","book"},{"count",1}},{{"slot",5},{"item","enchanted_book"},{"count",1}}})}});
        Json filled=Json::array();for(int i=0;i<5;++i) filled.push_back({{"slot",i},{"item","stone"},{"count",63}});
        s.stimulate({0,0,0},{{"inventory",filled}});s.advanceTo(10000);
        expect(s.analogOutput({0,1,0})==6 && s.pendingEvents()==0 && s.statistics.scheduledEvents==1,"failed precheck changed shelf or kept idle polling");
        s.stimulate({0,0,0},{{"inventory",Json::array({{{"slot",1},{"count",0}}})}});auto saved=s.saveProject("blockedShelf",true);Simulator restored(r);restored.loadProject(saved);
        s.advanceTo(10001);restored.advanceTo(10001);
        expect(s.analogOutput({0,1,0})==1 && s.inventoryJson({0,0,0})[1]["item"]=="minecraft:book","shelf failed to wake and extract first book");
        expect(s.saveProject("shelf",true)==restored.saveProject("shelf",true),"shelf/hopper checkpoint diverged");
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
