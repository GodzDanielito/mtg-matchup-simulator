// Test del generatore casuale PCG32, del metodo di Lemire e di Fisher-Yates.

#include "rng.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <numeric>

using mtg::Pcg32;

// Sequenza di riferimento ufficiale di PCG32 (pcg32-demo di M. O'Neill,
// pcg32_srandom(42, 54)): conferma che l'implementazione è quella standard,
// non solo che è stabile.
TEST_CASE("PCG32 riproduce la sequenza di riferimento ufficiale", "[rng]") {
    Pcg32 rng(42u, 54u);
    const std::array<std::uint32_t, 6> expected{
        0xa15c02b7u, 0x7b47f409u, 0xba1d3330u, 0x83d2f293u, 0xbfa4784bu, 0xcbed606eu,
    };
    for (const auto value : expected) CHECK(rng.next() == value);
}

TEST_CASE("Stesso seed, stessa sequenza", "[rng]") {
    Pcg32 a(2026), b(2026);
    for (int i = 0; i < 1000; ++i) REQUIRE(a.next() == b.next());
}

TEST_CASE("Seed diversi, sequenze diverse", "[rng]") {
    Pcg32 a(1), b(2);
    int equal = 0;
    for (int i = 0; i < 100; ++i) equal += (a.next() == b.next()) ? 1 : 0;
    CHECK(equal < 3);
}

TEST_CASE("Copiare il generatore copia la sequenza", "[rng]") {
    Pcg32 a(99);
    for (int i = 0; i < 10; ++i) (void)a.next();
    Pcg32 b = a;
    for (int i = 0; i < 100; ++i) REQUIRE(a.next() == b.next());
}

TEST_CASE("bounded (Lemire): valori nell'intervallo e distribuzione uniforme", "[rng]") {
    Pcg32 rng(7);
    SECTION("bound 1 dà sempre 0") {
        for (int i = 0; i < 100; ++i) REQUIRE(rng.bounded(1) == 0u);
    }
    SECTION("bound 6: 60.000 lanci, ogni faccia tra 9.400 e 10.600 volte") {
        std::array<int, 6> counts{};
        for (int i = 0; i < 60000; ++i) {
            const auto v = rng.bounded(6);
            REQUIRE(v < 6u);
            ++counts[v];
        }
        for (const int c : counts) {
            CHECK(c > 9400);
            CHECK(c < 10600);
        }
    }
}

// Valori fissati: proteggono da modifiche involontarie all'algoritmo.
// Se cambiano, tutte le partite registrate con un seed diventano irriproducibili.
TEST_CASE("bounded (Lemire): valori fissati", "[rng]") {
    Pcg32 rng(2026);
    const std::array<std::uint32_t, 10> expected{22, 41, 1, 32, 36, 49, 8, 43, 37, 54};
    for (const auto value : expected) CHECK(rng.bounded(60) == value);
}

TEST_CASE("Fisher-Yates: permutazione valida e valori fissati", "[rng]") {
    std::array<int, 10> values{};
    std::iota(values.begin(), values.end(), 0);
    Pcg32 rng(7);
    mtg::shuffle(std::span<int>(values), rng);

    std::array<int, 10> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    for (int i = 0; i < 10; ++i) REQUIRE(sorted[static_cast<std::size_t>(i)] == i);

    const std::array<int, 10> expected{7, 6, 3, 1, 0, 4, 9, 5, 2, 8};
    CHECK(values == expected);
}
