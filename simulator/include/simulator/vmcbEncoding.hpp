#pragma once
#include "types.hpp"
#include <span>
#include <vector>
#include <optional>

namespace simulator::vmcb {
using Bytes = std::vector<std::uint8_t>;
inline constexpr std::size_t maxPageBytes = 8 * 1024 * 1024;
class Reader {
    std::span<const std::uint8_t> bytes;
    std::size_t offset{};
public:
    explicit Reader(std::span<const std::uint8_t> data) : bytes(data) {}
    std::uint64_t integer(unsigned width);
    std::uint32_t varInt();
    std::string string();
    std::span<const std::uint8_t> take(std::size_t count);
    std::size_t remaining() const { return bytes.size() - offset; }
    void finish() const;
};
void integer(Bytes& bytes, std::uint64_t value, unsigned width);
void varInt(Bytes& bytes, std::uint32_t value);
void string(Bytes& bytes, const std::string& value);
void validUtf8(const std::string& value);
std::uint64_t unsignedValue(const Json& value, std::uint64_t maximum = UINT64_MAX);
std::uint32_t crc(std::span<const std::uint8_t> bytes);
Bytes encodeCbor(const Json& value);
Json decodeCbor(std::span<const std::uint8_t> bytes);
Bytes encodeSection(std::span<const StateId, 4096> fileStates);
std::array<StateId, 4096> decodeSection(std::span<const std::uint8_t> bytes, std::size_t paletteSize);
Bytes rulesDigest(const std::string& blockStatesPath);
}
