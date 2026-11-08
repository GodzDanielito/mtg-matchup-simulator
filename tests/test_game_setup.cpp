// Test della creazione della partita: due liste e un seed, grimori mescolati.

#include "test_helpers.hpp"

#include <map>
#include <set>
#include <vector>

using namespace mtg;

namespace {

std::vector<std::string> library_names(const GameState& s, PlayerId p) {
    std::vector<std::string> names;
    for (const CardId id : s.players[p].library) names.push_back(test::db_with_tokens().get(s.objects[id].def).name);
    return names;
}

}  // namespace

TEST_CASE("La partita parte con 60 carte per grimorio e nient'altro", "[setup]") {
    const GameState s = test::new_game(1);
    for (PlayerId p = 0; p < kNumPlayers; ++p) {
        CHECK(s.players[p].library.size() == 60);
        CHECK(s.players[p].hand.empty());
        CHECK(s.players[p].graveyard.empty());
        CHECK(s.players[p].life == 20);
        CHECK(s.players[p].gy == GraveyardSummary{});
    }
    CHECK(s.battlefield.empty());
    CHECK(s.exile.empty());
    for (std::size_t slot = kMaxCards; slot < kMaxObjects; ++slot) CHECK(s.objects[slot].zone == Zone::None);
    test::require_invariants(s);
}

TEST_CASE("Il grimorio contiene esattamente la lista", "[setup]") {
    const GameState s = test::new_game(1);
    std::map<std::string, int> uw;
    for (const auto& n : library_names(s, 0)) ++uw[n];
    std::map<std::string, int> expected;
    for (const auto& e : test::uw_sim_deck().main) expected[e.card] += e.count;
    CHECK(uw == expected);

    // Ogni carta appartiene al giocatore giusto.
    for (const CardId id : s.players[1].library) CHECK(s.objects[id].owner == 1);
}

TEST_CASE("Stesso seed, stesso ordine dei grimori", "[setup]") {
    const GameState a = test::new_game(424242);
    const GameState b = test::new_game(424242);
    CHECK(library_names(a, 0) == library_names(b, 0));
    CHECK(library_names(a, 1) == library_names(b, 1));
    // Anche gli identificativi delle carte fisiche coincidono.
    CHECK(std::vector<CardId>(a.players[0].library.begin(), a.players[0].library.end()) ==
          std::vector<CardId>(b.players[0].library.begin(), b.players[0].library.end()));
}

TEST_CASE("Seed diversi, ordini diversi", "[setup]") {
    std::set<std::vector<CardId>> orders;
    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
        const GameState s = test::new_game(seed);
        orders.insert(std::vector<CardId>(s.players[1].library.begin(), s.players[1].library.end()));
    }
    CHECK(orders.size() == 20);
}

TEST_CASE("Il grimorio viene davvero mescolato", "[setup]") {
    // Senza mescolare, le carte sarebbero nell'ordine della lista: 0, 1, 2, ...
    const GameState s = test::new_game(3);
    int in_place = 0;
    for (std::size_t i = 0; i < s.players[0].library.size(); ++i) {
        in_place += (s.players[0].library[i] == static_cast<CardId>(i)) ? 1 : 0;
    }
    CHECK(in_place < 10);
}

TEST_CASE("Una lista illegale o con carte sconosciute viene rifiutata", "[setup]") {
    Decklist short_deck = test::rakdos_deck();
    short_deck.main.pop_back();
    CHECK_THROWS_AS(create_game(test::real_db(), test::uw_sim_deck(), short_deck, 1), EngineError);

    Decklist unknown = test::rakdos_deck();
    unknown.main.back().card = "Lightning Blot";
    CHECK_THROWS_AS(create_game(test::real_db(), test::uw_sim_deck(), unknown, 1), CardDataError);
}
