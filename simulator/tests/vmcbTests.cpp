#include "simulator/simulator.hpp"
#include "simulator/vmcbFormat.hpp"
#include "simulator/vmcbEncoding.hpp"
#include <bit>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <zstd.h>

using namespace simulator;
using namespace simulator::vmcb;
namespace {
void expect(bool condition, const std::string& message) { if (!condition) throw std::runtime_error(message); }
std::string save(const Simulator& sim, bool checkpoint = true) { std::ostringstream output(std::ios::binary); writeVmcb(output, sim, {"往返测试", {-17, 0, 16}}, checkpoint); return output.str(); }
ProjectInfo load(Simulator& sim, const std::string& bytes, const ProjectFileOptions& options = {}) { std::istringstream input(bytes, std::ios::binary); return readProjectFile(input, sim, options); }
void roundTrip(Simulator& sim, Tick until) {
    const auto before = sim.saveProject("test", true); Simulator restored(sim.registry); auto info = load(restored, save(sim));
    expect(info.name == "往返测试" && info.origin == BlockPos{-17, 0, 16}, "metadata changed");
    expect(restored.saveProject("test", true) == before, "checkpoint changed: " + Json::diff(before, restored.saveProject("test", true)).dump());
    sim.advanceTo(until); restored.advanceTo(until); expect(sim.saveProject("test", true) == restored.saveProject("test", true), "checkpoint continuation diverged");
}
void base(Simulator& sim) { for (int x = -20; x < 25; ++x) for (int z = -1; z < 3; ++z) sim.world.set({x, 0, z}, sim.registry.state("stone")); }
// Rebuild a container independently of the production writer, storing every
// page raw. This lets structural tests mutate valid-CRC payloads.
std::string mutate(const std::string& source, const std::function<void(std::string&, Bytes&)>& edit) {
    Reader header(std::span(reinterpret_cast<const std::uint8_t*>(source.data()), source.size()).first(64)); header.take(16); const auto flags = header.integer(4), count = header.integer(4), directoryOffset = header.integer(8); header.take(16); const auto blocks = header.integer(8);
    Bytes data(64), directory;
    for (std::uint64_t i = 0; i < count; ++i) {
        const auto* start = reinterpret_cast<const std::uint8_t*>(source.data()) + directoryOffset + i * 64; Bytes entry(start, start + 64); Reader reader(entry); auto tagBytes = reader.take(4); std::string tag(tagBytes.begin(), tagBytes.end()); reader.take(8); auto codec = reader.integer(1); reader.take(19); auto offset = reader.integer(8), stored = reader.integer(8), rawSize = reader.integer(8);
        Bytes raw(static_cast<std::size_t>(rawSize));
        if (codec) { auto size = ZSTD_decompress(raw.data(), raw.size(), source.data() + offset, static_cast<std::size_t>(stored)); expect(!ZSTD_isError(size) && size == raw.size(), "test decompression failed"); }
        else std::copy_n(reinterpret_cast<const std::uint8_t*>(source.data()) + offset, raw.size(), raw.begin());
        edit(tag, raw); std::copy_n(tag.begin(), 4, entry.begin()); entry[12] = 0;
        auto patch = [&](std::size_t at, std::uint64_t value, unsigned width) { for (unsigned byte = 0; byte < width; ++byte) entry[at + byte] = static_cast<std::uint8_t>(value >> (byte * 8)); };
        patch(32, data.size(), 8); patch(40, raw.size(), 8); patch(48, raw.size(), 8); patch(56, crc(raw), 4); directory.insert(directory.end(), entry.begin(), entry.end()); data.insert(data.end(), raw.begin(), raw.end());
    }
    Bytes head{0x56,0x4d,0x43,0x42,13,10,26,10}; integer(head,1,2);integer(head,0,2);integer(head,64,2);integer(head,64,2);integer(head,flags,4);integer(head,count,4);integer(head,data.size(),8);integer(head,directory.size(),8);integer(head,data.size()+directory.size(),8);integer(head,blocks,8);integer(head,crc(directory),4);integer(head,crc(head),4);std::copy(head.begin(),head.end(),data.begin());data.insert(data.end(),directory.begin(),directory.end());return {data.begin(),data.end()};
}
}
int main(int argc, char** argv) {
    BlockRegistry registry;
    if (argc == 3 && std::string(argv[1]) == "--fixture") {
        Simulator sim(registry); base(sim); sim.place({-17,1,0}, registry.state("stone_button", {{"face","floor"}})); sim.addProbe({-17,1,0}, "button"); sim.interact({-17,1,0}); sim.advanceTo(7);
        std::ofstream output(argv[2], std::ios::binary); writeVmcb(output, sim, {"独立读取器验证",{}}, true); return 0;
    }
    int passed = 0, failed = 0;
    auto test = [&](const char* name, const auto& run) { try { run(); ++passed; std::cout << "PASS " << name << '\n'; } catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; } };
    test("CRC and independent fixed CBOR/layout vectors", [&] {
        const Bytes digits{'1','2','3','4','5','6','7','8','9'}; expect(crc(digits) == 0xcbf43926, "CRC vector");
        expect(encodeCbor(Json{{"aa",1},{"b",2}}) == Bytes({0xa2,0x61,0x62,2,0x62,0x61,0x61,1}), "CBOR key ordering");
        expect(encodeCbor(1.5) == Bytes({0xf9,0x3e,0}), "CBOR half"); expect(encodeCbor(-0.0) == Bytes({0xf9,0x80,0}), "CBOR negative zero");
        for (auto value : {0.0,-0.0,1.5,0.000000059604644775390625,3.141592653589793,65504.0,65536.0}) expect(std::bit_cast<std::uint64_t>(decodeCbor(encodeCbor(value)).get<double>()) == std::bit_cast<std::uint64_t>(value), "float bits changed");
        const Bytes uniform{0,0,0,0x10,1,0,0,0,7}; auto states = decodeSection(uniform, 8); expect(states[0] == 7 && states[4095] == 7 && encodeSection(states) == uniform, "uniform vector");
        const Bytes sparse{2,0,2,0,1,0,0,0,5,0,0xfe,0x1f}; states = decodeSection(sparse, 8); expect(states[0] == 5 && states[4095] == 5 && states[1] == 0 && encodeSection(states) == sparse, "sparse vector");
        const Bytes runs{3,1,1,0,2,0,0,0,0,5,2,0,0xfe,0x1f,1}; states = decodeSection(runs, 8); expect(states[0] == 5 && states[4095] == 0, "runs vector");
    });
    test("all layout modes, widths and malformed payloads", [&] {
        std::mt19937 random(456); std::set<unsigned> modes;
        for (unsigned count : {1u,2u,3u,4u,7u,16u,17u,255u,256u,257u,4096u}) for (int pattern = 0; pattern < 4; ++pattern) {
            std::array<StateId,4096> states{};
            for (unsigned i=0;i<4096;++i) states[i] = pattern == 0 ? 5 : pattern == 1 ? (i < 128 ? 1 : 0) : pattern == 2 ? (i % 64 == 0 ? random()%count+1 : 0) : random()%count+1;
            auto bytes=encodeSection(states); modes.insert(bytes[0]);expect(decodeSection(bytes,count+6)==states,"layout roundtrip");
            bytes.push_back(0);bool rejected=false;try{decodeSection(bytes,count+6);}catch(...){rejected=true;}expect(rejected,"trailing layout byte accepted");
        }
        expect(modes.size()==4,"did not exercise all modes");
        for (Bytes bytes : {Bytes{0xbf,0xff}, Bytes{0xa2,0x61,0x61,1,0x61,0x61,2}, Bytes{0x18,0x01}, Bytes{0xfb,0x7f,0xf0,0,0,0,0,0,0}}) {bool rejected=false;try{decodeCbor(bytes);}catch(...){rejected=true;}expect(rejected,"invalid CBOR accepted");}
    });
    test("empty circuit, checkpoint and native legacy JSON", [&] {
        Simulator sim(registry), restored(registry); load(restored,save(sim,false)); expect(restored.world.size()==0,"empty layout changed");roundTrip(sim,0);
        sim.setRandomSeed(UINT64_MAX); auto json=sim.saveProject("legacy",true);json["sequence"]=(1ULL<<53)+7;auto info=load(restored,json.dump(2));expect(info.name=="legacy" && restored.saveProject("legacy",true)==json,"native JSON lost integers");
    });
    test("global XYZ traversal and circuit initialization match legacy", [&] {
        Simulator sim(registry);base(sim);sim.place({-17,1,0},registry.state("lever",{{"face","floor"}}));sim.place({-16,1,0},registry.state("redstone_wire"));sim.place({-15,1,0},registry.state("repeater",{{"facing","west"}}));sim.place({-14,1,0},registry.state("redstone_lamp"));sim.addProbe({-14,1,0},"out");sim.interact({-17,1,0});sim.advanceTo(4);
        std::vector<Cell> ordered;sim.world.forEachCellXyz([&](Cell cell){ordered.push_back(cell);});auto cells=sim.world.cells();expect(ordered.size()==cells.size(),"XYZ size");for(std::size_t i=0;i<cells.size();++i)expect(ordered[i].pos==cells[i].pos&&ordered[i].state==cells[i].state,"XYZ order differs");
        Simulator json(registry),binary(registry);json.loadProject(sim.saveProject("design"));load(binary,save(sim,false));expect(json.saveProject("design",true)==binary.saveProject("design",true),"initialization diverged");roundTrip(sim,12);
    });
    test("binary chunk states and frozen queue survive reopening", [&] {
        Simulator sim(registry);base(sim);
        sim.place({0,1,0},registry.state("stone_button",{{"face","floor"}}));sim.interact({0,1,0});
        sim.advanceTo(3);sim.setChunkState(0,0,Simulator::ChunkState::loaded);sim.advanceTo(40);
        for(bool checkpoint:{false,true}) {
            Simulator restored(registry);load(restored,save(sim,checkpoint));
            expect(restored.chunkStatesJson()==sim.chunkStatesJson(),"VMCB dropped chunk states");
            if(checkpoint) {
                expect(restored.saveProject("test",true)==sim.saveProject("test",true),"frozen checkpoint changed");
                Simulator control(registry);control.loadProject(sim.saveProject("test",true));
                for(auto* world:{&control,&restored}) {world->setChunkState(0,0,Simulator::ChunkState::entityTicking);world->advanceTo(45);}
                expect(restored.saveProject("test",true)==control.saveProject("test",true),"resumed binary queue diverged");
                expect(!restored.at({0,1,0}).powered,"restored button did not release");
            }
        }
    });
    test("rule digest covers block tags", [&] {
        auto path=std::filesystem::path(SIMULATOR_DATA_DIR);
        const auto dir=std::filesystem::temp_directory_path()/ ("verimcTagDigest"+std::to_string(std::random_device{}()));
        std::filesystem::create_directory(dir);
        for(auto& item:std::filesystem::directory_iterator(path))if(item.is_regular_file())std::filesystem::copy_file(item.path(),dir/item.path().filename());
        auto before=rulesDigest((dir/"blockStates.json").string());
        { std::ofstream output(dir/"blockTags.json",std::ios::app); output << '\n'; }
        expect(before!=rulesDigest((dir/"blockStates.json").string()),"tag change did not change rules digest");
    });
    test("binary chunk metadata and old checkpoint ABI reject atomically", [&] {
        Simulator sim(registry);base(sim);sim.place({0,1,0},registry.state("stone"));
        const auto before=sim.saveProject("unchanged",true);
        for(bool checkpoint:{false,true})for(auto problem:{"missing","duplicate","invalid","oldAbi"}) {
            if(!checkpoint && std::string(problem)=="oldAbi")continue;
            auto bytes=mutate(save(sim,checkpoint),[&](std::string& tag,Bytes& raw){
                if(tag!="META")return;
                auto meta=decodeCbor(raw);
                Json row={{"chunk",Json::array({0,0})},{"state","loaded"},{"stalledSince",std::uint64_t{0}}};
                if(std::string(problem)=="missing")meta.erase("chunkStates");
                else if(std::string(problem)=="duplicate")meta["chunkStates"]=Json::array({row,row});
                else if(std::string(problem)=="invalid") {row["state"]="unloaded";meta["chunkStates"]=Json::array({row});}
                else meta["snapshot"]["checkpointAbi"]="simulatorCheckpoint1";
                raw=encodeCbor(meta);
            });
            bool rejected=false;try{load(sim,bytes);}catch(...){rejected=true;}
            expect(rejected && sim.saveProject("unchanged",true)==before,"corrupt chunk metadata changed live world");
        }
    });
    test("button phase, history budgets and big counters", [&] {
        Simulator sim(registry);base(sim);sim.traceCapacity=10;sim.traceAtomicReserve=20;sim.updateBudget=99999;sim.place({0,1,0},registry.state("stone_button",{{"face","floor"}}));sim.addProbe({0,1,0},"button");sim.interact({0,1,0});sim.advanceTo(7);Simulator restored(registry);load(restored,save(sim));expect(restored.traceCapacity==10&&restored.traceAtomicReserve==20&&restored.updateBudget==99999,"budgets not restored");roundTrip(sim,22);
    });
    test("moving piston checkpoint", [&] { Simulator sim(registry);sim.place({0,0,0},registry.state("sticky_piston",{{"facing","east"}}));sim.place({1,0,0},registry.state("stone"));sim.place({-1,0,0},registry.state("redstone_block"));sim.advanceTo(1);roundTrip(sim,5); });
    test("removed torch history survives checkpoint", [&] {
        Simulator sim(registry);base(sim);const BlockPos torch{0,1,0},lever{-1,0,0};
        sim.place(torch,registry.state("redstone_torch"));sim.place(lever,registry.state("lever",{{"face","wall"},{"facing","west"}}));
        for(int cycle=0;cycle<7;++cycle){sim.interact(lever);sim.advanceTo(sim.currentTick+2);sim.interact(lever);sim.advanceTo(sim.currentTick+2);}
        sim.setBlock(torch,0);expect(sim.saveProject("test",true).at("torchToggles").size()==7,"no retained torch history");
        Simulator restored(registry);load(restored,save(sim));
        for(auto* world:{&sim,&restored}){world->place(torch,registry.state("redstone_torch"));world->interact(lever);world->advanceTo(30);world->interact(lever);world->advanceTo(32);expect(!world->at(torch).lit,"burnout history lost");world->advanceTo(190);}
        expect(sim.saveProject("test",true)==restored.saveProject("test",true),"torch continuation diverged");
    });
    test("partly executed tick batch resumes event by event", [&] {
        Simulator sim(registry);const BlockPos first{0,0,0},second{1,0,0};
        sim.world.set(first,registry.state("observer",{{"facing","east"}}));sim.world.set(second,registry.state("observer",{{"facing","west"}}));
        sim.schedule(first,2);sim.schedule(second,2);sim.stepEvent();expect(sim.saveProject("test",true).at("blockTickState").at("batch").size()==1,"no partial tick batch");
        Simulator restored(registry);load(restored,save(sim));
        for(int event=0;event<12;++event){sim.stepEvent();restored.stepEvent();expect(sim.saveProject("test",true)==restored.saveProject("test",true),"event order changed after resume");}
    });
    test("hopper inventory and dropper external actions", [&] {
        Simulator sim(registry);sim.place({0,0,0},registry.state("hopper",{{"facing","east"}}));sim.place({1,0,0},registry.state("barrel"));sim.stimulate({0,0,0},{{"inventory",Json::array({{{"slot",0},{"item","stone"},{"count",8}}})}});sim.advanceTo(1);roundTrip(sim,9);
        sim.clear();sim.place({0,0,0},registry.state("dropper",{{"facing","east"}}));sim.stimulate({0,0,0},{{"inventory",Json::array({{{"slot",0},{"item","stone"},{"count",8}}})}});sim.place({-1,0,0},registry.state("redstone_block"));sim.advanceTo(4);expect(sim.hasPendingActions(),"no external action");roundTrip(sim,4);
    });
    test("bell, note, jukebox and sensor checkpoints", [&] {
        Simulator sim(registry);base(sim);sim.place({0,1,0},registry.state("bell"));sim.place({1,1,0},registry.state("redstone_block"));sim.advanceTo(2);roundTrip(sim,55);
        sim.clear();sim.place({0,0,0},registry.state("note_block"));sim.place({1,0,0},registry.state("redstone_block"));roundTrip(sim,3);
        sim.clear();sim.place({0,0,0},registry.state("jukebox"));sim.stimulate({0,0,0},{{"inventory",Json::array({{{"slot",0},{"item","music_disc_13"},{"count",1}}})}});sim.advanceTo(3);roundTrip(sim,20);
        sim.clear();base(sim);sim.place({0,1,0},registry.state("sculk_sensor"));sim.place({3,1,0},registry.state("lever",{{"face","floor"}}));sim.interact({3,1,0});sim.advanceTo(1);roundTrip(sim,10);
        sim.clear();base(sim);sim.place({0,1,0},registry.state("sculk_sensor"));sim.stimulate({3,1,0},{{"gameEvent","minecraft:step"},{"affectedBlock",{{"name","minecraft:tnt"}}}});
        expect(!sim.saveProject("test",true).at("sensors").empty(),"no vibration context");roundTrip(sim,10);
    });
    test("corrupt, incompatible and cancelled imports are atomic", [&] {
        Simulator sim(registry);base(sim);sim.place({0,1,0},registry.state("stone_button",{{"face","floor"}}));sim.addProbe({0,1,0},"input");sim.interact({0,1,0});sim.advanceTo(2);const auto bytes=save(sim),before=sim.saveProject("test",true).dump();
        auto reject=[&](const std::string& bad){bool rejected=false;try{load(sim,bad);}catch(...){rejected=true;}expect(rejected&&sim.saveProject("test",true).dump()==before,"invalid import changed world");};
        for(std::size_t n:{0u,3u,63u,64u,99u})reject(bytes.substr(0,n));reject(bytes.substr(0,bytes.size()-1));
        auto broken=bytes;broken[60]^=1;reject(broken);broken=bytes;broken.back()^=1;reject(broken);
        for(const auto* field:{"rulesDigest","checkpointAbi","palette","duplicateProbe","count"}) {
            reject(mutate(bytes,[&](std::string& tag,Bytes& raw){
                if(tag=="META"&&(std::string(field)=="rulesDigest"||std::string(field)=="checkpointAbi")){auto meta=decodeCbor(raw);if(std::string(field)=="rulesDigest")meta["rulesDigest"]=Json::binary(Bytes(32));else meta["snapshot"]["checkpointAbi"]="future";raw=encodeCbor(meta);}
                if(tag=="BLKS"&&std::string(field)=="count")raw[2]^=1;
                if(tag=="PALT"&&std::string(field)=="palette")raw[1]=127;
                if(tag=="PROB"&&std::string(field)=="duplicateProbe"){auto probes=decodeCbor(raw);probes.push_back(probes[0]);raw=encodeCbor(probes);}
            }));
        }
        ProjectFileOptions options;options.progress=[](const std::string& phase,std::uint64_t,std::uint64_t){if(phase=="构建方块分区")throw std::runtime_error("cancelled");};bool rejected=false;try{load(sim,bytes,options);}catch(...){rejected=true;}expect(rejected&&sim.saveProject("test",true).dump()==before,"cancel changed world");
        std::mt19937 random(99);for(int i=0;i<250;++i){auto fuzz=bytes;fuzz[random()%fuzz.size()]^=static_cast<char>(1u<<(random()%8));try{load(sim,fuzz);}catch(...){ }expect(sim.saveProject("test",true).dump()==before,"bit flip changed semantics");}
    });
    std::cout<<passed<<" passed, "<<failed<<" failed\n";return failed?1:0;
}
