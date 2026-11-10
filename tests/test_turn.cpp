// Test della struttura del turno e della priorità.
// UW Blink è il giocatore 0, Rakdos il giocatore 1. Le partite "preparate"
// partono senza mulligan, con mani e campo scelti dal test.

#include "test_helpers.hpp"

#include <algorithm>

using namespace mtg;
using namespace test;

namespace {

bool logged(const GameLog& log, LogEntry::Kind kind, PlayerId player, std::uint16_t turn, Step step) {
    return std::any_of(log.entries.begin(), log.entries.end(), [&](const LogEntry& e) {
        return e.kind == kind && e.player == player && e.turn == turn && e.step == step;
    });
}

}  // namespace

// --- Struttura del turno ---

TEST_CASE("Chi gioca per primo salta la pescata del primo turno", "[turn]") {
    Staged st = staged(0, {"Plains"}, {"Swamp"});
    REQUIRE(is_decision(st.s, DecisionKind::Priority, 0));
    CHECK(st.s.turn.turn_number == 1);
    CHECK(st.s.turn.step == Step::Main1);
    CHECK(st.s.players[0].hand.size() == 1);  // nessuna pescata
    CHECK(st.s.players[0].library.size() == 59);

    act(st, pass());  // la terra si può ancora giocare nella seconda fase principale
    REQUIRE(is_decision(st.s, DecisionKind::Priority, 0));
    CHECK(st.s.turn.step == Step::Main2);

    act(st, pass());
    REQUIRE(is_decision(st.s, DecisionKind::Priority, 1));
    CHECK(st.s.turn.turn_number == 2);
    CHECK(st.s.turn.active_player == 1);
    CHECK(st.s.turn.step == Step::Main1);
    CHECK(st.s.players[1].hand.size() == 2);  // il secondo giocatore pesca
    CHECK(st.s.players[1].library.size() == 58);
}

TEST_CASE("Una terra per turno", "[turn]") {
    Staged st = staged(0, {"Plains", "Plains"}, {"Swamp"});
    act(st, find_action(st.s, ActionType::PlayLand, "Plains"));

    // Dopo la terra resta solo "passa": la seconda fase principale passa da sola.
    REQUIRE(is_decision(st.s, DecisionKind::Priority, 1));
    CHECK(st.s.turn.turn_number == 2);
    CHECK(st.s.battlefield.size() == 1);
    CHECK(st.s.players[0].hand.size() == 1);
    CHECK(logged(st.log, LogEntry::Kind::AutoPass, 0, 1, Step::Main2));
    CHECK_FALSE(logged(st.log, LogEntry::Kind::Action, 0, 1, Step::Main2));
}

TEST_CASE("Debolezza da evocazione azzerata all'inizio del turno del controllore", "[turn]") {
    Staged st = staged(0, {"Plains", "Plains"}, {"Swamp"});
    act(st, find_action(st.s, ActionType::PlayLand, "Plains"));
    const ObjRef plains = find_on_battlefield(st.s, "Plains");
    CHECK(st.s.objects[plains.card].has_flag(ObjectFlag::SummoningSick));

    // Turno 2 di Rakdos: la Plains del giocatore 0 è ancora "malata".
    REQUIRE(is_decision(st.s, DecisionKind::Priority, 1));
    CHECK(st.s.objects[plains.card].has_flag(ObjectFlag::SummoningSick));
    act(st, find_action(st.s, ActionType::PlayLand, "Swamp"));
    const ObjRef swamp = find_on_battlefield(st.s, "Swamp");

    // Turno 3 del giocatore 0: la sua Plains non lo è più, la Swamp di Rakdos sì.
    REQUIRE(is_decision(st.s, DecisionKind::Priority, 0));
    CHECK(st.s.turn.turn_number == 3);
    CHECK_FALSE(st.s.objects[plains.card].has_flag(ObjectFlag::SummoningSick));
    CHECK(st.s.objects[swamp.card].has_flag(ObjectFlag::SummoningSick));
}

TEST_CASE("Stap dei permanenti del giocatore attivo", "[turn]") {
    Staged st = staged(1, {"Plains"}, {"Raucous Theater", "Swamp"});
    act(st, find_action(st.s, ActionType::PlayLand, "Raucous Theater"));  // entra tappata
    REQUIRE(is_decision(st.s, DecisionKind::Surveil, 1));
    act(st, option(kSurveilKeepOnTop));
    const ObjRef theater = find_on_battlefield(st.s, "Raucous Theater");

    REQUIRE(is_decision(st.s, DecisionKind::Priority, 0));  // turno 2 del giocatore 0
    CHECK(st.s.objects[theater.card].has_flag(ObjectFlag::Tapped));
    act(st, pass());
    act(st, pass());

    REQUIRE(is_decision(st.s, DecisionKind::Priority, 1));  // turno 3 di Rakdos
    CHECK(st.s.turn.turn_number == 3);
    CHECK_FALSE(st.s.objects[theater.card].has_flag(ObjectFlag::Tapped));
}

