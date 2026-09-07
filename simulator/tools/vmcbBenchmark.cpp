#include "simulator/simulator.hpp"
#include "simulator/vmcbFormat.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <zstd.h>
#if defined(__APPLE__) || defined(__unix__)
#include <sys/resource.h>
#endif
using namespace simulator;
int main(int argc, char** argv) {
    try {
        if (argc != 4) throw std::invalid_argument("usage: vmcbBenchmark blocks dense|sparse|containers output.vmcb");
        const auto count = std::stoull(argv[1]); const std::string kind = argv[2];
        if (count == 0 || count > 2000000 || (kind != "dense" && kind != "sparse" && kind != "containers")) throw std::invalid_argument("invalid benchmark workload");
        BlockRegistry registry; Simulator source(registry); std::mt19937 random(24680);
        std::vector<StateId> states;
        for (const auto* color : {"white","orange","magenta","light_blue","yellow","lime","pink","gray","light_gray","cyan","purple","blue","brown","green","red","black"}) states.push_back(registry.state(std::string(color)+"_wool"));
        for (std::uint64_t i = 0; i < count; ++i) {
            BlockPos pos; StateId state;
            if (kind == "sparse") { const auto section = i / 64, local = (i % 64) * 64; pos = {static_cast<int>((section % 128) * 16),static_cast<int>((section / 128) * 16 + (local >> 8)),static_cast<int>((local >> 4) & 15)}; state = states[0]; }
            else { pos = {static_cast<int>(i % 128),static_cast<int>(i / (128*128)),static_cast<int>((i / 128)%128)}; state = kind == "containers" ? registry.state("barrel") : states[random()%states.size()]; }
            source.world.set(pos,state);
            if (kind == "containers") source.stimulate(pos,{{"inventory",Json::array({{{"slot",0},{"item","stone"},{"count",64}},{{"slot",13},{"item","redstone"},{"count",31}}})}});
        }
        using Clock=std::chrono::steady_clock; const auto writeStart=Clock::now();
        {std::ofstream output(argv[3],std::ios::binary);writeVmcb(output,source,{"VMCB 规模验证",{}},false);}
        const auto readStart=Clock::now();Simulator restored(registry);
        {std::ifstream input(argv[3],std::ios::binary);readProjectFile(input,restored);}
        const auto readEnd=Clock::now();bool equal=source.world.size()==restored.world.size();
        source.world.forEachCell([&](Cell cell){if(restored.world.get(cell.pos)!=cell.state)equal=false;if(kind=="containers"&&source.inventoryJson(cell.pos,false)!=restored.inventoryJson(cell.pos,false))equal=false;});
        if(!equal)throw std::runtime_error("roundtrip mismatch");
        Json result{{"kind",kind},{"blocks",count},{"sections",source.world.chunkCount()},{"bytes",std::filesystem::file_size(argv[3])},{"writeSeconds",std::chrono::duration<double>(readStart-writeStart).count()},{"readSeconds",std::chrono::duration<double>(readEnd-readStart).count()},{"roundTripEqual",equal},{"zstdVersion",ZSTD_versionString()}};
#if defined(__APPLE__) || defined(__unix__)
        rusage usage{};getrusage(RUSAGE_SELF,&usage);
#if defined(__APPLE__)
        result["peakRssBytes"]=usage.ru_maxrss;
#else
        result["peakRssBytes"]=usage.ru_maxrss*1024;
#endif
#endif
        if(count<=100000) {
            auto legacy=source.saveProject("VMCB 规模验证",false);
            for(const auto& [label,indent]:{std::pair{"Compact",-1},std::pair{"Pretty",2}}) {
                const auto json=legacy.dump(indent);std::vector<char> compressed(ZSTD_compressBound(json.size()));
                const auto size=ZSTD_compress(compressed.data(),compressed.size(),json.data(),json.size(),3);
                if(ZSTD_isError(size))throw std::runtime_error(ZSTD_getErrorName(size));
                result[std::string("legacy")+label+"Bytes"]=json.size();result[std::string("legacy")+label+"ZstdBytes"]=size;
            }
        }
        std::cout<<result.dump(2)<<'\n';return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
