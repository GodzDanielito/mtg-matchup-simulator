// Test degli agenti. L'agente ha un generatore casuale suo, separato da quello
// della partita: la partita usa il proprio solo per i mescolamenti e la
// casualità delle regole. Così due agenti diversi giocano con gli stessi
// "numeri casuali comuni" (stessi grimori, stesse mani, stessi mescolamenti
// a parità di azioni), e il confronto tra agenti o tra mazzi è più preciso.

#include "test_helpers.hpp"

using namespace mtg;
using namespace test;

namespace {

std::vector<CardId> cards_of(const PlayerZone& zone) { return {zone.begin(), zone.end()}; }

GameState started(std::uint64_t seed) {
    GameState s = new_game(seed);
    start_game(s, db_with_tokens(), StartOptions{0, true});
    return s;
}

}  // namespace

TEST_CASE("Agenti diversi, stesso seed: stessi grimori e mani iniziali", "[agents]") {
    GameState a = started(42);
    GameState b = started(42);
    RandomAgent first(1);
    RandomAgent second(2);

    // Ogni agente prende decisioni sulla sua partita (le scelte vengono scartate).
    for (int i = 0; i < 50; ++i) {
        (void)first.choose(a, legal(a));
        (void)second.choose(b, legal(b));
    }
    for (PlayerId p = 0; p < kNumPlayers; ++p) {
        CHECK(cards_of(a.players[p].library) == cards_of(b.players[p].library));
        CHECK(cards_of(a.players[p].hand) == cards_of(b.players[p].hand));
    }

    // A parità di azioni, anche la casualità successiva coincide: dopo un mulligan
    // di entrambi le nuove mani sono identiche. Se le scelte degli agenti
    // consumassero il generatore della partita, sarebbero diverse.
    for (GameState* s : {&a, &b}) {
        apply(*s, db_with_tokens(), Action{ActionType::Mulligan});
        apply(*s, db_with_tokens(), Action{ActionType::Mulligan});
    }
    for (PlayerId p = 0; p < kNumPlayers; ++p) {
        CHECK(cards_of(a.players[p].hand) == cards_of(b.players[p].hand));
        CHECK(cards_of(a.players[p].library) == cards_of(b.players[p].library));
    }
}

TEST_CASE("Agente casuale: stesso seed, stesse scelte; seed diversi, scelte diverse", "[agents]") {
    const GameState s = started(7);
    const auto actions = legal(s);  // Keep o Mulligan
    RandomAgent a(5), b(5), c(6);
    int same = 0, differ = 0;
    for (int i = 0; i < 200; ++i) {
        const Action x = a.choose(s, actions);
        same += (x == b.choose(s, actions)) ? 1 : 0;
        differ += (x != c.choose(s, actions)) ? 1 : 0;
    }
    CHECK(same == 200);
    CHECK(differ > 50);
}
