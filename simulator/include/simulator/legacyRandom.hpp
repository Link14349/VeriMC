#pragma once
#include <bit>
#include <cstdint>
#include <stdexcept>

namespace simulator {
// Java 26.2 LegacyRandomSource / BitRandomSource primitives. The world has a
// single owner, so the Java concurrent-use detector needs no atomic counterpart.
class LegacyRandom {
public:
    explicit LegacyRandom(std::uint64_t seed = 0) { setSeed(seed); }
    void setSeed(std::uint64_t seed) { stateValue = (seed ^ multiplier) & mask; draws = 0; }
    std::uint64_t state() const { return stateValue; }
    std::uint64_t drawCount() const { return draws; }
    void restore(std::uint64_t state, std::uint64_t drawCount) {
        if (state > mask) throw std::invalid_argument("随机源内部状态必须为 48 位无符号整数");
        stateValue = state; draws = drawCount;
    }
    std::int32_t nextInt() { return std::bit_cast<std::int32_t>(nextBits(32)); }
    std::int32_t nextInt(std::int32_t bound) {
        if (bound <= 0) throw std::invalid_argument("随机数上界必须为正整数");
        const auto range = static_cast<std::uint32_t>(bound);
        if ((range & (range - 1)) == 0) return static_cast<std::int32_t>((static_cast<std::uint64_t>(range) * nextBits(31)) >> 31);
        std::uint32_t bits, value;
        do { bits = nextBits(31); value = bits % range; }
        while (bits - value + (range - 1) >= 0x80000000u);
        return static_cast<std::int32_t>(value);
    }
    std::int64_t nextLong() {
        const auto high = static_cast<std::uint64_t>(static_cast<std::int64_t>(nextInt()));
        const auto low = static_cast<std::uint64_t>(static_cast<std::int64_t>(nextInt()));
        // Java sign-extends the low int before adding; concatenating unsigned
        // halves would produce different results whenever its sign bit is set.
        return std::bit_cast<std::int64_t>((high << 32) + low);
    }
    bool nextBoolean() { return nextBits(1) != 0; }
    float nextFloat() { return static_cast<float>(nextBits(24)) * 0x1p-24F; }
    double nextDouble() {
        const auto high = static_cast<std::uint64_t>(nextBits(26));
        const auto low = nextBits(27);
        return static_cast<double>((high << 27) + low) * 0x1p-53;
    }
    double triangle(double mean, double deviation) {
        const auto first = nextDouble(), second = nextDouble();
        return mean + deviation * (first - second);
    }
    float triangle(float mean, float deviation) {
        const auto first = nextFloat(), second = nextFloat();
        return mean + deviation * (first - second);
    }
private:
    static constexpr std::uint64_t multiplier = 0x5deece66dULL, mask = (1ULL << 48) - 1;
    std::uint64_t stateValue{}, draws{};
    std::uint32_t nextBits(unsigned bits) {
        stateValue = (stateValue * multiplier + 11) & mask; ++draws;
        return static_cast<std::uint32_t>(stateValue >> (48 - bits));
    }
};
}
