// Test del mulligan London (regola 103.5): si pescano 7 carte; prima decide chi
// gioca per primo, poi l'altro; chi fa mulligan rimescola la mano e ripesca 7.
// Dopo aver tenuto, si mettono in fondo tante carte quanti i mulligan, una alla
// volta, nell'ordine scelto: la prima scelta resta più in alto, l'ultima va
// più in fondo.

#include "test_helpers.hpp"

using namespace mtg;
using namespace test;

namespace {

Staged with_mulligans(PlayerId starting, std::uint64_t seed = 7) {
    Staged st{new_game(seed), {}};
    start_game(st.s, db_with_tokens(), StartOptions{starting, true}, &st.log);
    require_invariants(st.s);
    return st;
}

const Action kKeep{ActionType::Keep};
const Action kMulligan{ActionType::Mulligan};

}  // namespace

TEST_CASE("Mulligan: si pescano 7 carte e decide prima chi gioca per primo", "[mulligan]") {
    Staged st = with_mulligans(1);
    for (PlayerId p = 0; p < kNumPlayers; ++p) {
        CHECK(st.s.players[p].hand.size() == 7);
        CHECK(st.s.players[p].library.size() == 53);
    }
    REQUIRE(is_decision(st.s, DecisionKind::Mulligan, 1));
    CHECK(legal(st.s) == std::vector<Action>{kKeep, kMulligan});
    act(st, kKeep);
    CHECK(is_decision(st.s, DecisionKind::Mulligan, 0));
}

TEST_CASE("Mulligan: nuova mano da 7, poi una carta in fondo", "[mulligan]") {
    Staged st = with_mulligans(0);
    act(st, kMulligan);  // giocatore 0
    act(st, kKeep);      // giocatore 1
    REQUIRE(is_decision(st.s, DecisionKind::Mulligan, 0));
    CHECK(st.s.players[0].mulligans == 1);
    CHECK(st.s.players[0].hand.size() == 7);
    CHECK(st.s.players[0].library.size() == 53);

    act(st, kKeep);
    REQUIRE(is_decision(st.s, DecisionKind::BottomCards, 0));
    CHECK(st.s.pending.amount == 1);
    const Action bottom = legal(st.s).front();
    act(st, bottom);

    // Il motore prosegue fino alla decisione successiva (forse dopo una pescata):
    // il fondo del grimorio però non si muove, e mano + grimorio restano 60.
    CHECK(st.s.players[0].library[0] == bottom.card);  // indice 0 = fondo
    CHECK(st.s.players[0].hand.size() + st.s.players[0].library.size() == 60);
    CHECK(st.s.turn.turn_number >= 1);
    CHECK(st.s.pending.kind != DecisionKind::Mulligan);
    CHECK(st.s.pending.kind != DecisionKind::BottomCards);
}

TEST_CASE("Mulligan: le carte vanno in fondo nell'ordine scelto", "[mulligan]") {
    Staged st = with_mulligans(0);
    act(st, kMulligan);
    act(st, kKeep);      // giocatore 1
    act(st, kMulligan);  // secondo mulligan del giocatore 0
    act(st, kKeep);
    REQUIRE(is_decision(st.s, DecisionKind::BottomCards, 0));
    CHECK(st.s.pending.amount == 2);

    const Action first = legal(st.s).front();
    act(st, first);
    REQUIRE(is_decision(st.s, DecisionKind::BottomCards, 0));
    const Action second = legal(st.s).front();
    CHECK(second.card != first.card);
    act(st, second);

    const auto& lib = st.s.players[0].library;
    CHECK(lib[0] == second.card);  // l'ultima scelta è la più in fondo
    CHECK(lib[1] == first.card);
    CHECK(st.s.players[0].hand.size() + lib.size() == 60);
}

TEST_CASE("Mulligan: entrambi nello stesso giro", "[mulligan]") {
    Staged st = with_mulligans(0);
    act(st, kMulligan);
    act(st, kMulligan);
    REQUIRE(is_decision(st.s, DecisionKind::Mulligan, 0));  // chi gioca per primo decide di nuovo per primo
    for (PlayerId p = 0; p < kNumPlayers; ++p) {
        CHECK(st.s.players[p].mulligans == 1);
        CHECK(st.s.players[p].hand.size() == 7);
    }
}

TEST_CASE("Mulligan: al massimo 7, poi si puo' solo tenere", "[mulligan]") {
    Staged st = with_mulligans(0);
    act(st, kMulligan);
    act(st, kKeep);  // giocatore 1
    for (int i = 1; i < 7; ++i) act(st, kMulligan);
    REQUIRE(is_decision(st.s, DecisionKind::Mulligan, 0));
    CHECK(st.s.players[0].mulligans == 7);
    CHECK(legal(st.s) == std::vector<Action>{kKeep});

    act(st, kKeep);
    std::vector<CardId> chosen;
    for (int i = 0; i < 7; ++i) {
        REQUIRE(is_decision(st.s, DecisionKind::BottomCards, 0));
        const Action a = legal(st.s).front();
        chosen.push_back(a.card);
        act(st, a);
    }
    // Tutta la mano è finita in fondo, l'ultima scelta più in basso.
    const auto& lib = st.s.players[0].library;
    for (std::size_t i = 0; i < chosen.size(); ++i) CHECK(lib[i] == chosen[chosen.size() - 1 - i]);
    CHECK(st.s.players[0].hand.size() + lib.size() == 60);
}
