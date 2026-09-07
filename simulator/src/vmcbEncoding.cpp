#include "simulator/vmcbEncoding.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <openssl/evp.h>
#include <memory>
#include <set>

namespace simulator::vmcb {
namespace {
[[noreturn]] void invalid(const char* message) { throw std::invalid_argument(std::string("VMCB：") + message); }
void require(bool condition, const char* message) { if (!condition) invalid(message); }
void bigInteger(Bytes& bytes, std::uint64_t value, unsigned width) {
    for (unsigned i = width; i > 0; --i) bytes.push_back(static_cast<std::uint8_t>(value >> ((i - 1) * 8)));
}
void cborHead(Bytes& bytes, unsigned major, std::uint64_t value) {
    const auto tag = static_cast<std::uint8_t>(major << 5);
    if (value < 24) bytes.push_back(static_cast<std::uint8_t>(tag | value));
    else { unsigned width = value <= 255 ? 1 : value <= 65535 ? 2 : value <= UINT32_MAX ? 4 : 8;
        bytes.push_back(static_cast<std::uint8_t>(tag | (width == 1 ? 24 : width == 2 ? 25 : width == 4 ? 26 : 27))); bigInteger(bytes, value, width); }
}
double halfValue(std::uint16_t bits) {
    const auto exponent = (bits >> 10) & 31, fraction = bits & 1023;
    double value = exponent == 31 ? (fraction ? std::numeric_limits<double>::quiet_NaN() : std::numeric_limits<double>::infinity())
        : std::ldexp(exponent ? 1024.0 + fraction : static_cast<double>(fraction), exponent ? static_cast<int>(exponent) - 25 : -24);
    return bits & 0x8000 ? -value : value;
}
std::optional<std::uint16_t> exactHalf(double value) {
    if (std::abs(value) > std::numeric_limits<float>::max()) return {};
    const auto single = static_cast<float>(value);
    if (static_cast<double>(single) != value) return {};
    const auto bits = std::bit_cast<std::uint32_t>(single);
    const auto sign = static_cast<std::uint16_t>((bits >> 16) & 0x8000);
    if (single == 0) return sign;
    const int exponent = static_cast<int>((bits >> 23) & 255) - 127;
    const auto mantissa = (bits & 0x7fffff) | 0x800000;
    if (exponent > 15 || exponent < -24) return {};
    const unsigned shift = exponent >= -14 ? 13u : static_cast<unsigned>(-exponent - 1);
    if (mantissa & ((1u << shift) - 1)) return {};
    const auto encoded = static_cast<std::uint16_t>(sign | (exponent >= -14 ? static_cast<unsigned>(exponent + 15) << 10 : 0) | ((mantissa >> shift) & (exponent >= -14 ? 1023u : 2047u)));
    return halfValue(encoded) == value ? std::optional(encoded) : std::nullopt;
}
void encodeValue(Bytes& bytes, const Json& value, unsigned depth, std::size_t& nodes) {
    require(depth <= 32 && ++nodes <= 1000000, "CBOR 结构超过预算");
    if (value.is_null()) bytes.push_back(0xf6);
    else if (value.is_boolean()) bytes.push_back(value.get<bool>() ? 0xf5 : 0xf4);
    else if (value.is_number_unsigned()) cborHead(bytes, 0, value.get<std::uint64_t>());
    else if (value.is_number_integer()) { auto v = value.get<std::int64_t>(); cborHead(bytes, v < 0 ? 1 : 0, v < 0 ? static_cast<std::uint64_t>(-(v + 1)) : static_cast<std::uint64_t>(v)); }
    else if (value.is_number_float()) {
        const auto v = value.get<double>(); require(std::isfinite(v), "非有限浮点值");
        if (auto half = exactHalf(v)) { bytes.push_back(0xf9); bigInteger(bytes, *half, 2); }
        else if (std::abs(v) <= std::numeric_limits<float>::max() && static_cast<double>(static_cast<float>(v)) == v) { bytes.push_back(0xfa); bigInteger(bytes, std::bit_cast<std::uint32_t>(static_cast<float>(v)), 4); }
        else { bytes.push_back(0xfb); bigInteger(bytes, std::bit_cast<std::uint64_t>(v), 8); }
    } else if (value.is_string()) {
        const auto& text = value.get_ref<const std::string&>(); require(text.size() <= 65536, "字符串超过 64 KiB"); validUtf8(text);
        cborHead(bytes, 3, text.size()); bytes.insert(bytes.end(), text.begin(), text.end());
    } else if (value.is_binary()) {
        const auto& data = value.get_binary(); cborHead(bytes, 2, data.size()); bytes.insert(bytes.end(), data.begin(), data.end());
    } else if (value.is_array()) {
        cborHead(bytes, 4, value.size()); for (const auto& row : value) encodeValue(bytes, row, depth + 1, nodes);
    } else if (value.is_object()) {
        std::vector<std::pair<Bytes, const Json*>> entries; entries.reserve(value.size());
        for (auto it = value.begin(); it != value.end(); ++it) { Bytes key; encodeValue(key, Json(it.key()), depth + 1, nodes); entries.emplace_back(std::move(key), &it.value()); }
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.first < b.first; }); cborHead(bytes, 5, value.size());
        for (const auto& [key, row] : entries) { bytes.insert(bytes.end(), key.begin(), key.end()); encodeValue(bytes, *row, depth + 1, nodes); }
    } else invalid("不支持的 CBOR 值类型");
    require(bytes.size() <= maxPageBytes, "数据段超过 8 MiB");
}
class CborReader {
    Reader input;
    std::size_t nodes{};
    std::uint64_t big(unsigned width) { std::uint64_t result = 0; for (auto byte : input.take(width)) result = (result << 8) | byte; return result; }
public:
    explicit CborReader(std::span<const std::uint8_t> data) : input(data) {}
    Json value(unsigned depth = 0) {
        require(depth <= 32 && ++nodes <= 1000000, "CBOR 结构超过预算");
        const auto head = static_cast<unsigned>(input.integer(1)); const unsigned major = head >> 5, info = head & 31;
        if (major == 7) {
            if (info == 20 || info == 21) return info == 21;
            if (info == 22) return nullptr;
            double number;
            if (info == 25) number = halfValue(static_cast<std::uint16_t>(big(2)));
            else if (info == 26) number = std::bit_cast<float>(static_cast<std::uint32_t>(big(4)));
            else if (info == 27) number = std::bit_cast<double>(big(8));
            else invalid("不支持的 CBOR 简单值");
            require(std::isfinite(number), "非有限浮点值"); return number;
        }
        require(major <= 5 && info <= 27, "不支持 tag 或不定长 CBOR");
        const auto length = info < 24 ? info : big(1u << (info - 24));
        if (major == 0) return length;
        if (major == 1) { require(length <= INT64_MAX, "负整数超出 i64"); return -static_cast<std::int64_t>(length) - 1; }
        require(length <= input.remaining() && length <= maxPageBytes, "CBOR 长度越界");
        if (major == 2) { const auto data = input.take(static_cast<std::size_t>(length)); return Json::binary(Bytes(data.begin(), data.end())); }
        if (major == 3) {
            require(length <= 65536, "字符串超过 64 KiB"); const auto data = input.take(static_cast<std::size_t>(length));
            std::string text(data.begin(), data.end()); validUtf8(text); return text;
        }
        require(length <= 1000000 - nodes, "CBOR 元素数量超过预算");
        Json result = major == 4 ? Json::array() : Json::object();
        for (std::uint64_t i = 0; i < length; ++i) {
            if (major == 4) result.push_back(value(depth + 1));
            else {
                auto key = value(depth + 1); require(key.is_string(), "CBOR map 键必须为字符串");
                auto name = key.get<std::string>(); require(!result.contains(name), "重复 CBOR map 键"); result[name] = value(depth + 1);
            }
        }
        return result;
    }
    void finish() const { input.finish(); }
};
unsigned widthFor(std::size_t count) { return count <= 1 ? 0u : static_cast<unsigned>(std::bit_width(static_cast<unsigned>(count - 1))); }
Bytes packed(std::span<const StateId> values, const std::vector<StateId>& palette) {
    const auto width = widthFor(palette.size()); Bytes bytes((values.size() * width + 7) / 8, 0);
    for (std::size_t j = 0; j < values.size(); ++j) {
        const auto index = static_cast<unsigned>(std::lower_bound(palette.begin(), palette.end(), values[j]) - palette.begin());
        for (unsigned bit = 0; bit < width; ++bit) if (index & (1u << bit)) bytes[(j * width + bit) / 8] |= static_cast<std::uint8_t>(1u << ((j * width + bit) % 8));
    }
    return bytes;
}
Bytes sectionHeader(unsigned mode, unsigned count, const std::vector<StateId>& palette) {
    Bytes bytes; integer(bytes, mode, 1); integer(bytes, widthFor(palette.size()), 1); integer(bytes, count, 2); integer(bytes, palette.size(), 2); integer(bytes, 0, 2);
    for (auto id : palette) varInt(bytes, id); return bytes;
}
}
std::span<const std::uint8_t> Reader::take(std::size_t count) {
    require(count <= remaining(), "数据截断"); auto data = bytes.subspan(offset, count); offset += count; return data;
}
std::uint64_t Reader::integer(unsigned width) {
    require(width <= 8, "整数宽度无效"); std::uint64_t result = 0; unsigned shift = 0;
    for (auto byte : take(width)) { result |= static_cast<std::uint64_t>(byte) << shift; shift += 8; } return result;
}
std::uint32_t Reader::varInt() {
    std::uint32_t result = 0;
    for (unsigned shift = 0; shift < 35; shift += 7) {
        const auto byte = static_cast<unsigned>(integer(1)); require(shift < 28 || byte <= 15, "varU32 溢出"); result |= (byte & 127) << shift;
        if (!(byte & 128)) { require(shift == 0 || byte != 0, "非最短 varU32"); return result; }
    }
    invalid("varU32 过长");
}
std::string Reader::string() { const auto length = varInt(); require(length <= 65536, "字符串超过 64 KiB"); auto data = take(length); std::string result(data.begin(), data.end()); validUtf8(result); return result; }
void Reader::finish() const { require(remaining() == 0, "存在尾随数据"); }
void integer(Bytes& bytes, std::uint64_t value, unsigned width) { for (unsigned i = 0; i < width; ++i) bytes.push_back(static_cast<std::uint8_t>(value >> (i * 8))); }
void varInt(Bytes& bytes, std::uint32_t value) { do { auto byte = static_cast<std::uint8_t>(value & 127); value >>= 7; bytes.push_back(static_cast<std::uint8_t>(byte | (value ? 128 : 0))); } while (value); }
void string(Bytes& bytes, const std::string& value) { require(value.size() <= 65536, "字符串超过 64 KiB"); validUtf8(value); varInt(bytes, static_cast<std::uint32_t>(value.size())); bytes.insert(bytes.end(), value.begin(), value.end()); }
void validUtf8(const std::string& value) { (void)Json(value).dump(-1, ' ', false, Json::error_handler_t::strict); }
std::uint64_t unsignedValue(const Json& value, std::uint64_t maximum) {
    require(value.is_number_integer() && (value.is_number_unsigned() || value.get<std::int64_t>() >= 0), "要求无损非负整数");
    auto result = value.get<std::uint64_t>(); require(result <= maximum, "整数越界"); return result;
}
std::uint32_t crc(std::span<const std::uint8_t> bytes) {
    static const auto table = [] { std::array<std::uint32_t, 256> result{}; for (unsigned i = 0; i < 256; ++i) { auto value = i; for (int bit = 0; bit < 8; ++bit) value = (value >> 1) ^ ((value & 1) ? 0xedb88320u : 0); result[i] = value; } return result; }();
    std::uint32_t value = UINT32_MAX; for (auto byte : bytes) value = table[(value ^ byte) & 255] ^ (value >> 8); return value ^ UINT32_MAX;
}
Bytes encodeCbor(const Json& value) { Bytes bytes; std::size_t nodes = 0; encodeValue(bytes, value, 0, nodes); return bytes; }
Json decodeCbor(std::span<const std::uint8_t> bytes) {
    require(bytes.size() <= maxPageBytes, "数据段超过 8 MiB"); CborReader reader(bytes); auto value = reader.value(); reader.finish();
    auto canonical = encodeCbor(value); require(std::equal(bytes.begin(), bytes.end(), canonical.begin(), canonical.end()), "CBOR 不是确定性编码"); return value;
}
Bytes encodeSection(std::span<const StateId, 4096> states) {
    std::vector<StateId> palette(states.begin(), states.end()); std::sort(palette.begin(), palette.end()); palette.erase(std::unique(palette.begin(), palette.end()), palette.end());
    const auto count = static_cast<unsigned>(std::count_if(states.begin(), states.end(), [](auto id) { return id != 0; })); require(count > 0, "禁止全空 BLKS");
    if (palette.size() == 1) return sectionHeader(0, count, palette);
    auto best = sectionHeader(1, count, palette); auto bits = packed(states, palette); best.insert(best.end(), bits.begin(), bits.end());
    auto choose = [&](Bytes candidate) { if (candidate.size() < best.size()) best = std::move(candidate); };
    std::vector<StateId> nonAirPalette = palette; if (nonAirPalette.front() == 0) nonAirPalette.erase(nonAirPalette.begin());
    auto sparse = sectionHeader(2, count, nonAirPalette); std::vector<StateId> values; values.reserve(count); int previous = -1;
    for (int i = 0; i < 4096; ++i) if (states[static_cast<std::size_t>(i)]) { varInt(sparse, static_cast<std::uint32_t>(i - previous - 1)); previous = i; values.push_back(states[static_cast<std::size_t>(i)]); }
    bits = packed(values, nonAirPalette); sparse.insert(sparse.end(), bits.begin(), bits.end()); choose(std::move(sparse));
    auto runs = sectionHeader(3, count, palette); std::vector<std::uint32_t> lengths; values.clear();
    for (auto state : states) { if (values.empty() || state != values.back()) { values.push_back(state); lengths.push_back(1); } else ++lengths.back(); }
    varInt(runs, static_cast<std::uint32_t>(values.size())); for (auto length : lengths) varInt(runs, length - 1);
    bits = packed(values, palette); runs.insert(runs.end(), bits.begin(), bits.end()); choose(std::move(runs)); return best;
}
std::array<StateId, 4096> decodeSection(std::span<const std::uint8_t> bytes, std::size_t paletteSize) {
    Reader reader(bytes); const auto mode = reader.integer(1), bits = reader.integer(1), count = reader.integer(2), size = reader.integer(2);
    require(reader.integer(2) == 0 && mode <= 3 && count > 0 && count <= 4096 && size > 0 && size <= 4096 && bits == widthFor(size), "无效分区头");
    std::vector<StateId> palette; for (std::size_t i = 0; i < size; ++i) { auto state = reader.varInt(); require(state < paletteSize && (palette.empty() || state > palette.back()), "无效局部状态表"); palette.push_back(state); }
    std::array<StateId, 4096> result{}; std::vector<unsigned> indices, lengths;
    std::size_t valueCount = 4096;
    if (mode == 0) { require(count == 4096 && size == 1 && palette[0] != 0, "无效填充分区"); result.fill(palette[0]); reader.finish(); return result; }
    if (mode == 2) {
        require(palette[0] != 0, "稀疏状态表包含空气"); std::uint64_t next = 0;
        for (std::size_t i = 0; i < count; ++i) { next += reader.varInt(); require(next < 4096, "稀疏位置越界"); indices.push_back(static_cast<unsigned>(next++)); } valueCount = count;
    } else if (mode == 3) {
        valueCount = reader.varInt(); require(valueCount > 0 && valueCount <= 4096, "无效游程数量"); std::uint64_t total = 0;
        for (std::size_t i = 0; i < valueCount; ++i) { const auto length = static_cast<std::uint64_t>(reader.varInt()) + 1; total += length; require(total <= 4096, "游程长度越界"); lengths.push_back(static_cast<unsigned>(length)); }
        require(total == 4096, "游程没有覆盖完整分区");
    }
    const auto bitCount = valueCount * bits; const auto packedBytes = reader.take((bitCount + 7) / 8);
    if (bitCount % 8) require((packedBytes.back() >> (bitCount % 8)) == 0, "位流填充非零");
    std::set<StateId> used; std::size_t cursor = 0; StateId last = UINT32_MAX;
    for (std::size_t j = 0; j < valueCount; ++j) {
        unsigned index = 0; for (unsigned bit = 0; bit < bits; ++bit) index |= ((packedBytes[(j * bits + bit) / 8] >> ((j * bits + bit) % 8)) & 1u) << bit;
        require(index < palette.size(), "位流状态编号越界"); const auto state = palette[index]; used.insert(state);
        if (mode == 1) result[j] = state;
        else if (mode == 2) result[indices[j]] = state;
        else { require(j == 0 || last != state, "相邻游程未合并"); for (unsigned i = 0; i < lengths[j]; ++i) result[cursor++] = state; last = state; }
    }
    require(used.size() == palette.size(), "局部状态表含未使用条目");
    require(static_cast<std::uint64_t>(std::count_if(result.begin(), result.end(), [](auto id) { return id != 0; })) == count, "非空气数量不符"); reader.finish(); return result;
}
Bytes rulesDigest(const std::string& blockStatesPath) {
    auto digest = [](std::span<const std::uint8_t> bytes) { Bytes result(32); unsigned size = 0; require(EVP_Digest(bytes.data(), bytes.size(), result.data(), &size, EVP_sha256(), nullptr) == 1 && size == 32, "无法计算规则指纹"); return result; };
    Bytes manifest;
    for (const auto* name : {"blockStates.json", "compostingRules.json", "itemDefinitions.json", "jukeboxRules.json", "noteRules.json", "referenceVersion.json", "vibrationRules.json"}) {
        auto path = std::string(name) == "blockStates.json" ? std::filesystem::path(blockStatesPath) : std::filesystem::path(blockStatesPath).parent_path() / name;
        std::ifstream input(path, std::ios::binary); require(static_cast<bool>(input), "无法读取规则文件");
        Bytes bytes((std::istreambuf_iterator<char>(input)), {}); string(manifest, name); auto hash = digest(bytes); manifest.insert(manifest.end(), hash.begin(), hash.end());
    }
    return digest(manifest);
}
}
