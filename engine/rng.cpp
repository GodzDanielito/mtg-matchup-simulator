#include "rng.hpp"

namespace mtg {

namespace {
constexpr std::uint64_t kMultiplier = 6364136223846793005ULL;
}

// Inizializzazione standard di PCG32 (pcg32_srandom_r): lo stream deve essere dispari.
Pcg32::Pcg32(std::uint64_t seed, std::uint64_t stream) : state_(0), inc_((stream << 1u) | 1u) {
    (void)next();
    state_ += seed;
    (void)next();
}

// PCG-XSH-RR: avanza un LCG a 64 bit e restituisce 32 bit permutati.
std::uint32_t Pcg32::next() {
    const std::uint64_t old = state_;
    state_ = old * kMultiplier + inc_;
    const auto xorshifted = static_cast<std::uint32_t>(((old >> 18u) ^ old) >> 27u);
    const auto rot = static_cast<std::uint32_t>(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
}

// Metodo di Lemire ("Fast Random Integer Generation in an Interval", 2019):
// moltiplicazione a 64 bit invece del modulo, con rifiuto solo nella piccola
// fascia che introdurrebbe distorsione. Quasi sempre un solo numero e nessuna divisione.
std::uint32_t Pcg32::bounded(std::uint32_t bound) {
    std::uint64_t m = static_cast<std::uint64_t>(next()) * bound;
    auto low = static_cast<std::uint32_t>(m);
    if (low < bound) {
        const std::uint32_t threshold = (0u - bound) % bound;  // 2^32 mod bound
        while (low < threshold) {
            m = static_cast<std::uint64_t>(next()) * bound;
            low = static_cast<std::uint32_t>(m);
        }
    }
    return static_cast<std::uint32_t>(m >> 32u);
}

}  // namespace mtg
