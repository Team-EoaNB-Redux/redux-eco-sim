#pragma once

// Deterministic xoshiro256** seeded with splitmix64. Same seed => same simulation.

#include "pdx/fixed.h"

#include <cstdint>

namespace world {

class Rng {
public:
    explicit Rng(uint64_t seed = 12345) { reseed(seed); }

    void reseed(uint64_t seed) {
        uint64_t x = seed;
        for (auto& s : s_) {
            x += 0x9E3779B97F4A7C15ULL;
            uint64_t z = x;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
            s = z ^ (z >> 31);
        }
    }

    uint64_t next() {
        const uint64_t result = rotl(s_[1] * 5, 7) * 9;
        const uint64_t t = s_[1] << 17;
        s_[2] ^= s_[0];
        s_[3] ^= s_[1];
        s_[1] ^= s_[2];
        s_[0] ^= s_[3];
        s_[2] ^= t;
        s_[3] = rotl(s_[3], 45);
        return result;
    }

    // Uniform integer in [0, n). n must be > 0.
    uint64_t below(uint64_t n) { return n == 0 ? 0 : next() % n; }

    // Uniform fixed-point value in [0, 1) at full 1e-5 resolution (the `random` game variable).
    pdx::Fixed unit() { return pdx::Fixed::from_raw(static_cast<int64_t>(below(pdx::Fixed::SCALE))); }

private:
    static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
    uint64_t s_[4] = {};
};

} // namespace world
