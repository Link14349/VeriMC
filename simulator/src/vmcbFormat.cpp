#include "simulator/vmcbFormat.hpp"
#include "buildIdentity.hpp"
#include "simulator/vmcbEncoding.hpp"
#include "simulator/projectIo.hpp"
#include "simulator/simulator.hpp"
#include <algorithm>
#include <bit>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <zstd.h>

namespace simulator {
namespace {
using namespace vmcb;
constexpr std::array<std::uint8_t, 8> magic{0x56, 0x4d, 0x43, 0x42, 13, 10, 26, 10};
constexpr const char* referenceSha = "823e2250d24b3ddac457a60c92a6a941943fcd6a";
constexpr const char* checkpointAbi = "simulatorCheckpoint2";
constexpr std::array<const char*, 8> runtimeTables{"blockTickBatch", "environmentActions", "events", "hoppers", "jukeboxes", "motions", "sensors", "torchToggles"};
void require(bool condition, const std::string& message) { if (!condition) throw std::invalid_argument("VMCB：" + message); }
std::uint32_t narrow(std::uint64_t value) { require(value <= UINT32_MAX, "32 位整数溢出"); return static_cast<std::uint32_t>(value); }
std::int32_t signed32(std::uint64_t value) { return std::bit_cast<std::int32_t>(narrow(value)); }
BlockPos sectionOf(BlockPos pos) { return {pos.x >> 4, pos.y >> 4, pos.z >> 4}; }
unsigned localIndex(BlockPos pos) { return (static_cast<unsigned>(pos.x) & 15u) | ((static_cast<unsigned>(pos.z) & 15u) << 4) | ((static_cast<unsigned>(pos.y) & 15u) << 8); }
BlockPos absolutePos(BlockPos section, unsigned local) {
    require(local < 4096, "局部坐标越界");
    auto axis = [](std::int32_t coarse, unsigned fine) { const auto value = static_cast<std::int64_t>(coarse) * 16 + fine; require(value >= -29999984 && value <= 29999984, "方块坐标越界"); return static_cast<std::int32_t>(value); };
    return {axis(section.x, local & 15), axis(section.y, local >> 8), axis(section.z, (local >> 4) & 15)};
}
void report(const ProjectFileOptions& options, const std::string& phase, std::uint64_t done = 0, std::uint64_t total = 0) { if (options.progress) options.progress(phase, done, total); }
void writeBytes(std::ostream& output, std::span<const std::uint8_t> bytes) {
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())); if (!output) throw std::runtime_error("工程文件写入失败");
}
Bytes readBytes(std::istream& input, std::uint64_t offset, std::size_t count) {
    input.clear(); input.seekg(static_cast<std::streamoff>(offset)); require(static_cast<bool>(input), "文件定位失败");
    Bytes bytes(count); input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(count)); require(input.gcount() == static_cast<std::streamsize>(count), "文件截断"); return bytes;
}
std::uint64_t streamLength(std::istream& input) { input.clear(); input.seekg(0, std::ios::end); const auto end = input.tellg(); require(end >= 0, "无法读取文件长度"); return static_cast<std::uint64_t>(end); }
void arrayHead(Bytes& bytes, std::size_t count) {
    if (count < 24) bytes.push_back(static_cast<std::uint8_t>(0x80 | count));
    else { const unsigned width = count <= 255 ? 1 : count <= 65535 ? 2 : 4; bytes.push_back(static_cast<std::uint8_t>(width == 1 ? 0x98 : width == 2 ? 0x99 : 0x9a)); for (unsigned i = width; i > 0; --i) bytes.push_back(static_cast<std::uint8_t>(count >> ((i - 1) * 8))); }
}
struct Entry {
    std::string tag;
    std::uint16_t schemaMajor{1}, schemaMinor{};
    std::uint32_t flags{1};
    std::uint8_t codec{};
    BlockPos section{};
    std::uint32_t part{};
    std::uint64_t offset{}, stored{}, raw{};
    std::uint32_t checksum{};
    std::string table;
    auto key() const { return std::tuple(tag, section, part); }
};
Bytes entryBytes(const Entry& entry) {
    Bytes bytes(entry.tag.begin(), entry.tag.end()); integer(bytes, entry.schemaMajor, 2); integer(bytes, entry.schemaMinor, 2); integer(bytes, entry.flags, 4);
    integer(bytes, entry.codec, 1); integer(bytes, 0, 3);
    for (auto axis : {entry.section.x, entry.section.y, entry.section.z}) integer(bytes, static_cast<std::uint32_t>(axis), 4);
    integer(bytes, entry.part, 4); integer(bytes, entry.offset, 8); integer(bytes, entry.stored, 8); integer(bytes, entry.raw, 8); integer(bytes, entry.checksum, 4); integer(bytes, 0, 4); return bytes;
}
Entry parseEntry(std::span<const std::uint8_t> bytes) {
    Reader reader(bytes); Entry entry; auto tag = reader.take(4); entry.tag.assign(tag.begin(), tag.end());
    entry.schemaMajor = static_cast<std::uint16_t>(reader.integer(2)); entry.schemaMinor = static_cast<std::uint16_t>(reader.integer(2)); entry.flags = narrow(reader.integer(4)); entry.codec = static_cast<std::uint8_t>(reader.integer(1));
    require(reader.integer(3) == 0, "目录保留位非零"); entry.section = {signed32(reader.integer(4)), signed32(reader.integer(4)), signed32(reader.integer(4))}; entry.part = narrow(reader.integer(4));
    entry.offset = reader.integer(8); entry.stored = reader.integer(8); entry.raw = reader.integer(8); entry.checksum = narrow(reader.integer(4)); require(reader.integer(4) == 0, "目录保留位非零"); return entry;
}
void zstdCheck(std::size_t code) { if (ZSTD_isError(code)) throw std::invalid_argument(std::string("VMCB 压缩数据无效：") + ZSTD_getErrorName(code)); }
std::uint64_t decimal(const Json& value) {
    require(value.is_string(), "随机数必须为十进制字符串"); const auto text = value.get<std::string>();
    require(!text.empty() && std::all_of(text.begin(), text.end(), [](unsigned char c) { return c >= '0' && c <= '9'; }), "随机数不是十进制整数"); return std::stoull(text);
}
Json describe(const BlockRegistry& registry, StateId state) { auto value = registry.describe(state); value.erase("stateId"); return value; }
std::pair<std::string, Json> stateKey(const Json& value) { return {value.at("name").get<std::string>(), value.at("properties")}; }

