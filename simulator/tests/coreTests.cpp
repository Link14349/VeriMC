#include "simulator/simulator.hpp"
#include "simulator/referenceReplay.hpp"
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
    test("palette defaults reproduce native placement before any world state is sent", [&] {
        for (const auto& item : r.catalog()) {
            const auto id = item.at("defaultState").get<StateId>();
            expect(r.state(item.at("name"), item.at("defaultProperties")) == id, "palette defaults changed placement state");
            expect(item.at("defaultProperties") == r.describe(id).at("properties"), "palette defaults disagree with state registry");
        }
    });
    test("all 26.2 property combinations round-trip", [&] {
        std::ifstream file(std::string(SIMULATOR_DATA_DIR) + "/blockStates.json"); auto source = Json::parse(file);
        for (const auto& block : source["blocks"]) for (const auto& state : block["states"]) expect(r.state(block["name"], state["properties"]) == state["id"].get<StateId>(), "state mismatch: " + block["name"].get<std::string>());
        for (StateId invalid : {static_cast<StateId>(r.stateCount()), UINT32_MAX}) {
            bool rejected = false;
            try { (void)r[invalid]; } catch (const std::out_of_range&) { rejected = true; }
            expect(rejected, "aligned state lookup omitted its bounds check");
        }
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
            ticks.collect(run.at("tick"), {}, run.at("limit"));
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
    // 显式动作闭环：投掷器抛出 → 外部环境反馈（声明落点）→ 漏斗按既有原版路径再吸收。
    // 抛出的初始位置与速度直接取自既有原版实测 java26_2DropperMotion.json（facing=east、
    // seedBits=0 那一例，逐位比较）；落点的声明形式与既有原版实测 java26_2HopperPickup.json
    // 里的 groundItems 外部刺激完全一致，漏斗吸取走的是同一条已经与原版对照过的路径。
    // **抛出点到落点之间那一段（物品在空中飞行）是外部输入，不是本内核实现的物品运动**：
    // 内核既不模拟也不推断实体运动，落点由 fixture 显式声明。
    // 「投掷器→空气→落点→漏斗」这个组合场景本身还缺一次原版捕获（本轮不跑捕获）。
    auto vanillaEject = [&] {
        std::ifstream file(std::string(SIMULATOR_DATA_DIR) + "/../tests/fixtures/java26_2DropperMotion.json");
        expect(static_cast<bool>(file), "missing vanilla dropper motion fixture");
        const auto fixture = Json::parse(file);
        for (const auto& row : fixture.at("cases")) if (row.at("facing") == "east" && row.at("seedBits") == "0") return row;
        throw std::runtime_error("missing vanilla dropper motion case");
    };
    // extraDropper：第二台投掷器晚一刻再抛一次，用来构造「fixture 未声明的动作」。
    // stock：投掷器初始物品数，1 表示抛出后容器变空（比较器输出 1→0）。
    auto ejectClosure = [&](const Json& motion, bool extraDropper, int stock) {
        const auto origin = motion.at("source").get<BlockPos>();
        const auto dropper = r.state("dropper", {{"facing", "east"}});
        const auto triggered = r.state("dropper", {{"facing", "east"}, {"triggered", "true"}});
        const auto hopper = r.state("hopper", {{"facing", "down"}});
        auto command = [](Tick tick, Json pos, const char* key, Json value) {
            Json result = Json::object(); result["tick"] = tick; result["pos"] = pos; result[key] = value; return result;
        };
        Json inventory = Json::object();
        inventory["inventory"] = Json::array({Json{{"slot", 0}, {"item", "stone"}, {"count", stock}}});
        Json landing = Json::object();
        landing["groundItems"] = Json::array({Json{{"item", "minecraft:stone"}, {"count", 1}, {"y", 0.72}}});
        Json commands = Json::array();
        commands.push_back(command(0, Json::array({0, 0, 0}), "stateId", dropper));
        commands.push_back(command(0, Json::array({0, 0, 0}), "stimulus", inventory));
        commands.push_back(command(0, Json::array({3, 0, 0}), "stateId", hopper));
        commands.push_back(command(0, Json::array({-1, 0, 0}), "stateId", r.state("redstone_block")));
        if (extraDropper) {
            commands.push_back(command(0, Json::array({0, 0, 4}), "stateId", dropper));
            commands.push_back(command(0, Json::array({0, 0, 4}), "stimulus", inventory));
            commands.push_back(command(1, Json::array({-1, 0, 4}), "stateId", r.state("redstone_block")));
        }
        // 外部环境反馈：落点是显式输入，出现在漏斗的吸取体积里。
        commands.push_back(command(8, Json::array({3, 0, 0}), "stimulus", landing));
        Json declaration = Json::object();
        declaration["index"] = 0; declaration["tick"] = 4; declaration["order"] = 0; declaration["kind"] = "itemEjected";
        declaration["source"] = Json::array({0, 0, 0}); declaration["item"] = "minecraft:stone"; declaration["count"] = 1;
        declaration["position"] = motion.at("position"); declaration["positionBits"] = motion.at("positionBits");
        declaration["velocity"] = motion.at("velocity"); declaration["velocityBits"] = motion.at("velocityBits");
        Json frames = Json::array();
        for (Tick tick = 0; tick <= 20; ++tick) {
            Json frame = Json::object();
            frame["tick"] = tick;
            frame["states"] = Json::array({triggered, hopper});
            frame["analogs"] = Json::array({-1, -1});
            // 第 8 刻声明的落点必须真的落在吸取体积里，第 20 刻必须已经被吸走。
            if (tick == 8) frame["groundItems"] = Json::array({nullptr, Json::array({Json{{"item", "minecraft:stone"}, {"count", 1}}})});
            if (tick == 20) {
                frame["groundItems"] = Json::array({nullptr, Json::array()});
                frame["inventories"] = Json::array({stock > 1 ? Json::array({Json{{"slot", 0}, {"item", "minecraft:stone"}, {"count", stock - 1}}}) : Json::array(),
                                                    Json::array({Json{{"slot", 0}, {"item", "minecraft:stone"}, {"count", 1}}})});
            }
            frames.push_back(frame);
        }
        Json fixture = Json::object();
        fixture["origin"] = origin;
        fixture["watch"] = Json::array({Json::array({0, 0, 0}), Json::array({3, 0, 0})});
        fixture["randomSeed"] = 0; fixture["endTick"] = 20;
        fixture["commands"] = commands; fixture["frames"] = frames;
        fixture["expectedActions"] = Json::array({declaration});
        return fixture;
    };
    test("dropper ejection, declared landing and hopper pickup close the loop", [&] {
        const auto motion = vanillaEject();
        const auto fixture = ejectClosure(motion, false, 2);
        Simulator s(r);
        const auto outcome = replayReferenceFixture(s, fixture);
        expect(outcome.at("status") == "match", "闭环重放未通过：" + outcome.dump());
        const auto origin = fixture.at("origin").get<BlockPos>();
        const BlockPos hopper{origin.x + 3, origin.y, origin.z};
        expect(s.actionHistory().size() == 1 && s.actionHistory().front().at("resolved") == true && !s.hasPendingActions(),
               "抛出动作没有按记录顺序逐条确认");
        expect(s.suckableItems(hopper).empty() && s.inventoryJson(hopper, false).size() == 1,
               "漏斗没有吸走外部声明的落点物品");
        expect(s.saveProject("eject", true)["randomSource"]["state"] == motion.at("randomState"),
               "抛出消耗的世界随机数与原版实测不一致");
    });
    test("declared external actions reject undeclared, mismatched and unrelated pauses", [&] {
        const auto motion = vanillaEject();
        auto replay = [&](const Json& fixture, const std::function<void(Simulator&)>& prepare) {
            Simulator s(r);
            if (prepare) prepare(s);
            try {
                const auto outcome = replayReferenceFixture(s, fixture);
                return outcome.at("status") == "match" ? std::string() : "difference " + outcome.dump();
            } catch (const std::exception& error) { return std::string(error.what()); }
        };
        auto mustFail = [&](const Json& fixture, const std::string& fragment, const std::string& what,
                            const std::function<void(Simulator&)>& prepare = {}) {
            const auto message = replay(fixture, prepare);
            expect(message.find(fragment) != std::string::npos, what + "；实际：" + (message.empty() ? std::string("match") : message));
        };
        const auto base = ejectClosure(motion, false, 2);
        auto edited = [&](const std::function<void(Json&)>& change) { Json copy = base; change(copy); return copy; };
        // 未声明任何外部动作：不允许把暂停当成成功。
        mustFail(edited([](Json& f) { f.erase("expectedActions"); }), "requires explicit external-action feedback", "没有声明也让重放通过了");
        mustFail(edited([](Json& f) { f["expectedActions"] = Json::array(); }), "Undeclared external action", "空声明没有拦住实际抛出");
        // 实际多抛了一次，而 fixture 只声明了一次。
        mustFail(ejectClosure(motion, true, 2), "Undeclared external action", "多出来的抛出被吞掉了");
        // 序列 / 数量 / 初值。
        mustFail(edited([](Json& f) { f["expectedActions"][0]["tick"] = 5; }), "differs in tick", "游戏刻不符仍然通过");
        mustFail(edited([](Json& f) { f["expectedActions"][0]["index"] = 1; }), "differs in index", "序号不符仍然通过");
        mustFail(edited([](Json& f) { f["expectedActions"][0]["order"] = 1; }), "differs in same-tick order", "同刻顺序不符仍然通过");
        mustFail(edited([](Json& f) { f["expectedActions"][0]["source"] = Json::array({1, 0, 0}); }), "differs in source", "源坐标不符仍然通过");
        mustFail(edited([](Json& f) { f["expectedActions"][0]["item"] = "minecraft:dirt"; }), "differs in item", "物品不符仍然通过");
        mustFail(edited([](Json& f) { f["expectedActions"][0]["count"] = 2; }), "differs in count", "数量不符仍然通过");
        mustFail(edited([](Json& f) { f["expectedActions"][0]["position"][1] = 0.0; }), "differs in position", "初始位置不符仍然通过");
        mustFail(edited([](Json& f) { f["expectedActions"][0]["velocity"][0] = 0.0; }), "differs in velocity", "初始速度不符仍然通过");
        mustFail(edited([](Json& f) { f["expectedActions"][0]["velocityBits"][2] = "bfa310e3c7dabeb2"; }), "differs in velocityBits[2]", "速度末位不符仍然通过");
        mustFail(edited([](Json& f) { f["expectedActions"][0].erase("velocity"); f["expectedActions"][0].erase("velocityBits"); }), "must state its velocity", "没有声明初速度也算通过");
        // 声明了却始终没有发生。
        mustFail(edited([](Json& f) { auto extra = f["expectedActions"][0]; extra["index"] = 1; extra["tick"] = 12; f["expectedActions"].push_back(extra); }),
                 "never happened", "声明了却没发生的抛出被放过");
        // 其他原因的暂停：探针断点与抛出发生在同一刻，确认外部动作不得把它一并清掉。
        mustFail(ejectClosure(motion, false, 1), "paused for a reason other than external actions", "探针断点被外部动作确认顺手清掉了",
                 [&](Simulator& s) { s.configureProbe(s.addProbe(motion.at("source").get<BlockPos>(), "eject", "analog"), Json{{"trigger", "falling"}}); });
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
    for (const auto* fixtureName : {"java26_2Redstone", "java26_2Devices", "java26_2Containers", "java26_2Hoppers", "java26_2Torches", "java26_2Rails", "java26_2TickBatches", "java26_2Tripwire", "java26_2Buttons", "java26_2Droppers", "java26_2Targets", "java26_2CopperChests", "java26_2Bookshelves", "java26_2Pots", "java26_2Vibrations", "java26_2DeviceVibrations", "java26_2Notes", "java26_2Bells", "java26_2Jukeboxes", "java26_2JukeboxHoppers", "java26_2Composters", "java26_2WireGeometry", "java26_2PistonPushability", "java26_2DaylightVibration", "java26_2StairShapes", "java26_2RailNotificationSource", "java26_2PistonRemovalCallback", "java26_2DiodeSupportBreak", "java26_2ObserverRemoval", "java26_2PistonLandingShape", "java26_2WireShapeToggle", "java26_2RemovalNotifySource", "java26_2RepeaterLockRefresh", "java26_2ItemFrameComparator", "java26_2UpdateTrace", "java26_2MachineMatrix", "java26_2SupportDirection", "java26_2FenceGateInWall", "java26_2UpdateSource", "java26_2HopperPickup", "java26_2ChunkLifecycle", "java26_2ChunkLifecycleNegative", "java26_2ScheduledQueue", "java26_2RandomState", "java26_2BlockEntityOrder", "java26_2EntityContainers", "java26_2BlockTicking", "java26_2SensorEdgeChunks", "java26_2SensorEdgeDiagonal", "java26_2SensorEdgeNegative"}) test(std::string("Java 26.2 differential: ") + fixtureName, [&] {
        std::ifstream file(std::string(SIMULATOR_DATA_DIR) + "/../tests/fixtures/" + fixtureName + ".json"); expect(static_cast<bool>(file), "missing vanilla reference fixture");
        const auto fixture = Json::parse(file); Simulator s(r);
        // 差分重放循环与 checkReference 工具共用 referenceReplay.hpp 里的同一份实现，
        // 免得某个字段只在其中一条入口里被比较。
        const auto outcome = replayReferenceFixture(s, fixture);
        expect(outcome.at("status") == "match", "vanilla differential: " + outcome.dump());
    });
    test("all original compost probabilities and random insertion samples", [&] {
        std::ifstream file(std::string(SIMULATOR_DATA_DIR)+"/../tests/fixtures/java26_2Composting.json");auto fixture=Json::parse(file);
        for(const auto& row:fixture.at("cases")) {
            Simulator s(r);const BlockPos pos{0,0,0};const int before=row.at("before");const std::string item=row.at("item");
            s.setBlock(pos,r.with(r.state("composter"),"level",before));s.setRandomSeed(std::stoull(row.at("seed").get<std::string>()));
            s.stimulate(pos,{{"compostItem",item}});
            expect(s.analogOutput(pos)==row.at("after").get<int>(),"compost result differs: "+row.dump());
            expect(s.saveProject("random",true)["randomSource"]["state"]==row.at("randomState"),"compost random consumption differs: "+row.dump());
            expect((r.item(r.itemId(item)).compostChance>=0 && before<7)==(row.at("count")==0),"compost input filter differs");
        }
    });
    test("composter maturation checkpoints, idle sleep and failed extraction", [&] {
        Simulator s(r);const BlockPos pos{0,1,0};s.place(pos,r.state("composter"));
        for(int i=0;i<7;++i)s.stimulate(pos,{{"compostItem","pumpkin_pie"}});
        expect(s.analogOutput(pos)==7 && s.pendingEvents()==1,"composter did not schedule one maturation");
        s.advanceTo(19);auto checkpoint=s.saveProject("compost",true);Simulator copy(r);copy.loadProject(checkpoint);
        s.advanceTo(20);copy.advanceTo(20);expect(s.saveProject("compost",true)==copy.saveProject("compost",true) && s.analogOutput(pos)==8,"maturation checkpoint diverged");
        auto count=s.statistics.scheduledEvents;s.advanceTo(1000000);expect(s.statistics.scheduledEvents==count,"mature composter kept ticking");
        s.place({0,0,0},r.state("hopper",{{"facing","east"}}));
        Json inventory=Json::array();for(int i=0;i<5;++i)inventory.push_back({{"slot",i},{"item","stone"},{"count",63}});
        s.stimulate({0,0,0},{{"inventory",inventory}});s.advanceTo(1000001);
        expect(s.analogOutput(pos)==0 && s.inventoryJson({0,0,0}).size()==5,"failed output extraction did not preserve vanilla loss");
        const auto before=s.saveProject("compost",true);bool threw=false;try{s.stimulate(pos,{{"compostItem","unknown_item"}});}catch(...){threw=true;}
        expect(threw && s.saveProject("compost",true)==before,"invalid compost input was not atomic");
    });
    test("all original jukebox songs match full playback, end padding and random consumption", [&] {
        std::ifstream file(std::string(SIMULATOR_DATA_DIR)+"/../tests/fixtures/java26_2JukeboxPlayback.json");auto fixture=Json::parse(file);
        for(const auto& row:fixture.at("cases")) {
            Simulator s(r);s.place({0,0,0},r.state("jukebox"));s.stimulate({0,0,0},{{"inventory",Json::array({{{"slot",0},{"item",row.at("item")},{"count",1}}})}});s.setRandomSeed(17);
            const int song=r.item(r.itemId(row.at("item"))).jukeboxSong;
            expect(r.song(song).lengthTicks==row.at("lengthTicks") && r.song(song).comparatorOutput==row.at("analog"),"song metadata differs");
            for(const auto& sample:row.at("samples")) {
                s.advanceTo(sample.at("ticks"));const auto info=s.inspect({0,0,0});
                expect(s.jukeboxPlaying({0,0,0})==sample.at("playing").get<bool>() && info["jukebox"]["elapsed"]==sample.at("elapsed"),"song playback differs: "+row.at("item").get<std::string>()+" "+sample.dump());
                expect(s.analogOutput({0,0,0})==row.at("analog") && s.signal({0,0,0},Direction::east)==(sample.at("playing").get<bool>()?15:0),"song power/analog differs");
                expect(s.saveProject("song",true)["randomSource"]["state"]==sample.at("randomState"),"song particle RNG differs");
            }
            expect(s.inventoryJson({0,0,0}).size()==1 && s.pendingEvents()==0,"finished song lost disc or kept ticking");
        }
    });
    test("jukebox checkpoint and ticker gate preserve playback and idle sentinels", [&] {
        Simulator s(r);const BlockPos pos{0,0,0};s.place(pos,r.state("jukebox"));s.stimulate(pos,{{"inventory",Json::array({{{"slot",0},{"item","music_disc_bounce"},{"count",1}}})}});s.advanceTo(7);
        auto before=s.saveProject("jukebox",true);Simulator restored(r);restored.loadProject(before);s.advanceTo(33);restored.advanceTo(33);expect(s.saveProject("jukebox",true)==restored.saveProject("jukebox",true),"playing checkpoint changed");
        s.setBlock(pos,r.withBool(s.world.get(pos),"has_record",false));const auto elapsed=s.inspect(pos)["jukebox"]["elapsed"];s.advanceTo(100);
        expect(s.jukeboxPlaying(pos) && s.signal(pos,Direction::up)==15 && s.inspect(pos)["jukebox"]["elapsed"]==elapsed && s.pendingEvents()==0,"ticker gate stopped or advanced song");
        s.place({100,0,0},r.state("sculk_sensor"));
        before=s.saveProject("jukebox",true);auto legacy=before;legacy["jukeboxes"][0]["wakeAt"]=UINT64_MAX;legacy["sensors"][0]["wakeAt"]=UINT64_MAX;
        restored.loadProject(Json::parse(legacy.dump()));expect(restored.saveProject("jukebox",true)==before,"exact old idle sentinel did not round trip");
        s.setBlock(pos,r.withBool(s.world.get(pos),"has_record",true));restored.setBlock(pos,r.withBool(restored.world.get(pos),"has_record",true));
        s.advanceTo(123);restored.advanceTo(123);expect(s.saveProject("jukebox",true)==restored.saveProject("jukebox",true),"gated checkpoint changed");
        auto invalid=s.saveProject("jukebox",true);invalid["jukeboxes"][0]["song"]="minecraft:11";before=s.saveProject("jukebox",true);bool threw=false;try{s.loadProject(invalid);}catch(...){threw=true;}expect(threw && before==s.saveProject("jukebox",true),"wrong song snapshot was not atomic");
        threw=false;try{s.stimulate(pos,{{"inventory",Json::array({{{"slot",0},{"item","stone"},{"count",1}}})}});}catch(...){threw=true;}expect(threw && before==s.saveProject("jukebox",true),"non-disc inventory changed state");
        Simulator design(r);design.loadProject(s.saveProject("design",false));expect(design.inspect(pos)["jukebox"]["elapsed"]==0 && design.jukeboxPlaying(pos),"design did not restart its disc");
        s.stimulate(pos,{{"inventory",Json::array({{{"slot",0},{"count",0}}})}});const auto events=s.statistics.scheduledEvents;s.advanceTo(1000000);expect(!s.jukeboxPlaying(pos) && s.statistics.scheduledEvents==events,"empty jukebox kept ticking");
    });
    test("jukebox disc filtering covers dropper directions and nonempty hopper slots", [&] {
        for(auto direction:directions) {
            Simulator s(r);BlockPos source{0,0,0},target=source.relative(direction);s.place(source,r.state("dropper",{{"facing",directionNames[static_cast<unsigned>(direction)]}}));s.place(target,r.state("jukebox"));
            s.stimulate(source,{{"inventory",Json::array({{{"slot",0},{"item","music_disc_13"},{"count",1}}})}});s.schedule(source,1);s.advanceTo(1);
            expect(s.inventoryJson(target).size()==1 && s.jukeboxPlaying(target),"dropper failed disc insertion");
            s.stimulate(source,{{"inventory",Json::array({{{"slot",0},{"item","music_disc_cat"},{"count",1}}})}});s.schedule(source,1);s.advanceTo(2);
            expect(s.inventoryJson(source).size()==1 && s.inventoryJson(target)[0]["item"]=="minecraft:music_disc_13","dropper overwrote existing disc");
        }
        Simulator s(r);s.place({0,1,0},r.state("jukebox"));s.place({0,0,0},r.state("hopper",{{"facing","east"}}));
        s.stimulate({0,0,0},{{"inventory",Json::array({{{"slot",0},{"item","stone"},{"count",63}},{{"slot",1},{"item","stone"},{"count",63}},{{"slot",2},{"item","stone"},{"count",63}},{{"slot",3},{"item","stone"},{"count",63}},{{"slot",4},{"item","stone"},{"count",63}}})}});
        // A solid block prevents output. The recipient has spare capacity but no empty slot.
        s.place({1,0,0},r.state("stone"));s.stimulate({0,1,0},{{"inventory",Json::array({{{"slot",0},{"item","music_disc_11"},{"count",1}}})}});s.advanceTo(30);
        expect(s.inventoryJson({0,1,0}).size()==1 && s.inspect({0,1,0})["jukebox"]["elapsed"]==30,"failed extraction restarted playback");
        s.stimulate({0,0,0},{{"inventory",Json::array({{{"slot",4},{"count",0}}})}});s.advanceTo(31);
        expect(s.inventoryJson({0,1,0}).size()==1 && r.property(s.world.get({0,0,0}),"enabled")=="false","playing jukebox failed to lock hopper");
        s.advanceTo(1441);expect(s.inventoryJson({0,1,0}).size()==1 && !s.jukeboxPlaying({0,1,0}),"song completion reordered its earlier hopper");
        s.advanceTo(1442);expect(s.inventoryJson({0,1,0}).empty(),"song completion did not wake extraction");
    });
    test("bell hit faces and float boundary match original across attachments and world heights", [&] {
        std::ifstream file(std::string(SIMULATOR_DATA_DIR)+"/../tests/fixtures/java26_2BellHits.json");auto fixture=Json::parse(file);
        for(const auto& row:fixture.at("cases")) {
            Simulator s(r);BlockPos pos{0,row.at("y"),0};s.setBlock(pos,r.state("bell",{{"facing",row.at("facing")},{"attachment",row.at("attachment")}}));
            s.stimulate(pos,{{"face",row.at("face")},{"height",row.at("height")}});
            expect((s.inspect(pos)["runtime"].value("ringCount",0)==1)==row.at("ring").get<bool>(),"bell hit differs: "+row.dump());
        }
    });
    test("bell re-ringing, ordered events, idle sleep and checkpoint cancellation", [&] {
        Simulator s(r);s.place({0,-1,0},r.state("stone"));s.place({0,0,0},r.state("bell"));s.interact({0,0,0});
        auto pending=s.saveProject("bell",true);Simulator restored(r);restored.loadProject(pending);s.advanceTo(2);restored.advanceTo(2);
        expect(s.saveProject("bell",true)==restored.saveProject("bell",true),"bell pending checkpoint diverged");
        s.advanceTo(10);s.stimulate({0,0,0},{{"face","south"},{"height",.5}});s.stimulate({0,0,0},{{"face","north"},{"height",.5}});s.advanceTo(11);
        expect(s.inspect({0,0,0})["runtime"]["ringDirection"]=="north","bell events reordered");
        auto moving=s.saveProject("bell",true);restored.loadProject(moving);s.advanceTo(59);restored.advanceTo(59);
        expect(s.inspect({0,0,0})["runtime"]["ringing"]==true && s.saveProject("bell",true)==restored.saveProject("bell",true),"re-ring stop/checkpoint changed");
        s.advanceTo(60);expect(s.inspect({0,0,0})["runtime"]["ringing"]==false,"bell did not stop after 50 ticks");
        const auto events=s.statistics.scheduledEvents;s.advanceTo(1000000);expect(s.statistics.scheduledEvents==events && s.pendingEvents()==0,"idle bell kept ticking");
        expect(s.saveProject("bell",true)["randomSource"]["draws"]=="0","bell consumed world RNG");
        auto broken=moving;broken["events"]=Json::array();bool threw=false;try{s.loadProject(broken);}catch(...){threw=true;}expect(threw,"bell accepted missing completion event");
        s.interact({0,0,0});s.advanceTo(1000001);s.setBlock({0,0,0},0);s.place({0,0,0},r.state("bell"));
        auto replacement=s.saveProject("bell",true);restored.loadProject(replacement);restored.advanceTo(1000060);expect(!restored.inspect({0,0,0})["runtime"].value("ringing",false),"replacement inherited old bell event");
    });
    test("note and bell placement wait for a real neighbor update beside existing power", [&] {
        for(const auto* name:{"note_block","bell"}) {
            Simulator s(r);s.place({0,-1,0},r.state("stone"));s.place({-1,0,0},r.state("redstone_block"));s.place({0,0,0},r.state(name));
            expect(!s.at({0,0,0}).powered && s.pendingEvents()==0,"placement manufactured neighbor notification");
            s.place({0,0,1},r.state("stone"));expect(s.at({0,0,0}).powered && s.pendingEvents()>0,"real neighbor notification did not trigger");
        }
    });
    test("notes preserve event deduplication, execution-time state and checkpoint random draws", [&] {
        Simulator s(r);s.place({0,0,0},r.state("note_block"));s.setRandomSeed(17);
        s.stimulate({0,0,0},{{"playNote",true}});s.interact({0,0,0});s.interact({0,0,0});
        expect(s.pendingEvents()==1,"note block events were not deduplicated");
        auto pending=s.saveProject("notes",true);Simulator restored(r);restored.loadProject(pending);
        bool threw=false;try{(void)s.saveProject("notes",false);}catch(...){threw=true;}expect(threw,"pending note was lost by circuit export");
        // Obstruction after scheduling does not cancel triggerEvent.
        s.place({0,1,0},r.state("glass"));restored.place({0,1,0},r.state("glass"));
        s.advanceTo(1);restored.advanceTo(1);expect(s.saveProject("notes",true)==restored.saveProject("notes",true),"note checkpoint diverged");
        auto info=s.inspect({0,0,0})["runtime"];expect(info["playCount"]==1 && info["lastPlayed"]["note"]==2,"queued event did not read latest note");
        LegacyRandom expected(17);expected.nextLong();expect(s.saveProject("notes",true)["randomSource"]["state"]==expected.state(),"note sound consumed wrong random state");
        s.stimulate({0,0,0},{{"playNote",true}});expect(s.pendingEvents()==0,"blocked note scheduled sound");
        auto bad=s.saveProject("notes",true);for(auto& row:bad["blockData"])if(row["pos"]==Json::array({0,0,0}))row["values"]["lastPlayed"]["pitch"]=100;
        auto before=s.saveProject("notes",true);threw=false;try{s.loadProject(bad);}catch(...){threw=true;}expect(threw && before==s.saveProject("notes",true),"invalid note history not rejected atomically");
        auto design=s.saveProject("design",false);Simulator clean(r);clean.loadProject(design);expect(!clean.inspect({0,0,0})["runtime"].contains("lastPlayed"),"design retained previous run diagnostics");
    });
    test("note block event random stream matches original across all instruments", [&] {
        std::ifstream file(std::string(SIMULATOR_DATA_DIR)+"/../tests/fixtures/java26_2NoteRandom.json");auto fixture=Json::parse(file);
        for(const auto& row:fixture.at("cases")) {
            Simulator s(r);const auto instrument=row.at("instrument").get<std::string>();
            s.setBlock({0,0,0},r.state("note_block",{{"instrument",instrument},{"note","17"}}));
            if(row.at("custom")) {s.place({0,1,0},r.state("player_head"));s.stimulate({0,1,0},{{"customSound","simulator:test/bell"}});}
            s.setRandomSeed(std::stoull(row.at("seedBits").get<std::string>(),nullptr,16));
            s.stimulate({0,0,0},{{"playNote",true}});s.advanceTo(1);
            expect(s.saveProject("note",true)["randomSource"]["state"]==row.at("randomState"),"note RNG differs for "+instrument);
            expect((s.inspect({0,0,0})["runtime"].value("playCount",0)==1)==row.at("played").get<bool>(),"note sound result differs for "+instrument);
        }
        for(const auto* head:{"skeleton_skull","wither_skeleton_skull","zombie_head","creeper_head","dragon_head","piglin_head","player_head"}) {
            Simulator s(r);s.place({0,0,0},r.state("note_block"));s.place({0,1,0},r.state(head));
            expect(r.instrument(r.instrumentId(r.property(s.world.get({0,0,0}),"instrument"))).above,"head instrument missing");
        }
    });
    test("head note overrides, missing custom sound and explicit custom sound", [&] {
        Simulator s(r);s.place({0,-1,0},r.state("gold_block"));s.place({0,0,0},r.state("note_block"));
        expect(r.property(s.world.get({0,0,0}),"instrument")=="bell","placement ignored base instrument");
        s.place({0,1,0},r.state("zombie_head"));s.interact({0,0,0});s.advanceTo(1);
        expect(s.inspect({0,0,0})["runtime"]["lastPlayed"]["instrument"]=="zombie","head did not override instrument");
        s.place({0,1,0},r.state("player_head"));s.setRandomSeed(9);s.stimulate({0,0,0},{{"playNote",true}});s.advanceTo(2);
        expect(s.saveProject("note",true)["randomSource"]["draws"]=="0","head without custom sound consumed random");
        auto before=s.saveProject("note",true);bool threw=false;try{s.stimulate({0,1,0},{{"customSound","bad:UPPER CASE"}});}catch(...){threw=true;}
        expect(threw && before==s.saveProject("note",true),"invalid head edit was not atomic");
        s.stimulate({0,1,0},{{"customSound","simulator:test/bell"}});s.stimulate({0,0,0},{{"playNote",true}});
        auto checkpoint=s.saveProject("note",true);Simulator restored(r);restored.loadProject(checkpoint);restored.advanceTo(3);s.advanceTo(3);
        expect(s.saveProject("note",true)==restored.saveProject("note",true),"custom head checkpoint changed");
        expect(s.inspect({0,0,0})["runtime"]["lastPlayed"]["sound"]=="simulator:test/bell" && s.saveProject("note",true)["randomSource"]["draws"]=="2","custom head play did not use configured sound");
        s.setBlock({0,1,0},0);expect(r.property(s.world.get({0,0,0}),"instrument")=="bell","head removal ignored base");
        s.place({0,-1,0},r.state("skeleton_skull"));expect(r.property(s.world.get({0,0,0}),"instrument")=="harp","head below should choose harp");
    });
    test("vibration travel, block-distance power and pending checkpoints", [&] {
        Simulator s(r);const BlockPos pos{-17,2,-17};s.place(pos,r.state("sculk_sensor"));s.advanceTo(3);
        s.stimulate({-9,2,-17},{{"gameEvent","step"},{"offset",Json::array({.999,.5,.5})}});
        auto pending=s.saveProject("pending",true);Simulator selected(r);selected.loadProject(pending);
        expect(pending==selected.saveProject("pending",true),"candidate snapshot changed");
        s.advanceTo(5);selected.advanceTo(5);auto flying=s.saveProject("flying",true);Simulator restored(r);restored.loadProject(flying);auto clone=s.clone();
        expect(s.saveProject("flying",true)==selected.saveProject("flying",true),"candidate continuation changed");
        bool rejected=false;try{(void)s.saveProject("design",false);}catch(...){rejected=true;}expect(rejected,"travelling vibration exported without queue");
        for(auto* world:{&s,&selected,&restored,clone.get()}) {
            world->advanceTo(10);expect(world->displayValue(pos)==0,"vibration arrived early");
            world->advanceTo(11);expect(world->displayValue(pos)==1 && world->analogOutput(pos)==1,"integer radius power or float travel mismatch");
            world->advanceTo(41);expect(world->displayValue(pos)==0 && r.property(world->world.get(pos),"sculk_sensor_phase")=="cooldown","active interval mismatch");
            world->advanceTo(51);expect(r.property(world->world.get(pos),"sculk_sensor_phase")=="inactive","cooldown mismatch");
            expect(world->saveProject("done",true)["randomSource"]["draws"]=="2","sensor sound pitch random consumption mismatch");
        }
        expect(s.saveProject("done",true)==restored.saveProject("done",true) && s.saveProject("done",true)==clone->saveProject("done",true),"travelling snapshot/clone divergence");
        auto invalid=flying;invalid["sensors"][0]["current"]["distance"]=1;auto before=restored.saveProject("before",true);rejected=false;
        try{restored.loadProject(invalid);}catch(...){rejected=true;}expect(rejected && before==restored.saveProject("before",true),"invalid vibration snapshot was not atomic");
    });
    test("vibration candidates prioritize distance then frequency and keep first tick", [&] {
        Simulator s(r);const BlockPos pos{0,0,0};s.place(pos,r.state("calibrated_sculk_sensor",{{"waterlogged","true"}}));
        s.stimulate({4,0,0},{{"gameEvent","explode"}});s.stimulate({2,0,0},{{"gameEvent","step"}});s.stimulate({-2,0,0},{{"gameEvent","eat"}});
        s.advanceTo(1);s.stimulate({1,0,0},{{"gameEvent","explode"}});s.advanceTo(2);
        expect(s.analogOutput(pos)==8 && s.displayValue(pos)==14,"selection ignored nearest/highest frequency rule");
        s.advanceTo(22);expect(s.saveProject("wet",true)["randomSource"]["draws"]=="0","waterlogged sensor consumed dry sound randomness");
        s.stimulate({0,0,0},{{"gameEvent","block_place"}});s.stimulate({0,0,0},{{"gameEvent","block_destroy"}});
        s.stimulate({1,0,0},{{"gameEvent","step"},{"source",{{"sneaking",true}}}});
        s.stimulate({1,0,0},{{"gameEvent","explode"},{"source",{{"spectator",true}}}});
        s.stimulate({1,0,0},{{"gameEvent","explode"},{"source",{{"dampensVibrations",true}}}});
        s.stimulate({1,0,0},{{"gameEvent","block_change"},{"affectedBlock",{{"name","white_carpet"}}}});
        expect(s.pendingEvents()==0,"excluded vibration scheduled");
        auto before=s.saveProject("before",true);bool threw=false;
        try{s.stimulate({1,0,0},{{"gameEvent","explode"},{"offset",Json::array({.5,2,.5})}});}catch(...){threw=true;}
        expect(threw && before==s.saveProject("before",true),"invalid source position changed world");
        s.stimulate({1,0,0},{{"gameEvent","block_change"},{"source",{{"sneaking",true}}}});s.advanceTo(23);
        expect(s.analogOutput(pos)==11,"sneaking suppressed an allowed event");
    });
    test("idle sensors do no tick work and removal cancels travelling state", [&] {
        Simulator s(r);for(int i=0;i<5000;++i)s.place({i*32,0,0},r.state("sculk_sensor"));
        s.advanceTo(1000000);expect(s.pendingEvents()==0 && s.statistics.scheduledEvents==0,"idle sensor polling");
        s.stimulate({4,0,0},{{"gameEvent","step"}});s.advanceTo(1000001);
        s.place({0,0,0},r.state("stone"));s.place({0,0,0},r.state("sculk_sensor"));s.advanceTo(1000020);
        expect(s.displayValue({0,0,0})==0 && s.pendingEvents()==0,"removed listener retained vibration");
        s.stimulate({4,0,0},{{"gameEvent","eat"}});s.advanceTo(1000024);expect(s.analogOutput({0,0,0})==8,"replacement listener not registered");
        s.clear();s.stimulate({4,0,0},{{"gameEvent","step"}});expect(s.pendingEvents()==0,"clear retained listeners");
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
    test("cached world writes agree with a coordinate map at integer limits and collisions", [&] {
        World world;
        std::map<BlockPos, StateId> expected;
        std::vector<BlockPos> positions;
        for (int n : {INT_MIN, INT_MIN + 15, INT_MIN + 16, -129, -128, -33, -32, -17, -16, -1, 0, 15, 16, 31, 32, 127, 128, INT_MAX - 16, INT_MAX - 15, INT_MAX}) {
            positions.push_back({n, n, n});
            positions.push_back({n, -17, 31});
            positions.push_back({15, n, -16});
            positions.push_back({-1, 16, n});
        }
        // Widely spaced x coordinates deliberately evict the same cache slot.
        for (int n = -16; n < 16; ++n) positions.push_back({n * 128, 0, 0});
        std::sort(positions.begin(), positions.end());
        positions.erase(std::unique(positions.begin(), positions.end()), positions.end());
        auto write = [&](BlockPos pos, StateId value) {
            const auto found = expected.find(pos);
            const auto old = found == expected.end() ? 0 : found->second;
            expect(world.set(pos, value) == old, "cached write returned wrong previous state");
            if (value) expected[pos] = value; else expected.erase(pos);
            expect(world.get(pos) == value && world.size() == expected.size(), "cached write lost state/count");
        };
        auto verify = [&] {
            const auto cells = world.cells();
            expect(cells.size() == expected.size(), "world enumeration count");
            auto item = expected.begin();
            for (const auto& cell : cells) {
                expect(cell.pos == item->first && cell.state == item->second, "world enumeration at integer boundary");
                ++item;
            }
            for (const auto& pos : positions) {
                const auto found = expected.find(pos);
                expect(world.get(pos) == (found == expected.end() ? 0 : found->second), "evicted or negative cached read");
            }
        };
        for (auto pos : positions) { expect(world.get(pos) == 0, "initial miss"); write(pos, 7); }
        verify();
        std::uint32_t random = 71237;
        for (int i = 0; i < 10000; ++i) {
            random = random * 1664525u + 1013904223u;
            auto pos = positions[(random >> 8u) % positions.size()];
            write(pos, i % 3 == 0 ? 0 : random % 31u + 1u);
            if (i % 257 == 0) verify();
        }
        verify();
        for (auto pos : positions) write(pos, 0);
        expect(world.chunkCount() == 0, "empty cached chunks were retained");
        verify();
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
    test("fence gate IN_WALL follows the perpendicular axis only", [&] {
        // 原版 FenceGateBlock.updateShape 只在 FACING.getClockWise() 那条轴上重算 IN_WALL；
        // 放置时按同一条轴两侧是否是墙取值，朝向轴上的形状更新完全不动它。
        Simulator s(r);
        s.place({0, 0, 0}, r.state("oak_fence_gate", {{"facing", "north"}}));
        expect(r.property(s.world.get({0, 0, 0}), "in_wall") == "false", "bare gate should not be in a wall");
        s.setBlock({1, 0, 0}, r.state("cobblestone_wall"));
        expect(r.property(s.world.get({0, 0, 0}), "in_wall") == "true", "perpendicular wall did not set IN_WALL");
        s.setBlock({0, 0, -1}, r.state("white_wool"));
        expect(r.property(s.world.get({0, 0, 0}), "in_wall") == "true", "facing-axis update must not clear IN_WALL");
        s.setBlock({1, 0, 0}, 0);
        expect(r.property(s.world.get({0, 0, 0}), "in_wall") == "false", "removing the wall did not clear IN_WALL");
        s.place({0, 0, 4}, r.state("oak_fence_gate", {{"facing", "north"}}));
        s.setBlock({0, 0, 5}, r.state("cobblestone_wall"));
        expect(r.property(s.world.get({0, 0, 4}), "in_wall") == "false", "wall on the facing axis must be ignored");
        s.world.set({1, 0, 8}, r.state("cobblestone_wall"));
        s.place({0, 0, 8}, r.state("oak_fence_gate", {{"facing", "north"}}));
        expect(r.property(s.world.get({0, 0, 8}), "in_wall") == "true", "placement did not read the perpendicular walls");
    });
    test("26.2 redstone capability coverage gate", [&] {
        // Every block whose 26.2 class actually overrides a redstone-relevant callback, or that
        // emits a signal or analog output, must be either explicitly supported here or refused.
        std::ifstream file(std::string(SIMULATOR_DATA_DIR) + "/../tests/fixtures/java26_2BlockCapabilities.json");
        expect(static_cast<bool>(file), "missing vanilla block capability fixture");
        auto fixture = Json::parse(file);
        std::set<std::string> relevant;
        for (const auto& row : fixture.at("blocks")) relevant.insert(row.at("name").get<std::string>());
        expect(relevant.size() == fixture.at("blocks").size(), "duplicate capability rows");
        std::map<std::string, int> placeable;
        std::vector<std::string> refused;
        for (std::size_t index = 0; index < r.typeCount(); ++index) {
            const auto& type = r.blockType(static_cast<std::uint16_t>(index));
            expect(type.supportLevel == "implemented" || type.supportLevel == "partial"
                || type.supportLevel == "externalStimulus" || type.supportLevel == "unimplemented",
                "unknown support level " + type.supportLevel + " for " + type.name);
            if (type.device == Device::dispenser || type.device == Device::crafter || type.device == Device::furnace)
                expect(type.supportLevel == "unimplemented", "placeholder device became placeable: " + type.name);
            if (!relevant.contains(type.name)) continue;
            if (type.supportLevel == "unimplemented") refused.push_back(type.name);
            else ++placeable[type.className + "=" + type.supportLevel];
        }
        std::vector<std::string> actual;
        for (const auto& [key, count] : placeable) actual.push_back(key + "x" + std::to_string(count));
        const std::vector<std::string> expected{
            "BarrelBlock=implementedx1", "BeehiveBlock=externalStimulusx2", "BellBlock=partialx1", "ButtonBlock=implementedx14",
            "CalibratedSculkSensorBlock=partialx1", "CauldronBlock=externalStimulusx1", "ChestBlock=implementedx1", "ChiseledBookShelfBlock=partialx1",
            "ComparatorBlock=implementedx1", "ComposterBlock=partialx1", "CopperBulbBlock=implementedx4", "CopperChestBlock=implementedx4",
            "CopperGolemStatueBlock=externalStimulusx4", "DaylightDetectorBlock=externalStimulusx1", "DecoratedPotBlock=partialx1", "DetectorRailBlock=externalStimulusx1",
            "DoorBlock=implementedx17", "DropperBlock=partialx1", "EndPortalFrameBlock=externalStimulusx1", "FenceGateBlock=implementedx12",
            "HopperBlock=implementedx1", "JukeboxBlock=partialx1", "LavaCauldronBlock=externalStimulusx1", "LayeredCauldronBlock=externalStimulusx2",
            "LecternBlock=externalStimulusx1", "LeverBlock=implementedx1", "LightningRodBlock=externalStimulusx4", "MovingPistonBlock=implementedx1",
            "NoteBlock=partialx1", "ObserverBlock=implementedx1", "PiglinWallSkullBlock=partialx1", "PistonBaseBlock=implementedx2",
            "PistonHeadBlock=implementedx1", "PlayerHeadBlock=partialx1", "PlayerWallHeadBlock=partialx1", "PoweredBlock=implementedx1",
            "PoweredRailBlock=implementedx2", "PressurePlateBlock=externalStimulusx14", "RailBlock=implementedx1", "RedStoneWireBlock=implementedx1",
            "RedstoneLampBlock=implementedx1", "RedstoneTorchBlock=implementedx1", "RedstoneWallTorchBlock=implementedx1", "RepeaterBlock=implementedx1",
            "RespawnAnchorBlock=externalStimulusx1", "SculkSensorBlock=partialx1", "SkullBlock=partialx5", "TargetBlock=externalStimulusx1",
            "TrapDoorBlock=implementedx17", "TrappedChestBlock=implementedx1", "TripWireBlock=externalStimulusx1", "TripWireHookBlock=implementedx1",
            "WallSkullBlock=partialx4", "WeatheringCopperBulbBlock=implementedx4", "WeatheringCopperChestBlock=implementedx4", "WeatheringCopperDoorBlock=implementedx4",
            "WeatheringCopperGolemStatueBlock=externalStimulusx4", "WeatheringCopperTrapDoorBlock=implementedx4", "WeatheringLightningRodBlock=externalStimulusx4", "WeightedPressurePlateBlock=externalStimulusx2",
            "WitherSkullBlock=partialx1", "WitherWallSkullBlock=partialx1"};
        expect(actual == expected, "redstone-relevant palette changed; update the inventory and add per-device evidence. actual="
            + Json(actual).dump());
        expect(refused.size() == 243, "refused redstone block count changed: " + std::to_string(refused.size()));
        Simulator s(r); floor(s);
        for (const auto& name : refused) {
            bool threw = false;
            try { s.place({0, 1, 0}, r.state(name)); } catch (...) { threw = true; }
            expect(threw && s.world.get({0, 1, 0}) == 0, "unimplemented block was placed: " + name);
        }
    });
    test("item frame comparator input validation and checkpoint", [&] {
        Simulator s(r); floor(s);
        BlockPos comparator{0, 1, 0}, mount{1, 1, 0};
        s.place(comparator, r.state("comparator", {{"facing", "east"}}));
        s.place(mount, r.state("stone"));
        s.stimulate(mount, {{"itemFrames", Json::array({{{"facing", "east"}, {"rotation", 5}, {"hasItem", true}}})}});
        s.advanceTo(2);
        expect(s.analogOutput(comparator) == 6, "item frame rotation reading");
        auto saved = s.saveProject("frames", true); Simulator restored(r); restored.loadProject(saved);
        expect(restored.analogOutput(comparator) == 6, "item frame lost across checkpoint");
        s.stimulate(mount, {{"itemFrames", Json::array()}});
        s.advanceTo(4);
        expect(s.analogOutput(comparator) == 0, "item frame removal not applied");
        auto rejects = [&](const Json& input) {
            bool threw = false; try { s.stimulate(mount, input); } catch (...) { threw = true; }
            return threw;
        };
        expect(rejects({{"itemFrames", Json::array({{{"facing", "east"}, {"rotation", 8}, {"hasItem", true}}})}}), "rotation 8 accepted");
        expect(rejects({{"itemFrames", Json::array({{{"facing", "east"}, {"rotation", 0}, {"hasItem", true}, {"item", "stone"}}})}}), "unknown frame field accepted");
        expect(rejects({{"itemFrames", Json::array()}, {"viewers", 1}}), "mixed stimulus accepted");
        bool threw = false; try { s.stimulate({5, 1, 0}, {{"itemFrames", Json::array({{{"facing", "east"}, {"rotation", 0}, {"hasItem", false}}})}}); } catch (...) { threw = true; }
        expect(threw, "item frame accepted on an empty cell");
    });
    test("chunk lifecycle: stalled ticks, deferred block events and frozen block entities", [&] {
        Simulator s(r);
        // 一格里放中继器链、活塞和漏斗；把它所在的区块从 entityTicking 降到 loaded，
        // 三类待办都应当停住，恢复后继续。
        for (int x = 0; x < 8; ++x) for (int z = 0; z < 8; ++z) s.world.set({x, -1, z}, r.state("stone"));
        s.place({1, 0, 1}, r.state("repeater", {{"facing", "west"}, {"delay", "4"}}));
        s.place({2, 0, 1}, r.state("redstone_lamp"));
        s.place({1, 0, 4}, r.state("piston", {{"facing", "east"}}));
        s.place({2, 0, 4}, r.state("stone"));
        s.place({1, 0, 6}, r.state("hopper", {{"facing", "east"}}));
        s.place({2, 0, 6}, r.state("chest"));
        s.stimulate({1, 0, 6}, {{"inventory", Json::array({{{"slot", 0}, {"item", "minecraft:stone"}, {"count", 5}}})}});
        s.advanceTo(2);
        expect(s.inventoryJson({2, 0, 6}, false).size() == 1, "hopper did not start transferring before the stall");
        // 先停摆，再在停摆的区块里加电源：邻居更新照常立即发生，但由此排下的计划刻与
        // 方块事件都必须等到区块恢复。
        s.setChunkState(0, 0, Simulator::ChunkState::loaded);
        s.place({0, 0, 1}, r.state("redstone_block"));
        s.place({0, 0, 4}, r.state("redstone_block"));
        const auto stalledLamp = s.world.get({2, 0, 1});
        const auto stalledPiston = s.world.get({1, 0, 4});
        s.advanceTo(60);
        expect(s.world.get({2, 0, 1}) == stalledLamp && s.world.get({1, 0, 4}) == stalledPiston,
               "stalled chunk kept running scheduled ticks or block events");
        expect(s.inventoryJson({2, 0, 6}, false).size() == 1, "stalled chunk kept ticking the hopper");
        // 停摆期间保存/读取，冻结的倒计时必须原样恢复。
        auto saved = s.saveProject("chunks", true); Simulator restored(r); restored.loadProject(saved);
        expect(restored.chunkState({0, 0, 0}) == Simulator::ChunkState::loaded, "chunk state lost across checkpoint");
        for (Simulator* world : {&s, &restored}) {
            world->setChunkState(0, 0, Simulator::ChunkState::entityTicking);
            world->advanceTo(80);
            expect(world->world.get({1, 0, 4}) != stalledPiston, "piston block event was dropped instead of deferred");
            expect(world->world.get({2, 0, 1}) != stalledLamp, "overdue scheduled ticks were dropped");
        }
        expect(s.inventoryJson({2, 0, 6}, false) == restored.inventoryJson({2, 0, 6}, false),
               "hopper cooldown differs after restoring a stalled checkpoint");
    });
    test("唱片机、感测体与钟在停摆区块里也能存读，且钟的摆动计时同样冻结", [&] {
        // 停摆时 `stepEvent` 把最后一批事件放回**当前刻**并停下，随后空闲推进把 currentTick
        // 直接跳到目标刻，于是这些方块实体的 wakeAt 落在 currentTick 之前。漏斗的快照校验
        // 为此开了口子，唱片机与感测体没有；钟则连 wakeAt 同步都没有。
        // 另外 `bellWakeAt` 是「摆动还剩多久」的倒计时，停摆期间必须和漏斗冷却一样冻结。
        Simulator s(r);
        for (int x = 0; x < 8; ++x) for (int z = 0; z < 8; ++z) s.world.set({x, -1, z}, r.state("stone"));
        s.place({1, 0, 1}, r.state("jukebox"));
        s.stimulate({1, 0, 1}, {{"inventory", Json::array({{{"slot", 0}, {"item", "minecraft:music_disc_cat"}, {"count", 1}}})}});
        s.place({1, 0, 4}, r.state("sculk_sensor"));
        s.place({1, 0, 6}, r.state("bell"));
        s.interact({1, 0, 6});
        s.advanceTo(2);
        s.stimulate({4, 0, 4}, {{"gameEvent", "minecraft:block_place"}});
        s.advanceTo(3);
        expect(s.inspect({1, 0, 6})["runtime"]["ringing"] == true, "bell was not ringing before the stall");
        const auto elapsedBefore = s.inspect({1, 0, 1}).at("jukebox").at("elapsed");
        s.setChunkState(0, 0, Simulator::ChunkState::loaded);
        s.advanceTo(300);
        expect(s.inspect({1, 0, 1}).at("jukebox").at("elapsed") == elapsedBefore, "jukebox kept counting while stalled");
        expect(s.inspect({1, 0, 6})["runtime"]["ringing"] == true, "bell shake timer kept running while stalled");
        // 停摆中存读：三种方块实体的快照都必须能原样加载。
        auto saved = s.saveProject("stalledEntities", true);
        Simulator restored(r); restored.loadProject(saved);
        expect(restored.saveProject("stalledEntities", true) == saved, "stalled block entity checkpoint diverged");
        // 恢复后钟还应当摆完剩下的刻数，而不是一恢复就停。
        for (Simulator* world : {&s, &restored}) {
            world->setChunkState(0, 0, Simulator::ChunkState::entityTicking);
            world->advanceTo(301);
            expect(world->inspect({1, 0, 6})["runtime"]["ringing"] == true, "bell stopped immediately after the chunk resumed");
            world->advanceTo(360);
            expect(world->inspect({1, 0, 6})["runtime"]["ringing"] == false, "bell never stopped after resuming");
        }
        // 未加载区块拒绝写入；可 ticking 区块周围八格不能是未加载区块，
        // 因此要先把一圈降级成 loaded 才能卸载中心，正如原版票据等级形成的加载环。
        for (int dx = -1; dx <= 1; ++dx) for (int dz = -1; dz <= 1; ++dz) s.setChunkState(5 + dx, 5 + dz, Simulator::ChunkState::loaded);
        s.setChunkState(5, 5, Simulator::ChunkState::unloaded);
        bool threw = false; try { s.setBlock({80, 0, 80}, r.state("stone")); } catch (...) { threw = true; }
        expect(threw && s.world.get({80, 0, 80}) == 0, "wrote into an unloaded chunk");
        threw = false; try { s.setChunkState(6, 5, Simulator::ChunkState::entityTicking); } catch (...) { threw = true; }
        expect(threw, "a ticking chunk was allowed next to an unloaded one");
    });
    test("block ticking runs block entities and restores only the frozen interval", [&] {
        // Source-derived: LevelChunk.isTicking checks BLOCK_TICKING, not ENTITY_TICKING.
        // Compare against a continuously ticking control to catch both skipped ticks and
        // accidental extra cooldown shifts on blockTicking -> entityTicking transitions.
        Simulator s(r), control(r);
        const BlockPos hopper{2, 0, 2}, chest{3, 0, 2};
        for (auto* world : {&s, &control}) {
            world->place(hopper, r.state("hopper", {{"facing", "east"}}));
            world->place(chest, r.state("chest"));
            world->stimulate(hopper, {{"inventory", Json::array({{{"slot", 0}, {"item", "minecraft:stone"}, {"count", 30}}})}});
        }
        s.setChunkState(0, 0, Simulator::ChunkState::blockTicking);
        expect(s.runnable(), "block entity in blockTicking chunk considered unrunnable");
        s.advanceTo(10); control.advanceTo(10);
        expect(s.inventoryJson(chest, false) == control.inventoryJson(chest, false), "blockTicking skipped hopper ticks");
        s.setChunkState(0, 0, Simulator::ChunkState::loaded);
        s.advanceTo(30);
        const auto saved = s.saveProject("block-only", true);
        Simulator restored(r); restored.loadProject(saved);
        for (auto* world : {&s, &restored}) world->setChunkState(0, 0, Simulator::ChunkState::blockTicking);
        for (Tick tick = 31; tick <= 50; ++tick) {
            if (tick == 36) for (auto* world : {&s, &restored}) world->setChunkState(0, 0, Simulator::ChunkState::entityTicking);
            control.advanceTo(tick - 20);
            for (auto* world : {&s, &restored}) {
                world->advanceTo(tick);
                expect(world->inventoryJson(chest, false) == control.inventoryJson(chest, false), "restored cooldown differs from exactly 20 frozen ticks");
            }
        }
        // Entity contact must still stay gated. A solid block above disables BE pickup
        // but does not disable HopperBlock.entityInside for an item in the hopper itself.
        Simulator contact(r);
        contact.place(hopper, r.state("hopper"));
        contact.place(hopper.relative(Direction::up), r.state("stone"));
        contact.setChunkState(0, 0, Simulator::ChunkState::blockTicking);
        contact.stimulate(hopper, {{"groundItems", Json::array({{{"item", "minecraft:dirt"}, {"count", 1}, {"y", 0.7}}})}});
        contact.advanceTo(5);
        expect(contact.inventoryJson(hopper, false).empty(), "entity contact ran in blockTicking chunk: " + contact.inventoryJson(hopper, false).dump() + " above=" + std::to_string(contact.world.get(hopper.relative(Direction::up))) + " full=" + std::to_string(r[contact.world.get(hopper.relative(Direction::up))].fullCube));
        contact.setChunkState(0, 0, Simulator::ChunkState::entityTicking);
        contact.advanceTo(6);
        expect(!contact.inventoryJson(hopper, false).empty(), "entity contact did not resume");
    });
    test("vibration delivery needs the 3x3 chunks around the listener to be block ticking", [&] {
        // 原版 VibrationSystem.Ticker.receiveVibration 第一句就是
        // requiresAdjacentChunksToBeTicking && !areAdjacentChunksTicking -> return false，
        // 而 SculkSensorBlockEntity.VibrationUser 返回 true（校准感测体继承它）。
        // 返回 false 时既不投递也不清 currentVibration，于是每刻重试而不是丢弃。
        const BlockPos pos{8, 0, 8};      // 区块 (0,0)
        const BlockPos source{2, 0, 8};   // 同区块，距离 6 → 传播 6 刻
        auto build = [&](Simulator& world, const char* block) {
            world.place(pos, r.state(block));
            world.advanceTo(1);
            world.stimulate(source, {{"gameEvent", "step"}});
        };
        // 一、缺省（不声明任何区块状态）行为必须完全不变：第 1+6=7 刻照常投递。
        Simulator base(r); build(base, "sculk_sensor");
        base.advanceTo(6); expect(base.displayValue(pos) == 0, "default vibration arrived early");
        base.advanceTo(7); expect(base.displayValue(pos) == 4, "default vibration delivery changed");
        // 二、监听者在 blockTicking 边缘区块，相邻区块只有 loaded。
        // entityTicking 区块的八邻居至少 blockTicking，不能用那个不可达构造作原版证据。
        // 感测体一旦被激活就会先 active 再 cooldown 最后回到 inactive，所以要在这三段之内
        // 直接看 sculk_sensor_phase，光看功率会漏掉「已经响过又冷却完了」。
        Simulator s(r); build(s, "sculk_sensor");
        s.setChunkState(0, 0, Simulator::ChunkState::blockTicking);
        s.setChunkState(1, 0, Simulator::ChunkState::loaded);
        auto idle = [&](const Simulator& world) {
            return world.displayValue(pos) == 0 && r.property(world.world.get(pos), "sculk_sensor_phase") == "inactive";
        };
        s.advanceTo(8); expect(idle(s), "vibration was delivered while an adjacent chunk was not block ticking");
        s.advanceTo(40); expect(idle(s), "blocked vibration was delivered later while the chunk was still stalled");
        expect(s.pendingEvents() == 1, "blocked vibration stopped retrying instead of waking every tick");
        // 三、停摆中保存/读取：未投递的振动（current 仍在、remaining 已减到 0）原样恢复。
        auto saved = s.saveProject("stuck", true);
        expect(saved["sensors"].size() == 1 && saved["sensors"][0].contains("current") && saved["sensors"][0]["remaining"] == 0,
               "blocked vibration was not kept as a travelling-but-undelivered state");
        Simulator restored(r); restored.loadProject(saved);
        expect(restored.saveProject("stuck", true) == saved, "blocked vibration checkpoint changed across a reload");
        for (Simulator* world : {&s, &restored}) {
            world->setChunkState(1, 0, Simulator::ChunkState::blockTicking);
            world->advanceTo(41);
            expect(world->displayValue(pos) == 4, "vibration was dropped instead of retried once the adjacent chunk resumed");
        }
        // 四、校准幽匿感测体走同一条路径（半径 16，同样距离 6 → 功率 10）。
        Simulator c(r); build(c, "calibrated_sculk_sensor");
        c.setChunkState(0, 0, Simulator::ChunkState::blockTicking);
        c.setChunkState(0, -1, Simulator::ChunkState::loaded);  // 换一个方向的相邻区块
        c.advanceTo(8); expect(idle(c), "calibrated sensor ignored the adjacent chunk requirement");
        c.advanceTo(40); expect(idle(c), "calibrated sensor delivered later while the chunk was still stalled");
        c.setChunkState(0, -1, Simulator::ChunkState::blockTicking);
        c.advanceTo(41);
        expect(c.displayValue(pos) == 10, "calibrated sensor never delivered after the adjacent chunk resumed");
    });
    test("hopper ground item input validation, range and checkpoint", [&] {
        Simulator s(r); floor(s);
        const BlockPos hopper{0, 1, 0};
        s.place(hopper, r.state("hopper", {{"facing", "down"}}));
        // 在吸取体积里、但不与漏斗自己那一格重叠：只走方块实体阶段的 suckInItems。
        s.stimulate(hopper, {{"groundItems", Json::array({{{"item", "minecraft:stone"}, {"count", 3}, {"y", 1.2}}})}});
        expect(s.suckableItems(hopper).size() == 1, "declared item is not in the suck volume");
        s.advanceTo(4);
        expect(s.inventoryJson(hopper, false).size() == 1 && s.suckableItems(hopper).empty(), "hopper did not take the dropped stack");
        auto saved = s.saveProject("drops", true); Simulator restored(r); restored.loadProject(saved);
        expect(restored.inventoryJson(hopper, false) == s.inventoryJson(hopper, false), "hopper inventory lost across checkpoint");
        // 声明集合整体替换，坐标超出体积的条目不参与吸取但仍然保留在输入里。
        s.stimulate(hopper, {{"groundItems", Json::array({{{"item", "minecraft:dirt"}, {"count", 1}, {"y", 2.5}}})}});
        expect(s.suckableItems(hopper).empty(), "an item above the suck volume was reported as suckable");
        auto savedOut = s.saveProject("drops", true); Simulator keptOut(r); keptOut.loadProject(savedOut);
        expect(keptOut.suckableItems(hopper).empty(), "out-of-range declaration changed across checkpoint");
        auto rejects = [&](BlockPos pos, const Json& input) {
            bool threw = false; try { s.stimulate(pos, input); } catch (...) { threw = true; }
            return threw;
        };
        expect(rejects(hopper, {{"groundItems", Json::array({{{"item", "minecraft:stone"}, {"count", 0}}})}}), "zero count accepted");
        expect(rejects(hopper, {{"groundItems", Json::array({{{"item", "minecraft:stone"}, {"count", 65}}})}}), "over-stack count accepted");
        expect(rejects(hopper, {{"groundItems", Json::array({{{"item", "minecraft:stone"}, {"count", 1}, {"w", 1}}})}}), "unknown drop field accepted");
        expect(rejects(hopper, {{"groundItems", Json::array({{{"item", "minecraft:stone"}, {"count", 1}, {"y", 9.0}}})}}), "far away coordinate accepted");
        expect(rejects(hopper, {{"groundItems", Json::array()}, {"viewers", 1}}), "mixed drop stimulus accepted");
        s.place({3, 1, 0}, r.state("chest"));
        expect(rejects({3, 1, 0}, {{"groundItems", Json::array({{{"item", "minecraft:stone"}, {"count", 1}}})}}), "drops accepted on a chest");
    });
    test("container entity declaration, replacement and snapshot round-trip", [&] {
        Simulator s(r); floor(s);
        const BlockPos cart{0, 2, 0};
        auto declare = [&](const Json& entities) { s.stimulate(cart, {{"containerEntities", entities}}); };
        auto stone = [](int slot, int count) { return Json{{"slot", slot}, {"item", "minecraft:stone"}, {"count", count}}; };
        // 空气格上的声明：矿车通常停在空气或铁轨那一格里，不需要方块承载。
        declare(Json::array({{{"type", "chest_minecart"}, {"inventory", Json::array({stone(0, 5), stone(26, 1)})}},
                             {{"type", "hopper_minecart"}, {"inventory", Json::array({stone(4, 2)})}}}));
        expect(s.world.get(cart) == 0, "declaring container entities placed a block");
        expect(s.containerEntitiesJson(cart).size() == 2, "declared container entities were dropped");
        expect(s.containerEntitiesJson(cart).at(0).at("inventory").size() == 2, "minecart inventory was not stored");
        // 整体替换，不是合并。
        declare(Json::array({{{"type", "hopper_minecart"}, {"inventory", Json::array()}}}));
        expect(s.containerEntitiesJson(cart).size() == 1 && s.containerEntitiesJson(cart).at(0).at("type") == "hopper_minecart",
               "container entity declaration was merged instead of replaced");
        // 工程与运行快照都必须带上空气格里的声明。
        declare(Json::array({{{"type", "chest_minecart"}, {"inventory", Json::array({stone(0, 3)})}}}));
        for (bool checkpoint : {false, true}) {
            auto saved = s.saveProject("carts", checkpoint); Simulator restored(r); restored.loadProject(saved);
            expect(restored.containerEntitiesJson(cart) == s.containerEntitiesJson(cart),
                   std::string(checkpoint ? "checkpoint" : "project") + " lost the container entity declaration");
        }
        auto snapshot = s.saveProject("carts", true);
        auto corrupt = [&](const std::function<void(Json&)>& mutate) {
            auto broken = snapshot;
            for (auto& row : broken["blockData"]) if (row["values"].contains("containerEntities")) mutate(row["values"]["containerEntities"]);
            Simulator target(r); bool threw = false;
            try { target.loadProject(broken); } catch (...) { threw = true; }
            return threw;
        };
        expect(corrupt([](Json& e) { e = Json::array(); }), "snapshot accepted noncanonical empty container entity list");
        expect(corrupt([](Json& e) { e.at(0)["type"] = "minecart"; }), "snapshot with a non-container minecart accepted");
        expect(corrupt([&](Json& e) { e.at(0)["inventory"] = Json::array({stone(27, 1)}); }), "snapshot with an out-of-range cart slot accepted");
        expect(corrupt([&](Json& e) { e.at(0)["inventory"] = Json::array({stone(0, 65)}); }), "snapshot with an over-stacked cart slot accepted");
        expect(corrupt([](Json& e) { e = Json::object(); }), "snapshot with a non-array cart list accepted");
        // 空数组整体移除，空气格上不再留下器件数据行。
        declare(Json::array());
        expect(s.containerEntitiesJson(cart).empty(), "clearing the declaration left entities behind");
        expect(s.saveProject("carts", true).at("blockData").empty(), "an empty declaration left a runtime row on air");
        auto rejects = [&](BlockPos pos, const Json& input) {
            bool threw = false; try { s.stimulate(pos, input); } catch (...) { threw = true; }
            return threw;
        };
        expect(rejects(cart, {{"containerEntities", Json::array({{{"type", "minecart"}}})}}), "a non-container minecart was accepted");
        expect(rejects(cart, {{"containerEntities", Json::array({{{"inventory", Json::array()}}})}}), "a container entity without a type was accepted");
        expect(rejects(cart, {{"containerEntities", Json::array({{{"type", "chest_minecart"}, {"x", 1}}})}}), "an unknown container entity field was accepted");
        expect(rejects(cart, {{"containerEntities", Json::array({{{"type", "hopper_minecart"}, {"inventory", Json::array({stone(5, 1)})}}})}}), "an out-of-range hopper minecart slot was accepted");
        expect(rejects(cart, {{"containerEntities", Json::array({{{"type", "chest_minecart"}, {"inventory", Json::array({stone(0, 65)})}}})}}), "an over-stacked cart slot was accepted");
        expect(rejects(cart, {{"containerEntities", Json::array({{{"type", "chest_minecart"}, {"inventory", Json::array({stone(1, 1), stone(1, 1)})}}})}}), "a duplicated cart slot was accepted");
        expect(rejects(cart, {{"containerEntities", Json::object()}}), "a non-array container entity list was accepted");
        expect(rejects(cart, {{"containerEntities", Json::array()}, {"viewers", 1}}), "a mixed container entity stimulus was accepted");
        Json many = Json::array();
        for (int i = 0; i < 17; ++i) many.push_back({{"type", "hopper_minecart"}, {"inventory", Json::array()}});
        expect(rejects(cart, {{"containerEntities", many}}), "more container entities than the declared limit were accepted");
        expect(s.containerEntitiesJson(cart).empty(), "a rejected declaration still changed the cell");
    });
    test("chest boats and rafts are container entities too", [&] {
        // 26.2 的 EntitySelector.CONTAINER_ENTITY_SELECTOR 是 `entity instanceof Container && isAlive()`。
        // 满足它的只有两条继承线：AbstractMinecartContainer（MinecartChest 27 槽 / MinecartHopper 5 槽）
        // 与 AbstractChestBoat（ChestBoat / ChestRaft，getContainerSize()=27，两个子类都不覆写）。
        // 木头种类各自是独立的 EntityType（EntityTypeIds.*_CHEST_BOAT / BAMBOO_CHEST_RAFT），共 10 个 ID。
        // 注意：**船这一支尚无原版差分**，下面全是内核内部一致性回归。
        // 船的运动/浮力/乘骑一律不建模，与矿车同一个约定：位置是输入，停在格中心。
        auto stone = [](int slot, int count) { return Json{{"slot", slot}, {"item", "minecraft:stone"}, {"count", count}}; };
        Simulator s(r); floor(s);
        const BlockPos cell{0, 2, 0};
        auto rejects = [&](BlockPos pos, const Json& input) {
            bool threw = false; try { s.stimulate(pos, input); } catch (...) { threw = true; }
            return threw;
        };
        auto declares = [&](const std::string& type, int slot) {
            return Json{{"containerEntities", Json::array({{{"type", type}, {"inventory", Json::array({stone(slot, 1)})}}})}};
        };
        // 槽位数逐个断言：最后一格可用、再往后一格拒绝，这样就不必把私有的
        // containerEntitySize 暴露出来也能钉死每个类型的 getContainerSize()。
        const std::vector<std::pair<std::string, int>> sizes{
            {"chest_minecart", 27}, {"hopper_minecart", 5},
            {"oak_chest_boat", 27}, {"spruce_chest_boat", 27}, {"birch_chest_boat", 27}, {"jungle_chest_boat", 27},
            {"acacia_chest_boat", 27}, {"dark_oak_chest_boat", 27}, {"mangrove_chest_boat", 27},
            {"cherry_chest_boat", 27}, {"pale_oak_chest_boat", 27}, {"bamboo_chest_raft", 27}};
        for (const auto& [type, size] : sizes) {
            s.stimulate(cell, declares(type, size - 1));
            expect(s.containerEntitiesJson(cell).at(0).at("inventory") == Json::array({stone(size - 1, 1)}),
                   type + " lost its last slot");
            expect(rejects(cell, declares(type, size)), type + " accepted a slot past " + std::to_string(size - 1));
            expect(s.containerEntitiesJson(cell).at(0).at("type") == type, "a rejected declaration changed " + type);
        }
        // 拒绝路径保持严格：普通船/竹筏不带箱子（不是 Container），驴/骡/羊驼只有
        // HasCustomInventoryScreen，玩家与铜傀儡实现的是 ContainerUser。类名不是注册 ID，也拒。
        for (const char* bad : {"oak_boat", "bamboo_raft", "donkey", "mule", "llama", "player", "copper_golem",
                                "minecart", "furnace_minecart", "tnt_minecart", "chest_boat", "chest_raft",
                                "minecraft:oak_chest_boat", "warped_chest_boat", ""})
            expect(rejects(cell, {{"containerEntities", Json::array({{{"type", bad}, {"inventory", Json::array()}}})}}),
                   std::string("a non-container entity type was accepted: ") + bad);
        s.stimulate(cell, {{"containerEntities", Json::array({{{"type", "bamboo_chest_raft"}, {"inventory", Json::array({stone(26, 1)})}}})}});
        // 快照往返：工程与检查点都要带上船的声明，破坏后的快照要被拒。
        s.stimulate(cell, {{"containerEntities", Json::array({{{"type", "cherry_chest_boat"}, {"inventory", Json::array({stone(0, 3), stone(26, 1)})}}})}});
        for (bool checkpoint : {false, true}) {
            auto saved = s.saveProject("boats", checkpoint); Simulator restored(r); restored.loadProject(saved);
            expect(restored.containerEntitiesJson(cell) == s.containerEntitiesJson(cell),
                   std::string(checkpoint ? "checkpoint" : "project") + " lost the chest boat declaration");
        }
        auto broken = s.saveProject("boats", true);
        for (auto& row : broken["blockData"]) if (row["values"].contains("containerEntities")) row["values"]["containerEntities"].at(0)["type"] = "oak_boat";
        Simulator target(r); bool threw = false;
        try { target.loadProject(broken); } catch (...) { threw = true; }
        expect(threw, "a snapshot with a non-container boat was accepted");

        // 漏斗从运输船拉取，与从运输矿车拉取逐条一致（同一条 getEntityContainer 路径）。
        Simulator pull(r); floor(pull);
        const BlockPos puller{0, 1, 0}, above{0, 2, 0};
        pull.place(puller, r.state("hopper", {{"facing", "down"}}));
        pull.stimulate(above, {{"containerEntities", Json::array({{{"type", "oak_chest_boat"}, {"inventory", Json::array({stone(0, 2)})}}})}});
        const auto beforeDraw = pull.randomState();
        pull.advanceTo(4);
        expect(pull.randomState() != beforeDraw, "a single chest boat candidate consumed no random draw");
        expect(pull.inventoryJson(puller, false) == Json::array({{{"slot", 0}, {"item", "minecraft:stone"}, {"count", 1}}}),
               "the hopper did not pull from the chest boat: " + pull.inventoryJson(puller, false).dump());
        expect(pull.containerEntitiesJson(above).at(0).at("inventory") == Json::array({stone(0, 1)}),
               "the chest boat slot was not decremented");
        pull.advanceTo(20);
        expect(pull.containerEntitiesJson(above).at(0).at("inventory").empty(), "the second pull did not empty the chest boat");
        // 向运输船推入：船不是方块实体，写入不通知比较器；27 槽从 slot 0 起填。
        Simulator push(r); floor(push);
        const BlockPos pusher{0, 1, 0}, front{1, 1, 0};
        push.place(pusher, r.state("hopper", {{"facing", "east"}}));
        push.stimulate(front, {{"containerEntities", Json::array({{{"type", "bamboo_chest_raft"}, {"inventory", Json::array()}}})}});
        push.stimulate(pusher, {{"inventory", Json::array({stone(0, 1)})}});
        push.advanceTo(4);
        expect(push.inventoryJson(pusher, false).empty(), "the hopper kept the item instead of pushing it into the raft");
        expect(push.containerEntitiesJson(front).at(0).at("inventory") == Json::array({stone(0, 1)}),
               "the chest raft did not receive the pushed item: " + push.containerEntitiesJson(front).dump());
        // 船装满 27 槽时推不进去，但候选非空仍然每刻抽一次（tryMoveItems 失败不设冷却）。
        auto draws = [](const Simulator& world) {
            return std::stoull(world.saveProject("draws", true).at("randomSource").at("draws").get<std::string>());
        };
        Simulator blocked(r); floor(blocked);
        blocked.place(pusher, r.state("hopper", {{"facing", "east"}}));
        Json full = Json::array();
        for (int slot = 0; slot < 27; ++slot) full.push_back(stone(slot, 64));
        blocked.stimulate(front, {{"containerEntities", Json::array({{{"type", "spruce_chest_boat"}, {"inventory", full}}})}});
        blocked.stimulate(pusher, {{"inventory", Json::array({{{"slot", 0}, {"item", "minecraft:dirt"}, {"count", 1}}})}});
        blocked.advanceTo(10); const auto atTen = draws(blocked);
        blocked.advanceTo(20);
        expect(draws(blocked) - atTen == 10, "a hopper blocked by a full chest boat stopped drawing: "
               + std::to_string(draws(blocked) - atTen) + " draws over 10 ticks");
        expect(blocked.containerEntitiesJson(front).at(0).at("inventory").size() == 27, "the full chest boat changed");
        // 船与矿车混放：候选集合按声明顺序，nextInt(2) 决定这一刻搬谁；
        // 同一格里的两个候选与「两辆矿车」那组走完全同一条代码路径。
        Simulator mixed(r); floor(mixed);
        mixed.place(puller, r.state("hopper", {{"facing", "down"}}));
        mixed.stimulate(above, {{"containerEntities", Json::array({
            {{"type", "chest_minecart"}, {"inventory", Json::array({stone(0, 1)})}},
            {{"type", "oak_chest_boat"}, {"inventory", Json::array({{{"slot", 0}, {"item", "minecraft:dirt"}, {"count", 1}}})}}})}});
        expect(mixed.containerEntitiesJson(above).size() == 2, "the mixed cart/boat declaration was not stored");
        // 与不带船的两候选场景相比，抽取序列与搬运结果必须逐刻完全一致。
        Simulator carts(r); floor(carts);
        carts.place(puller, r.state("hopper", {{"facing", "down"}}));
        carts.stimulate(above, {{"containerEntities", Json::array({
            {{"type", "chest_minecart"}, {"inventory", Json::array({stone(0, 1)})}},
            {{"type", "chest_minecart"}, {"inventory", Json::array({{{"slot", 0}, {"item", "minecraft:dirt"}, {"count", 1}}})}}})}});
        for (int tick = 1; tick <= 40; ++tick) {
            mixed.advanceTo(static_cast<Tick>(tick));
            carts.advanceTo(static_cast<Tick>(tick));
            expect(mixed.randomState() == carts.randomState(),
                   "a chest boat candidate drew differently from a chest minecart at tick " + std::to_string(tick));
            expect(mixed.inventoryJson(puller, false) == carts.inventoryJson(puller, false),
                   "a chest boat candidate transferred differently at tick " + std::to_string(tick));
        }
        expect(mixed.inventoryJson(puller, false).size() == 2, "the mixed candidates were not both drained: "
               + mixed.inventoryJson(puller, false).dump());
        for (int entity = 0; entity < 2; ++entity)
            expect(mixed.containerEntitiesJson(above).at(static_cast<std::size_t>(entity)).at("inventory").empty(),
                   "candidate " + std::to_string(entity) + " kept its item");
    });
    test("hoppers pull from and push into declared container minecarts", [&] {
        auto stone = [](int slot, int count) { return Json{{"slot", slot}, {"item", "minecraft:stone"}, {"count", count}}; };
        // 拉取：上方没有方块容器，改用实体容器；候选只有一个时原版仍然调用 nextInt(1)。
        Simulator s(r); floor(s);
        const BlockPos puller{0, 1, 0}, above{0, 2, 0};
        s.place(puller, r.state("hopper", {{"facing", "down"}}));
        s.stimulate(above, {{"containerEntities", Json::array({{{"type", "chest_minecart"}, {"inventory", Json::array({stone(0, 2)})}}})}});
        const auto beforeDraw = s.randomState();
        s.advanceTo(4);
        expect(s.randomState() != beforeDraw, "getEntityContainer with one candidate consumed no random draw");
        expect(s.inventoryJson(puller, false) == Json::array({{{"slot", 0}, {"item", "minecraft:stone"}, {"count", 1}}}),
               "hopper did not pull a single item from the chest minecart: " + s.inventoryJson(puller, false).dump());
        expect(s.containerEntitiesJson(above).at(0).at("inventory") == Json::array({stone(0, 1)}),
               "the minecart slot was not decremented: " + s.containerEntitiesJson(above).dump());
        s.advanceTo(20);
        expect(s.containerEntitiesJson(above).at(0).at("inventory").empty(), "the second pull did not empty the minecart");
        expect(s.inventoryJson(puller, false).at(0).at("count") == 2, "the hopper did not keep both pulled items");
        // 原版 suckInItems：容器分支（含实体容器）一旦命中就直接返回，根本不看掉落物，
        // 空的容器实体同样会把掉落物挡在外面。
        Simulator both(r); floor(both);
        both.place(puller, r.state("hopper", {{"facing", "down"}}));
        both.stimulate(puller, {{"groundItems", Json::array({{{"item", "minecraft:dirt"}, {"count", 1}, {"y", 1.2}}})}});
        both.stimulate(above, {{"containerEntities", Json::array({{{"type", "chest_minecart"}, {"inventory", Json::array({stone(0, 1)})}}})}});
        both.advanceTo(4);
        expect(both.inventoryJson(puller, false).at(0).at("item") == "minecraft:stone", "the hopper preferred the dropped item over the container entity");
        both.advanceTo(40);
        expect(both.suckableItems(puller).size() == 1, "an emptied container entity stopped shadowing the dropped item");
        // 方块容器优先：同一格既有箱子又有声明的矿车时，只看箱子，也不消耗随机数。
        Simulator shadowed(r); floor(shadowed);
        shadowed.place(puller, r.state("hopper", {{"facing", "down"}}));
        shadowed.place(above, r.state("chest"));
        shadowed.stimulate(above, {{"inventory", Json::array({{{"slot", 0}, {"item", "minecraft:dirt"}, {"count", 1}}})}});
        shadowed.stimulate(above, {{"containerEntities", Json::array({{{"type", "chest_minecart"}, {"inventory", Json::array({stone(0, 1)})}}})}});
        const auto shadowedDraw = shadowed.randomState();
        shadowed.advanceTo(4);
        expect(shadowed.randomState() == shadowedDraw, "a block container above still consulted the entity container");
        expect(shadowed.inventoryJson(puller, false).at(0).at("item") == "minecraft:dirt", "the hopper took from the shadowed minecart");
        expect(shadowed.containerEntitiesJson(above).at(0).at("inventory") == Json::array({stone(0, 1)}), "the shadowed minecart was modified");
        // 推出：朝向格没有方块容器时改用实体容器；矿车不是方块实体，写入不通知比较器。
        Simulator push(r); floor(push);
        const BlockPos pusher{0, 1, 0}, front{1, 1, 0};
        push.place(pusher, r.state("hopper", {{"facing", "east"}}));
        push.stimulate(front, {{"containerEntities", Json::array({{{"type", "hopper_minecart"}, {"inventory", Json::array()}}})}});
        push.stimulate(pusher, {{"inventory", Json::array({stone(0, 1)})}});
        push.advanceTo(4);
        expect(push.inventoryJson(pusher, false).empty(), "the hopper kept the item instead of pushing it into the minecart");
        expect(push.containerEntitiesJson(front).at(0).at("inventory") == Json::array({stone(0, 1)}),
               "the hopper minecart did not receive the pushed item: " + push.containerEntitiesJson(front).dump());
        auto saved = push.saveProject("push", true); Simulator restored(r); restored.loadProject(saved);
        expect(restored.containerEntitiesJson(front) == push.containerEntitiesJson(front), "transferred cart inventory lost across checkpoint");
        expect(restored.saveProject("push", true) == saved, "container entity checkpoint diverged");
    });
    test("a hopper blocked by a container entity keeps drawing every tick", [&] {
        // 原版 tryMoveItems 搬不动东西时**不设冷却**，下一刻整套重跑一遍。内核为省事在空转之后
        // 就不再排程，靠库存/拓扑/信号变化唤醒——只要空转确实没有可观测效果，这是等价的。
        // 容器实体打破了这个前提：只要那一格有矿车，getEntityContainer 每刻都消耗一次
        // nextInt，哪怕一件也搬不动。少抽的那些会让世界随机源整体错位，
        // 进而改变之后任何一次抽取（投掷器选槽等）的结果。
        auto stone = [](int slot, int count) { return Json{{"slot", slot}, {"item", "minecraft:stone"}, {"count", count}}; };
        auto dirt = Json::array({{{"slot", 0}, {"item", "minecraft:dirt"}, {"count", 1}}});
        auto draws = [](const Simulator& world) {
            return std::stoull(world.saveProject("draws", true).at("randomSource").at("draws").get<std::string>());
        };
        const BlockPos pusher{0, 1, 0}, front{1, 1, 0};
        // 推出侧：朝向格里是一辆装满的漏斗矿车，每刻都抽一次、每刻都搬不动。
        Simulator s(r); floor(s);
        s.place(pusher, r.state("hopper", {{"facing", "east"}}));
        Json full = Json::array();
        for (int slot = 0; slot < 5; ++slot) full.push_back(stone(slot, 64));
        s.stimulate(front, {{"containerEntities", Json::array({{{"type", "hopper_minecart"}, {"inventory", full}}})}});
        s.stimulate(pusher, {{"inventory", dirt}});
        s.advanceTo(10); const auto atTen = draws(s);
        s.advanceTo(20);
        expect(draws(s) - atTen == 10, "a hopper blocked by a full container entity stopped drawing: "
               + std::to_string(draws(s) - atTen) + " draws over 10 ticks");
        expect(s.inventoryJson(pusher, false) == dirt, "an item moved into a full minecart");
        expect(s.containerEntitiesJson(front).at(0).at("inventory").size() == 5, "the full minecart changed");
        // 拉取侧：上方是一辆空的运输矿车，同样每刻抽一次。
        Simulator pull(r); floor(pull);
        const BlockPos puller{0, 1, 0}, above{0, 2, 0};
        pull.place(puller, r.state("hopper", {{"facing", "down"}}));
        pull.stimulate(above, {{"containerEntities", Json::array({{{"type", "chest_minecart"}, {"inventory", Json::array()}}})}});
        pull.advanceTo(10); const auto pullTen = draws(pull);
        pull.advanceTo(20);
        expect(draws(pull) - pullTen == 10, "a hopper pulling from an empty container entity stopped drawing");
        // 对照一：那一格没有容器实体时不该有任何抽取，空转仍然可以休眠。
        Simulator idle(r); floor(idle);
        idle.place(pusher, r.state("hopper", {{"facing", "east"}}));
        idle.stimulate(pusher, {{"inventory", dirt}});
        idle.advanceTo(10); const auto idleTen = draws(idle);
        idle.advanceTo(20);
        expect(draws(idle) == idleTen, "an idle hopper with no container entity consumed randomness");
        // 对照二：搬成功那一刻起 8 gt 冷却，原版 isOnCooldown 期间根本走不到 getEntityContainer，
        // 所以冷却里的那几刻**不**抽——每 8 刻只有一次。
        Simulator moving(r); floor(moving);
        moving.place(pusher, r.state("hopper", {{"facing", "east"}}));
        moving.stimulate(front, {{"containerEntities", Json::array({{{"type", "chest_minecart"}, {"inventory", Json::array()}}})}});
        Json many = Json::array();
        for (int slot = 0; slot < 5; ++slot) many.push_back(stone(slot, 64));
        moving.stimulate(pusher, {{"inventory", many}});
        moving.advanceTo(10); const auto movingTen = draws(moving);
        moving.advanceTo(26);
        expect(draws(moving) - movingTen == 2, "a transferring hopper drew during its 8 gt cooldown: "
               + std::to_string(draws(moving) - movingTen) + " draws over 16 ticks");
    });
    test("full 26.2 sine table and daylight index boundaries", [&] {
        std::ifstream file(std::string(SIMULATOR_DATA_DIR) + "/../tests/fixtures/java26_2SineTable.json");
        expect(static_cast<bool>(file), "missing vanilla sine table fixture");
        auto fixture = Json::parse(file);
        const auto& bits = fixture.at("sineBits");
        const auto& table = daylightSineTable();
        expect(bits.size() == table.size(), "sine table size differs");
        for (std::size_t i = 0; i < table.size(); ++i) {
            std::int32_t actual{}; std::memcpy(&actual, &table[i], sizeof actual);
            expect(actual == bits[i].get<std::int32_t>(), "sine table entry " + std::to_string(i) + " differs: expected bits "
                + std::to_string(bits[i].get<std::int32_t>()) + " got " + std::to_string(actual));
        }
        Simulator s(r); BlockPos pos{0, 0, 0}; s.place(pos, r.state("daylight_detector"));
        for (const auto& sample : fixture.at("cosSamples")) for (int sky = 0; sky <= 15; ++sky) {
            // stimulate only schedules the next 20 gt poll; two interacts recompute in place.
            s.stimulate(pos, {{"skyBrightness", sky}, {"sunAngle", sample.at("degrees")}});
            s.interact(pos); s.interact(pos);
            expect(s.at(pos).power == sample.at("strengths").at(static_cast<std::size_t>(sky)).get<int>(),
                "daylight strength differs at " + sample.at("degrees").dump() + " sky " + std::to_string(sky));
        }
    });
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
    test("pot capacity, failed extraction sleep and checkpoint wake", [&] {
        Simulator s(r);const BlockPos pot{0,1,0},hopper{0,0,0};
        s.place(pot,r.state("decorated_pot"));s.place(hopper,r.state("hopper",{{"facing","east"}}));
        s.stimulate(pot,{{"inventory",Json::array({{{"slot",0},{"item","snowball"},{"count",16}}})}});
        expect(s.analogOutput(pot)==15,"full single-slot pot output");
        s.addProbe(pot,"pot");
        Json filled=Json::array();for(int i=0;i<5;++i) filled.push_back({{"slot",i},{"item","stone"},{"count",63}});
        s.stimulate(hopper,{{"inventory",filled}});auto edges=s.getTrace().size();s.advanceTo(10000);
        expect(s.pendingEvents()==0 && s.statistics.scheduledEvents==1 && s.getTrace().size()==edges,"failed pot extraction notified or kept polling");
        auto before=s.saveProject("pot",true);bool threw=false;
        try{s.stimulate(pot,{{"inventory",Json::array({{{"slot",0},{"item","snowball"},{"count",17}}})}});}catch(...){threw=true;}
        expect(threw && s.saveProject("pot",true)==before,"overstacked pot input changed world");
        s.stimulate(hopper,{{"inventory",Json::array({{{"slot",1},{"count",0}}})}});
        auto saved=s.saveProject("pot",true);Simulator restored(r);restored.loadProject(saved);
        s.advanceTo(10001);restored.advanceTo(10001);
        expect(s.inventoryJson(pot)[0]["count"]==15 && s.analogOutput(pot)==14,"pot did not wake and transfer");
        expect(s.getTrace().size()==edges+1 && s.getTrace().back().value==14,"pot transfer emitted phantom transitions");
        expect(s.saveProject("pot",true)==restored.saveProject("pot",true),"pot checkpoint continuation diverged");
        s.setBlock(pot,r.state("decorated_pot",{{"facing","south"},{"cracked","true"}}));
        expect(s.inventoryJson(pot)[0]["count"]==15,"pot state edit erased inventory");
        s.place(pot,r.state("stone"));expect(s.inventoryJson(pot).empty(),"pot removal left stale inventory");
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