TEST_CASE("La riserva di mana si svuota tra uno step e l'altro", "[turn]") {
    Staged st = staged(0, {"Plains"}, {"Swamp"});
    st.s.players[0].pool.amount[static_cast<std::size_t>(Color::White)] = 2;
    st.s.players[1].pool.amount[static_cast<std::size_t>(Color::Black)] = 1;
    act(st, pass());
    REQUIRE(st.s.turn.step == Step::Main2);
    for (PlayerId p = 0; p < kNumPlayers; ++p) {
        for (const auto amount : st.s.players[p].pool.amount) CHECK(amount == 0);
    }
}

TEST_CASE("Scarto fino a 7 carte nella sottofase di cancellazione", "[turn]") {
    // Carte che senza terre non si lanciano (Solitude no: si evoca senza mana).
    Staged st = staged(0,
                       {"Guide of Souls", "Guide of Souls", "Ephemerate", "Starfield Shepherd", "Ocelot Pride",
                        "Quantum Riddler", "Phelia, Exuberant Shepherd", "Consign to Memory", "Thraben Charm"},
                       {"Swamp"});
    // Nessuna terra: il turno passa da solo fino alla cancellazione.
    REQUIRE(is_decision(st.s, DecisionKind::DiscardToHandSize, 0));
    CHECK(st.s.turn.step == Step::Cleanup);
    CHECK(st.s.pending.amount == 2);
    CHECK(legal(st.s).size() == 8);  // 9 carte, 8 nomi diversi: una scelta per nome

    act(st, find_action(st.s, ActionType::ChooseCard, "Guide of Souls"));
    REQUIRE(is_decision(st.s, DecisionKind::DiscardToHandSize, 0));
    CHECK(st.s.players[0].hand.size() == 9);  // gli scarti avvengono insieme, alla fine
    act(st, find_action(st.s, ActionType::ChooseCard, "Guide of Souls"));  // l'altra copia

    CHECK(st.s.players[0].hand.size() == 7);
    CHECK(st.s.players[0].graveyard.size() == 2);
    CHECK(st.s.players[0].gy.card_count == 2);
    REQUIRE(is_decision(st.s, DecisionKind::Priority, 1));
    CHECK(st.s.turn.turn_number == 2);
}

// --- Priorità ---

TEST_CASE("Azioni legali: una sola azione per carte identiche", "[priority]") {
    Staged st = staged(0, {"Plains", "Plains", "Hallowed Fountain"}, {"Swamp"});
    const auto actions = legal(st.s);
    CHECK(actions.size() == 3);  // passa, Plains, Hallowed Fountain
    CHECK(std::count(actions.begin(), actions.end(), pass()) == 1);
}

TEST_CASE("Un'azione non legale viene rifiutata senza modifiche", "[priority]") {
    Staged st = staged(0, {"Plains"}, {"Swamp"});
    const ObjRef swamp = find_cards(st.s, 1, Zone::Hand, "Swamp")[0];
    CHECK_THROWS_AS(apply(st.s, db_with_tokens(), Action{ActionType::PlayLand, swamp.card, 0}), EngineError);
    CHECK(is_decision(st.s, DecisionKind::Priority, 0));
    CHECK(st.s.battlefield.empty());
}

TEST_CASE("Il giocatore non attivo riceve la priorita' se puo' agire", "[priority]") {
    Staged st = staged(0, {"Plains"}, {}, {}, {"Marsh Flats"});
    // Mantenimento del turno 1: il giocatore 0 passa da solo, Rakdos può attivare la fetch.
    REQUIRE(is_decision(st.s, DecisionKind::Priority, 1));
    CHECK(st.s.turn.step == Step::Upkeep);
    CHECK(st.s.turn.active_player == 0);
    CHECK(legal(st.s).size() == 2);
    CHECK(logged(st.log, LogEntry::Kind::AutoPass, 0, 1, Step::Upkeep));
}

TEST_CASE("Il passaggio automatico viene registrato ma non e' una decisione", "[priority]") {
    Staged st = staged(0, {"Plains"}, {"Swamp"});
    CHECK(logged(st.log, LogEntry::Kind::AutoPass, 0, 1, Step::Upkeep));
    CHECK(logged(st.log, LogEntry::Kind::AutoPass, 1, 1, Step::Upkeep));
    CHECK(logged(st.log, LogEntry::Kind::AutoPass, 0, 1, Step::Draw));
    // Nessuna decisione registrata prima della prima fase principale.
    CHECK(std::none_of(st.log.entries.begin(), st.log.entries.end(),
                       [](const LogEntry& e) { return e.kind == LogEntry::Kind::Action; }));
}
