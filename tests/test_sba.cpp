// Test delle azioni di stato (regola 704). Si preparano le condizioni mentre il
// giocatore 0 ha la priorità nella prima fase principale; quando passa, le
// azioni di stato si controllano prima che Rakdos riceva la priorità.
//
// Le creature usate (Moonshadow, Street Wraith, Phelia, Nethergoyf) servono
// solo per le caratteristiche stampate lette da cards.json: nessuna loro
// abilità è implementata in questo passo.

#include "test_helpers.hpp"

using namespace mtg;
using namespace test;

namespace {

Staged at_main_phase() {
    Staged st = staged(0, {"Plains"}, {"Swamp"});
    REQUIRE(is_decision(st.s, DecisionKind::Priority, 0));
    return st;
}

ObjRef onto_battlefield(GameState& s, PlayerId player, const char* name) {
    const ObjRef card = find_cards(s, player, Zone::Library, name)[0];
    return move_object(s, db_with_tokens(), card, Zone::Battlefield, Cause::Test).moves[0].new_ref;
}

}  // namespace

TEST_CASE("SBA: punti vita a 0 o meno", "[sba]") {
    Staged st = at_main_phase();
    st.s.players[0].life = 0;
    act(st, pass());
    CHECK(st.s.pending.kind == DecisionKind::GameOver);
    CHECK(st.s.turn.game_over);
    CHECK(st.s.turn.winner == 1);
    CHECK(legal(st.s).empty());
}

TEST_CASE("SBA: entrambi a 0 punti vita, partita patta", "[sba]") {
    Staged st = at_main_phase();
    st.s.players[0].life = -3;
    st.s.players[1].life = 0;
    act(st, pass());
    CHECK(st.s.turn.game_over);
    CHECK(st.s.turn.winner == kNoPlayer);
}

TEST_CASE("SBA: pescata da grimorio vuoto", "[sba]") {
    Staged st = at_main_phase();
    std::vector<ObjRef> library;
    for (const CardId id : st.s.players[1].library) library.push_back(st.s.ref(id));
    move_objects(st.s, db_with_tokens(), library, Zone::Exile, Cause::Test);

    act(st, pass());  // fase principale 2 del giocatore 0
    act(st, pass());  // turno 2: Rakdos pesca da un grimorio vuoto
    CHECK(st.s.players[1].drew_from_empty_library);
    CHECK(st.s.turn.game_over);
    CHECK(st.s.turn.winner == 0);
}

TEST_CASE("SBA: costituzione 0 o meno", "[sba]") {
    Staged st = at_main_phase();
    const ObjRef moon = onto_battlefield(st.s, 1, "Moonshadow");  // stampata 7/7
    st.s.objects[moon.card].counters.minus_one = 7;
    act(st, pass());
    CHECK(st.s.objects[moon.card].zone == Zone::Graveyard);
    CHECK(st.s.players[1].gy.card_count == 1);
}

TEST_CASE("SBA: danno letale", "[sba]") {
    Staged st = at_main_phase();
    const ObjRef dying = onto_battlefield(st.s, 1, "Street Wraith");  // 3/4
    const ObjRef alive = onto_battlefield(st.s, 1, "Street Wraith");
    st.s.objects[dying.card].damage = 4;
    st.s.objects[alive.card].damage = 3;
    act(st, pass());
    CHECK(st.s.objects[dying.card].zone == Zone::Graveyard);
    CHECK(st.s.objects[alive.card].zone == Zone::Battlefield);
}

TEST_CASE("SBA: regola delle leggende, sceglie il controllore", "[sba]") {
    Staged st = at_main_phase();
    const ObjRef first = onto_battlefield(st.s, 0, "Phelia, Exuberant Shepherd");
    const ObjRef second = onto_battlefield(st.s, 0, "Phelia, Exuberant Shepherd");
    act(st, pass());

    REQUIRE(is_decision(st.s, DecisionKind::LegendRule, 0));
    const auto choices = legal(st.s);
    REQUIRE(choices.size() == 2);  // nessuna fusione: le due copie possono differire
    act(st, Action{ActionType::ChooseCard, second.card, 0});

    CHECK(st.s.objects[first.card].zone == Zone::Graveyard);
    CHECK(st.s.is_valid(second));
    CHECK(st.s.objects[second.card].zone == Zone::Battlefield);
}

TEST_CASE("SBA: ripetute finche' non cambia nulla (token)", "[sba]") {
    Staged st = at_main_phase();
    const ObjRef cat = create_token(st.s, db_with_tokens(), db_with_tokens().id_of("Cat Token"), 1);
    st.s.objects[cat.card].counters.minus_one = 1;  // 1/1 con un -1/-1: costituzione 0
    act(st, pass());
    // Primo passaggio: il token va al cimitero. Secondo: smette di esistere.
    CHECK(st.s.objects[cat.card].zone == Zone::None);
    CHECK(st.s.players[1].graveyard.empty());
    CHECK(st.s.players[1].gy.card_count == 0);
}

TEST_CASE("SBA: annullamento dei segnalini +1/+1 e -1/-1", "[sba]") {
    Staged st = at_main_phase();
    const ObjRef moon = onto_battlefield(st.s, 1, "Moonshadow");
    st.s.objects[moon.card].counters.plus_one = 2;
    st.s.objects[moon.card].counters.minus_one = 3;
    act(st, pass());
    CHECK(st.s.objects[moon.card].zone == Zone::Battlefield);
    CHECK(st.s.objects[moon.card].counters.plus_one == 0);
    CHECK(st.s.objects[moon.card].counters.minus_one == 1);
    CHECK(toughness(st.s, db_with_tokens(), moon.card) == 6);
}

TEST_CASE("SBA: Nethergoyf con il cimitero vuoto e' una 0/1 e sopravvive", "[sba]") {
    // Nethergoyf ha */1+*: con il cimitero vuoto la costituzione è 1 (test della carta
    // in test_rakdos_cards.cpp).
    Staged st = at_main_phase();
    const ObjRef goyf = onto_battlefield(st.s, 1, "Nethergoyf");
    CHECK(toughness(st.s, db_with_tokens(), goyf.card) == 1);
    act(st, pass());
    CHECK(st.s.objects[goyf.card].zone == Zone::Battlefield);
}
