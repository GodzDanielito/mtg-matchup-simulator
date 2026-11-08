// Test di prova della fase 0: verifica solo che la toolchain
// (CMake + compilatore C++20 + Catch2) funzioni. Nessuna regola di gioco.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <numeric>

TEST_CASE("La toolchain compila ed esegue i test", "[smoke]") {
    // Una caratteristica di C++20 (inizializzatori designati) per confermare lo standard.
    struct Deck {
        int main_size;
        int sideboard_size;
    };
    constexpr Deck deck{.main_size = 60, .sideboard_size = 15};

    REQUIRE(deck.main_size == 60);
    REQUIRE(deck.sideboard_size == 15);
}

TEST_CASE("Calcolo constexpr in C++20", "[smoke]") {
    constexpr std::array<int, 4> copies{4, 4, 4, 3};
    constexpr int total = std::accumulate(copies.begin(), copies.end(), 0);
    STATIC_REQUIRE(total == 15);
}
