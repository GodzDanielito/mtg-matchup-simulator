#pragma once

// Generatore casuale del motore: PCG32 (M. O'Neill, pcg-random.org).
//
// Perché non std::mt19937 + std::uniform_int_distribution:
//  - mt19937 occupa ~2,5 KB e vive dentro GameState, che si copia di continuo;
//  - le distribuzioni della libreria standard danno numeri diversi con MSVC,
//    GCC e Clang. Qui intervalli (Lemire) e mescolamento (Fisher-Yates) sono
//    scritti a mano, quindi la stessa partita è identica su ogni compilatore.

#include <cstdint>
#include <span>
#include <utility>

namespace mtg {

class Pcg32 {
public:
    // Stream di default per le partite: due generatori con lo stesso seed e
    // stream diversi producono sequenze indipendenti.
    static constexpr std::uint64_t kDefaultStream = 0xda3e39cb94b95bdbULL;

    Pcg32() : Pcg32(0) {}
    explicit Pcg32(std::uint64_t seed, std::uint64_t stream = kDefaultStream);

    // 32 bit uniformi.
    std::uint32_t next();

    // Intero uniforme in [0, bound), senza distorsione (metodo di Lemire).
    // Richiede bound > 0.
    std::uint32_t bounded(std::uint32_t bound);

private:
    std::uint64_t state_ = 0;
    std::uint64_t inc_ = 0;
};

// Mescolamento di Fisher-Yates: ogni permutazione ha la stessa probabilità.
template <typename T>
void shuffle(std::span<T> items, Pcg32& rng) {
    for (std::size_t i = items.size(); i > 1; --i) {
        const std::size_t j = rng.bounded(static_cast<std::uint32_t>(i));
        using std::swap;
        swap(items[i - 1], items[j]);
    }
}

}  // namespace mtg