class VmcbSink final : public ProjectSink {
    std::ostream& output;
    const BlockRegistry& registry;
    const ProjectInfo& info;
    const ProjectFileOptions& options;
    std::vector<Entry> entries;
    std::vector<StateId> toFile;
    std::uint64_t blockCount{}, offset{64}, rawTotal{};
    bool checkpoint{};
    std::string table, tag;
    BlockPos section{};
    Bytes page;
    std::uint32_t rowsInPage{}, part{};
    std::size_t tableRows{};
    std::unique_ptr<ZSTD_CCtx, decltype(&ZSTD_freeCCtx)> compressor{ZSTD_createCCtx(), ZSTD_freeCCtx};
    void add(std::string kind, Bytes bytes, BlockPos pos = {}, std::uint32_t pageIndex = 0, std::string tableName = {}) {
        check(); require(!bytes.empty() && bytes.size() <= maxPageBytes, "数据段超过 8 MiB"); require(entries.size() < 2000000, "目录超过预算");
        rawTotal += bytes.size(); require(rawTotal <= 4ULL * 1024 * 1024 * 1024, "解压总量超过预算");
        Entry entry; entry.tag = std::move(kind); entry.section = pos; entry.part = pageIndex; entry.offset = offset; entry.raw = bytes.size(); entry.checksum = crc(bytes); entry.table = std::move(tableName);
        Bytes compressed(ZSTD_compressBound(bytes.size()));
        const auto length = ZSTD_compress2(compressor.get(), compressed.data(), compressed.size(), bytes.data(), bytes.size()); zstdCheck(length);
        if (length < bytes.size()) { compressed.resize(length); entry.codec = 1; bytes = std::move(compressed); }
        entry.stored = bytes.size(); require(offset <= options.maxFileBytes && entry.stored + (entries.size() + 1) * 64 <= options.maxFileBytes - offset, "文件超过写入预算");
        writeBytes(output, bytes); offset += bytes.size(); entries.push_back(std::move(entry));
    }
    void flushPage() {
        Bytes payload;
        if (tag == "ORDR" || tag == "TRCE") integer(payload, rowsInPage, 4);
        else if (tag == "RUNT") { payload.push_back(0xa2); auto key = encodeCbor("rows"); payload.insert(payload.end(), key.begin(), key.end()); arrayHead(payload, rowsInPage); }
        else arrayHead(payload, rowsInPage);
        payload.insert(payload.end(), page.begin(), page.end());
        if (tag == "RUNT") { auto key = encodeCbor("table"), name = encodeCbor(table); payload.insert(payload.end(), key.begin(), key.end()); payload.insert(payload.end(), name.begin(), name.end()); }
        add(tag, std::move(payload), section, part++, table); page.clear(); rowsInPage = 0;
    }
    void mapVibration(Json& value) {
        if (value.contains("affectedBlock")) { const auto& block = value.at("affectedBlock"); value["affectedPaletteId"] = toFile.at(registry.state(block.at("name"), block.at("properties"))); value.erase("affectedBlock"); }
    }
public:
    VmcbSink(std::ostream& target, const BlockRegistry& blocks, const ProjectInfo& project, const ProjectFileOptions& limits) : output(target), registry(blocks), info(project), options(limits) {
        require(static_cast<bool>(compressor), "无法创建压缩器"); zstdCheck(ZSTD_CCtx_setParameter(compressor.get(), ZSTD_c_compressionLevel, 3)); zstdCheck(ZSTD_CCtx_setParameter(compressor.get(), ZSTD_c_windowLog, 23));
    }
    void check() const override { report(options, "编码文件", offset); }
    void begin(const Json& data, const World& world, std::span<const StateId> additional) override {
        blockCount = world.size(); checkpoint = data.at("kind") == "checkpoint"; require(blockCount <= options.maxBlocks, "工程超过方块预算");
        require(info.name.size() <= 16384, "工程名称超过 16 KiB"); validUtf8(info.name); (void)Json(info.origin).get<BlockPos>();
        output.seekp(0); writeBytes(output, Bytes(64));
        Json meta{{"name", info.name}, {"edition", "java"}, {"minecraftVersion", "26.2"}, {"referenceServerSha1", referenceSha}, {"writerVersion", "simulator-0.1.0"}, {"rulesDigest", Json::binary(registry.ruleFingerprint())}, {"profile", data.at("profile")}, {"origin", info.origin}, {"initialization", checkpoint ? "checkpoint1" : "canonicalXYZ1"}, {"randomSource", {{"algorithm", "javaLegacy48"}, {"seed", decimal(data.at("randomSource").at("seed"))}}}, {"nextEntityOrder", data.at("nextEntityOrder")}};
        meta["chunkStates"] = data.value("chunkStates", Json::array());
        if (checkpoint) {
            Json snapshot{{"checkpointAbi", checkpointAbi}, {"engineBuild", SIMULATOR_BUILD_ID}, {"randomState", data.at("randomSource").at("state")}, {"randomDraws", decimal(data.at("randomSource").at("draws"))}, {"blockTickEarliestCollection", data.at("blockTickState").at("earliestCollection")}};
            for (const auto* field : {"tick", "nextOrder", "sequence", "nextProbeId", "nextActionId", "actionsDropped", "traceDropped", "faulted"}) snapshot[field] = data.at(field);
            for (const auto* field : {"traceCapacity", "traceAtomicReserve", "updateBudget"}) snapshot[field] = data.at("loadSettings").at(field);
            meta["snapshot"] = std::move(snapshot);
        }
        add("META", encodeCbor(meta));
        std::vector<bool> used(registry.stateCount(), false); used.at(0) = true; const auto sections = world.sectionPositions();
        for (auto pos : sections) { check(); for (auto state : world.sectionStates(pos)) used.at(state) = true; }
        for (auto state : additional) used.at(state) = true;
        std::vector<std::pair<Json, StateId>> palette;
        for (StateId state = 1; state < used.size(); ++state) if (used[state]) palette.emplace_back(describe(registry, state), state);
        std::sort(palette.begin(), palette.end(), [](const auto& a, const auto& b) { return stateKey(a.first) < stateKey(b.first); }); palette.insert(palette.begin(), {describe(registry, 0), 0});
        require(palette.size() <= 262144, "状态表超过预算"); toFile.assign(registry.stateCount(), UINT32_MAX); Bytes encoded; varInt(encoded, static_cast<std::uint32_t>(palette.size()));
        for (std::size_t i = 0; i < palette.size(); ++i) {
            const auto& [block, state] = palette[i]; toFile[state] = static_cast<StateId>(i); string(encoded, block.at("name")); const auto& properties = block.at("properties"); varInt(encoded, static_cast<std::uint32_t>(properties.size()));
            for (auto it = properties.begin(); it != properties.end(); ++it) { string(encoded, it.key()); string(encoded, it.value()); }
        }
        add("PALT", std::move(encoded));
        std::uint64_t done = 0;
        for (auto pos : sections) {
            report(options, "编码方块分区", done++, sections.size()); std::array<StateId, 4096> states{}; const auto native = world.sectionStates(pos);
            for (std::size_t i = 0; i < states.size(); ++i) states[i] = toFile.at(native[i]); add("BLKS", encodeSection(states), pos);
        }
    }
    void beginTable(const std::string& name) override {
        table = name; tag = name == "blockData" ? "BDAT" : name == "entityOrder" ? "ORDR" : name == "probes" ? "PROB" : name == "trace" ? "TRCE" : "RUNT";
        page.clear(); rowsInPage = 0; part = 0; tableRows = 0; section = {};
    }
    void row(Json value) override {
        check(); Bytes bytes;
        if (tag == "BDAT") {
            auto pos = value.at("pos").get<BlockPos>(); const auto nextSection = sectionOf(pos);
            if (tableRows && nextSection != section) { flushPage(); part = 0; }
            section = nextSection; value.erase("pos"); value["localIndex"] = localIndex(pos);
        } else if (tag == "PROB" && !checkpoint) value.erase("lastValue");
        if (table == "events" || table == "blockTickBatch") { value["blockName"] = registry.blockType(value.at("type").get<std::uint16_t>()).name; value.erase("type"); }
        if (table == "motions") value["movedState"] = toFile.at(value.at("movedState").get<StateId>());
        if (table == "sensors") for (const auto* field : {"candidate", "current"}) if (value.contains(field)) mapVibration(value[field]);
        if (tag == "ORDR") { auto pos = value.at("pos").get<BlockPos>(); for (auto axis : {pos.x, pos.y, pos.z}) integer(bytes, static_cast<std::uint32_t>(axis), 4); integer(bytes, unsignedValue(value.at("order")), 8); }
        else if (tag == "TRCE") { for (std::size_t i = 0; i < 3; ++i) integer(bytes, unsignedValue(value.at(i)), 8); integer(bytes, unsignedValue(value.at(3), 15), 1); }
        else bytes = encodeCbor(value);
        if (rowsInPage && page.size() + bytes.size() > 512 * 1024) flushPage();
        page.insert(page.end(), bytes.begin(), bytes.end()); ++rowsInPage; ++tableRows;
    }
    void endTable() override { if (rowsInPage || (tableRows == 0 && tag != "BDAT")) flushPage(); }
    void finish() override {
        // Physical pages stay in streaming order. Only directory RUNT indices
        // are assigned in table order, independent of the core's emit order.
        std::vector<Entry*> runtime;
        for (auto& entry : entries) if (entry.tag == "RUNT") runtime.push_back(&entry);
        std::sort(runtime.begin(), runtime.end(), [](const Entry* a, const Entry* b) { return std::tie(a->table, a->part) < std::tie(b->table, b->part); });
        for (std::size_t i = 0; i < runtime.size(); ++i) runtime[i]->part = static_cast<std::uint32_t>(i);
        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.key() < b.key(); });
        Bytes directory; directory.reserve(entries.size() * 64); for (const auto& entry : entries) { auto bytes = entryBytes(entry); directory.insert(directory.end(), bytes.begin(), bytes.end()); }
        writeBytes(output, directory); Bytes header(magic.begin(), magic.end()); integer(header, 1, 2); integer(header, 0, 2); integer(header, 64, 2); integer(header, 64, 2); integer(header, checkpoint ? 1 : 0, 4); integer(header, entries.size(), 4); integer(header, offset, 8); integer(header, directory.size(), 8); integer(header, offset + directory.size(), 8); integer(header, blockCount, 8); integer(header, crc(directory), 4); integer(header, crc(header), 4);
        output.seekp(0); writeBytes(output, header); output.seekp(0, std::ios::end); output.flush(); if (!output) throw std::runtime_error("工程文件写入失败"); report(options, "文件已生成", offset + directory.size(), offset + directory.size());
    }
};
class VmcbSource final : public ProjectSource {
    std::istream& input;
    const BlockRegistry& registry;
    const ProjectFileOptions& options;
    std::vector<Entry> entries;
    std::map<std::string, std::vector<std::size_t>> tables;
    std::vector<StateId> palette;
    Json data;
    bool checkpoint{};
    std::uint64_t declaredBlocks{};
    std::unique_ptr<ZSTD_DCtx, decltype(&ZSTD_freeDCtx)> decompressor{ZSTD_createDCtx(), ZSTD_freeDCtx};
    Bytes payload(const Entry& entry) {
        report(options, "校验和解压", entry.offset);
        auto bytes = readBytes(input, entry.offset, static_cast<std::size_t>(entry.stored));
        if (entry.codec == 1) {
            require(ZSTD_getDictID_fromFrame(bytes.data(), bytes.size()) == 0, "不支持外部压缩字典");
            require(ZSTD_getFrameContentSize(bytes.data(), bytes.size()) == entry.raw, "压缩帧声明长度不符");
            const auto frameSize = ZSTD_findFrameCompressedSize(bytes.data(), bytes.size()); zstdCheck(frameSize); require(frameSize == bytes.size(), "禁止压缩帧拼接或尾随数据");
            Bytes raw(static_cast<std::size_t>(entry.raw)); const auto length = ZSTD_decompressDCtx(decompressor.get(), raw.data(), raw.size(), bytes.data(), bytes.size()); zstdCheck(length); require(length == raw.size(), "实际解压长度不符"); bytes = std::move(raw);
        }
        require(crc(bytes) == entry.checksum, entry.tag + " 段 CRC 校验失败（偏移 " + std::to_string(entry.offset) + "）"); return bytes;
    }
    void readPalette(const Bytes& bytes) {
        Reader reader(bytes); const auto count = reader.varInt(); require(count > 0 && count <= 262144, "无效状态表大小"); palette.reserve(count); Json previous;
        std::set<StateId> unique;
        for (std::uint32_t i = 0; i < count; ++i) {
            auto name = reader.string(); const auto propertiesCount = reader.varInt(); require(propertiesCount <= 256, "属性数量过多"); Json properties = Json::object(); std::string last;
            for (std::uint32_t j = 0; j < propertiesCount; ++j) { auto key = reader.string(); require(j == 0 || key > last, "属性未排序或重复"); last = key; properties[key] = reader.string(); }
            Json block{{"name", name}, {"properties", properties}};
            if (i == 0) require(name == "minecraft:air" && properties.empty(), "状态表 0 必须为空气");
            else if (i > 1) require(stateKey(previous) < stateKey(block), "状态表未排序");
            const auto state = registry.state(name, properties); require(describe(registry, state) == block && unique.insert(state).second, "状态属性不完整或重复");
            // Known states may be vibration context without being simulated
            // blocks. The common world loader checks physical device support.
            palette.push_back(state); previous = std::move(block);
        }
        reader.finish();
    }
    void readMetadata(const Json& meta) {
        require(meta.is_object() && meta.at("edition") == "java" && meta.at("minecraftVersion") == "26.2" && meta.at("referenceServerSha1") == referenceSha, "Minecraft 版本或参考构建不匹配");
        require(meta.at("rulesDigest").is_binary() && meta.at("rulesDigest") == Json::binary(registry.ruleFingerprint()), "规则数据指纹不匹配，需要显式迁移");
        require(meta.at("initialization") == (checkpoint ? "checkpoint1" : "canonicalXYZ1"), "初始化方式与文件类型不匹配");
        require(meta.at("writerVersion").is_string(), "缺少写入器版本"); info.name = meta.at("name").get<std::string>(); require(info.name.size() <= 16384, "工程名称超过 16 KiB"); info.origin = meta.at("origin").get<BlockPos>();
        const auto& random = meta.at("randomSource"); require(random.at("algorithm") == "javaLegacy48", "不支持的随机源");
        data = {{"format", "verimc.simulator"}, {"formatVersion", 1}, {"edition", "java"}, {"minecraftVersion", "26.2"}, {"name", info.name}, {"kind", checkpoint ? "checkpoint" : "circuit"}, {"profile", meta.at("profile")}, {"nextEntityOrder", unsignedValue(meta.at("nextEntityOrder"))}, {"randomSource", {{"algorithm", "javaLegacy48"}, {"seed", std::to_string(unsignedValue(random.at("seed")))}}}};
        data["chunkStates"] = meta.at("chunkStates");
        require(meta.contains("snapshot") == checkpoint, "快照元数据与文件类型不符");
        if (checkpoint) {
            const auto& snapshot = meta.at("snapshot"); require(snapshot.at("checkpointAbi") == checkpointAbi && snapshot.at("engineBuild").is_string(), "快照执行版本不兼容，需要显式迁移");
            for (const auto* field : {"tick", "nextOrder", "sequence", "nextProbeId", "nextActionId", "actionsDropped", "traceDropped"}) data[field] = unsignedValue(snapshot.at(field));
            require(unsignedValue(data.at("nextProbeId"), UINT32_MAX) > 0, "无效探针编号计数");
            require(snapshot.at("faulted").is_boolean(), "无效快照中止标志"); data["faulted"] = snapshot.at("faulted");
            data["randomSource"]["state"] = unsignedValue(snapshot.at("randomState"), (1ULL << 48) - 1); data["randomSource"]["draws"] = std::to_string(unsignedValue(snapshot.at("randomDraws")));
            data["blockTickState"] = {{"earliestCollection", unsignedValue(snapshot.at("blockTickEarliestCollection"))}}; data["torchToggles"] = Json::array();
            data["loadSettings"] = Json::object();
            for (const auto* field : {"traceCapacity", "traceAtomicReserve", "updateBudget"}) data["loadSettings"][field] = unsignedValue(snapshot.at(field), 2000000);
            require(data["loadSettings"]["traceCapacity"].get<std::uint64_t>() + data["loadSettings"]["traceAtomicReserve"].get<std::uint64_t>() <= 2000000 && data["loadSettings"]["updateBudget"] > 0, "运行预算越界");
        }
    }
    void mapVibration(Json& value) {
        require(!value.contains("affectedBlock"), "二进制振动记录使用了旧状态对象");
        if (value.contains("affectedPaletteId")) { value["affectedBlock"] = describe(registry, palette.at(static_cast<std::size_t>(unsignedValue(value.at("affectedPaletteId"), palette.size() - 1)))); value.erase("affectedPaletteId"); }
    }
    Json decodeRows(const Entry& entry) {
        auto bytes = payload(entry);
        if (entry.tag == "ORDR" || entry.tag == "TRCE") {
            Reader reader(bytes); const auto count = reader.integer(4); const std::size_t width = entry.tag == "ORDR" ? 20 : 25;
            require(count <= 2000000 && count * width == reader.remaining(), "固定记录表长度不符"); Json rows = Json::array();
            for (std::uint64_t i = 0; i < count; ++i) {
                if (entry.tag == "ORDR") { BlockPos pos{signed32(reader.integer(4)), signed32(reader.integer(4)), signed32(reader.integer(4))}; (void)Json(pos).get<BlockPos>(); rows.push_back({{"pos", pos}, {"order", reader.integer(8)}}); }
                else { const auto probe = reader.integer(8), tick = reader.integer(8), sequence = reader.integer(8), value = reader.integer(1); require(probe > 0 && probe <= UINT32_MAX && value <= 15, "无效探针历史记录"); rows.push_back(Json::array({probe, tick, sequence, value})); }
            }
            return rows;
        }
        auto value = decodeCbor(bytes);
        if (entry.tag == "RUNT") { require(value.is_object() && value.size() == 2 && value.at("table") == entry.table, "运行表名不符"); value = std::move(value["rows"]); }
        require(value.is_array(), "数据表不是数组"); return value;
    }
    void normalizeRow(const std::string& table, const Entry& entry, Json& row) {
        if (table == "trace") return;
        require(row.is_object(), "表记录不是对象");
        if (table == "blockData") {
            require(!row.contains("pos"), "器件记录不能重复存储绝对坐标"); const auto local = unsignedValue(row.at("localIndex"), 4095); row.erase("localIndex"); row["pos"] = absolutePos(entry.section, static_cast<unsigned>(local));
        }
        if (table == "events" || table == "blockTickBatch") {
            require(!row.contains("type"), "事件包含本机内部类型编号"); auto name = row.at("blockName").get<std::string>(); require(name.starts_with("minecraft:"), "事件方块名缺少命名空间"); row["type"] = registry[registry.state(name)].type; row.erase("blockName");
            for (const auto* field : {"tick", "order", "data", "entityOrder"}) (void)unsignedValue(row.at(field));
            (void)unsignedValue(row.at("phase"), 3); const auto& priority = row.at("priority"); require(priority.is_number_integer() && priority >= -3 && priority <= 3, "事件优先级越界");
        }
        if (table == "motions") row["movedState"] = palette.at(static_cast<std::size_t>(unsignedValue(row.at("movedState"), palette.size() - 1)));
        if (table == "sensors") for (const auto* field : {"candidate", "current"}) if (row.contains(field)) mapVibration(row[field]);
        if (row.contains("pos")) (void)row.at("pos").get<BlockPos>();
        if (table == "probes") {
            require(unsignedValue(row.at("id"), UINT32_MAX) > 0, "无效探针 id");
            require(row.at("triggerValue").is_number_integer() && row.at("triggerValue") >= 0 && row.at("triggerValue") <= 15, "无效探针触发值");
            if (checkpoint) require(row.at("lastValue").is_number_integer() && row.at("lastValue") >= -1 && row.at("lastValue") <= 15 && row.at("id") < data.at("nextProbeId"), "无效探针快照值");
        }
        if (table == "blockData" && checkpoint) require(row.at("output").is_number_integer() && row.at("output") >= 0 && row.at("output") <= 15, "无效器件输出");
    }
public:
    ProjectInfo info;
    VmcbSource(std::istream& stream, const BlockRegistry& blocks, const ProjectFileOptions& limits) : input(stream), registry(blocks), options(limits) {
        require(static_cast<bool>(decompressor), "无法创建解压器"); zstdCheck(ZSTD_DCtx_setParameter(decompressor.get(), ZSTD_d_windowLogMax, 23));
        const auto length = streamLength(input); require(length >= 64 && length <= options.maxFileBytes, "文件大小越界"); auto header = readBytes(input, 0, 64); Reader h(header); require(std::equal(magic.begin(), magic.end(), h.take(8).begin()), "文件标识错误");
        require(h.integer(2) == 1, "不支持的格式主版本"); require(h.integer(2) == 0, "不支持的格式次版本"); require(h.integer(2) == 64 && h.integer(2) == 64, "文件头或目录项长度错误");
        const auto flags = h.integer(4); require(flags <= 1, "未知文件标志"); checkpoint = flags == 1; const auto count = h.integer(4), directoryOffset = h.integer(8), directorySize = h.integer(8), fileSize = h.integer(8); declaredBlocks = h.integer(8); const auto directoryCrc = h.integer(4), headerCrc = h.integer(4);
        require(crc(std::span(header).first(60)) == headerCrc, "文件头 CRC 校验失败");
        require(count <= 2000000 && count * 64 == directorySize && fileSize == length && directoryOffset >= 64 && directoryOffset <= length && directorySize == length - directoryOffset, "目录边界或文件长度错误");
        require(declaredBlocks <= options.maxBlocks && count * 256 <= options.maxMemoryBytes, "工程数量超过读取预算");
        const auto directory = readBytes(input, directoryOffset, static_cast<std::size_t>(directorySize)); require(crc(directory) == directoryCrc, "目录 CRC 校验失败"); entries.reserve(static_cast<std::size_t>(count));
        std::uint64_t rawTotal = 0, sections = 0; std::map<std::string, std::size_t> kinds;
        const std::set<std::string> known{"META", "PALT", "BLKS", "BDAT", "ORDR", "PROB", "RUNT", "TRCE"};
        for (std::size_t i = 0; i < count; ++i) {
            check(); auto entry = parseEntry(std::span(directory).subspan(i * 64, 64));
            require(known.contains(entry.tag), "尚不能保留未知扩展段 " + entry.tag + "，已停止导入以防丢失数据");
            require(entry.schemaMajor == 1 && entry.schemaMinor == 0 && entry.flags == 1 && entry.codec <= 1, "段版本、标志或压缩方式不支持");
            require(entries.empty() || entries.back().key() < entry.key(), "目录键未排序或重复");
            require(entry.part == (!entries.empty() && entries.back().tag == entry.tag && entries.back().section == entry.section ? entries.back().part + 1 : 0), "分片号不连续");
            require(entry.raw > 0 && entry.raw <= maxPageBytes && entry.stored > 0 && entry.stored <= maxPageBytes && entry.offset >= 64 && entry.offset <= directoryOffset && entry.stored <= directoryOffset - entry.offset, "数据段长度或位置越界");
            if (entry.codec == 0) require(entry.raw == entry.stored, "原样数据段长度不符");
            if (entry.tag != "BLKS" && entry.tag != "BDAT") require(entry.section == BlockPos{}, "全局段包含分区坐标");
            if (entry.tag == "META" || entry.tag == "PALT" || entry.tag == "BLKS") require(entry.part == 0, "不允许拆分此段");
            if (entry.tag == "BLKS") ++sections;
            if (!checkpoint) require(entry.tag != "RUNT" && entry.tag != "TRCE", "电路包含快照专用段");
            rawTotal += entry.raw; ++kinds[entry.tag]; entries.push_back(std::move(entry));
        }
        require(rawTotal <= 4ULL * 1024 * 1024 * 1024, "解压总量超出预算");
        // Conservative allowance for the candidate, initialization's stable
        // World copy, directory indices, native records, and one CBOR page.
        require(sections * 33000 + count * 256 + rawTotal * 16 + 256ULL * 1024 * 1024 <= options.maxMemoryBytes, "候选世界及解码数据超出内存预算");
        for (const auto* kind : {"META", "PALT"}) require(kinds[kind] == 1, std::string("缺少或重复 ") + kind);
        for (const auto* kind : {"ORDR", "PROB"}) require(kinds[kind] > 0, std::string("缺少 ") + kind);
        if (checkpoint) require(kinds["RUNT"] > 0 && kinds["TRCE"] > 0, "快照缺少运行表或波形");
        require((sections == 0) == (declaredBlocks == 0) && sections <= declaredBlocks, "分区数与方块数矛盾");
        std::vector<const Entry*> physical; physical.reserve(entries.size()); for (const auto& entry : entries) physical.push_back(&entry);
        std::sort(physical.begin(), physical.end(), [](const Entry* a, const Entry* b) { return a->offset < b->offset; });
        std::uint64_t next = 64; for (const auto* entry : physical) { require(entry->offset == next, "数据段重叠或有间隙"); next += entry->stored; } require(next == directoryOffset, "数据段没有覆盖载荷区");
        // Validate every page before building the candidate. Runtime table
        // names become a small index, not a retained project-sized DOM.
        std::string lastRuntime; std::map<std::string, std::uint64_t> tableCounts;
        for (std::size_t i = 0; i < entries.size(); ++i) {
            auto& entry = entries[i]; auto bytes = payload(entry);
            if (entry.tag == "META") readMetadata(decodeCbor(bytes));
            else if (entry.tag == "PALT") readPalette(bytes);
            else if (entry.tag == "BLKS") tables["blocks"].push_back(i);
            else {
                std::string name; std::uint64_t rows = 0;
                if (entry.tag == "ORDR" || entry.tag == "TRCE") {
                    name = entry.tag == "ORDR" ? "entityOrder" : "trace"; Reader reader(bytes); rows = reader.integer(4); require(rows * (entry.tag == "ORDR" ? 20 : 25) == reader.remaining(), "固定表长度不符");
                } else {
                    auto decoded = decodeCbor(bytes);
                    if (entry.tag == "RUNT") {
                        require(decoded.is_object() && decoded.size() == 2 && decoded.contains("table") && decoded.contains("rows"), "无效运行分片"); name = decoded.at("table").get<std::string>();
                        require(std::find(runtimeTables.begin(), runtimeTables.end(), name) != runtimeTables.end() && (lastRuntime.empty() || name >= lastRuntime), "未知或未排序的运行表"); lastRuntime = name; entry.table = name; decoded = std::move(decoded["rows"]);
                    } else name = entry.tag == "BDAT" ? "blockData" : "probes";
                    require(decoded.is_array(), "表记录必须为数组"); rows = decoded.size();
                }
                tableCounts[name] += rows; require(tableCounts[name] <= 2000000, "表记录超过数量预算"); tables[name].push_back(i);
            }
        }
        if (checkpoint) for (const auto* name : runtimeTables) require(tables.contains(name), std::string("缺少快照表 ") + name);
        require(tableCounts["events"] + tableCounts["blockTickBatch"] <= 2000000, "计划事件和批次超过总预算");
        require(tableCounts["blockData"] <= declaredBlocks && tableCounts["entityOrder"] <= declaredBlocks, "器件记录数超过方块数量");
        if (checkpoint) require(tableCounts["trace"] <= unsignedValue(data.at("loadSettings").at("traceCapacity")) + unsignedValue(data.at("loadSettings").at("traceAtomicReserve")), "波形超过保存的容量");
    }
    const Json& metadata() const override { return data; }
    bool restoreBudgets() const override { return true; }
    void check() const override { report(options, "校验工程"); }
    void loadWorld(World& world) override {
        std::uint64_t blocks = 0, done = 0;
        for (auto index : tables["blocks"]) {
            const auto& entry = entries[index]; report(options, "构建方块分区", done++, tables["blocks"].size()); const auto states = decodeSection(payload(entry), palette.size());
            for (unsigned local = 0; local < 4096; ++local) if (auto state = states[local]) { world.set(absolutePos(entry.section, local), palette.at(state)); ++blocks; }
            require(blocks <= declaredBlocks, "方块数超过文件声明");
        }
        require(blocks == declaredBlocks && world.size() == blocks, "文件总方块数不符");
    }
    ProjectRows rows(const std::string& name) override {
        struct Cursor { std::size_t page{}, row{}; Json rows; const Entry* entry{}; std::optional<BlockPos> previous; std::optional<std::pair<BlockPos, unsigned>> spatial; std::optional<std::uint64_t> order; std::set<std::uint64_t> ids; std::optional<std::pair<std::uint64_t, std::uint64_t>> traceKey; };
        auto cursor = std::make_shared<Cursor>();
        return ProjectRows([this, name, cursor](Json& value) {
            check(); const auto& indices = tables[name];
            while (cursor->row >= cursor->rows.size()) {
                if (cursor->page == indices.size()) return false;
                cursor->entry = &entries[indices[cursor->page++]]; cursor->rows = decodeRows(*cursor->entry); cursor->row = 0;
            }
            value = std::move(cursor->rows[cursor->row++]); normalizeRow(name, *cursor->entry, value);
            if (name == "blockData") {
                const auto pos = value.at("pos").get<BlockPos>(); const auto key = std::pair(sectionOf(pos), localIndex(pos)); require(!cursor->spatial || *cursor->spatial < key, "重复或未排序的器件记录"); cursor->spatial = key;
            } else if (name == "hoppers" || name == "sensors" || name == "jukeboxes" || name == "motions") {
                const auto pos = value.at("pos").get<BlockPos>(); require(!cursor->previous || *cursor->previous < pos, "重复或未排序的运行器件"); cursor->previous = pos;
            } else if (name == "entityOrder") {
                const auto order = unsignedValue(value.at("order")); require(!cursor->order || order > *cursor->order, "实体执行顺序未排序"); cursor->order = order;
            } else if (name == "probes") require(cursor->ids.insert(unsignedValue(value.at("id"))).second, "重复探针 id");
            else if (name == "trace") {
                const auto key = std::pair(unsignedValue(value.at(1)), unsignedValue(value.at(2))); require(!cursor->traceKey || *cursor->traceKey <= key, "波形时序倒退"); require(value.at(1) <= data.at("tick") && value.at(2) <= data.at("sequence"), "波形包含未来记录"); cursor->traceKey = key;
            }
            return true;
        });
    }
};
}
void writeVmcb(std::ostream& output, const Simulator& sim, const ProjectInfo& info, bool checkpoint, const ProjectFileOptions& options) {
    VmcbSink sink(output, sim.registry, info, options); sim.writeProject(sink, info.name, checkpoint);
}
ProjectInfo readProjectFile(std::istream& input, Simulator& sim, const ProjectFileOptions& options) {
    report(options, "识别文件"); const auto length = streamLength(input); require(length > 0 && length <= options.maxFileBytes, "工程文件大小超过预算或为空"); auto prefix = readBytes(input, 0, static_cast<std::size_t>(std::min<std::uint64_t>(length, 8)));
    if (prefix.size() == magic.size() && std::equal(magic.begin(), magic.end(), prefix.begin())) {
        VmcbSource source(input, sim.registry, options); sim.loadProject(source); return source.info;
    }
    if (prefix.size() >= 4 && std::equal(magic.begin(), magic.begin() + 4, prefix.begin())) throw std::invalid_argument("VMCB 文件头损坏");
    // Legacy JSON necessarily has a DOM; keep an explicit budget and parse it
    // natively so 64-bit timestamps and integer seeds never pass through JS.
    require(length <= options.maxMemoryBytes / 12, "旧 JSON 超过解析内存预算"); input.clear(); input.seekg(0);
    std::map<int, std::set<std::string>> keys; std::uint64_t events = 0;
    auto callback = [&](int depth, Json::parse_event_t event, Json& value) {
        require(depth <= 32 && ++events <= 30000000, "旧 JSON 结构超过预算"); if ((events & 4095) == 0) report(options, "解析旧 JSON", static_cast<std::uint64_t>(input.tellg()), length);
        if (event == Json::parse_event_t::object_start) keys[depth + 1].clear();
        else if (event == Json::parse_event_t::key) require(keys[depth].insert(value.get<std::string>()).second, "旧 JSON 包含重复键");
        return true;
    };
    auto data = Json::parse(input, callback); ProjectInfo info{data.value("name", std::string("导入电路")), {}}; require(info.name.size() <= 16384, "工程名称超过 16 KiB");
    report(options, "装载旧 JSON"); sim.loadProject(data, [&] { report(options, "装载旧 JSON"); }); return info;
}
}
