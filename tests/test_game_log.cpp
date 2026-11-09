// Test del registro della partita (GameLog): oltre alle decisioni, il motore
// registra ogni cambio di zona, ogni variazione di vita con la sua fonte e
// l'inizio di ogni step. Dal registro si ricostruisce la partita (regola 5 di
// CLAUDE.md): mano iniziale, mulligan, pescate con il turno, carte giocate,
// stato del cimitero, esito.

#include "test_helpers.hpp"

#include <algorithm>
#include <map>

using namespace mtg;
using namespace test;

namespace {

constexpr PlayerId kUw = 0;
constexpr PlayerId kRakdos = 1;

using Kind = LogEntry::Kind;

// Gioca una partita casuale completa (o fino a `max_decisions`) registrando tutto.
GameState play_random(std::uint64_t seed, GameLog& log, const Decklist& deck0, std::size_t max_decisions = 100000) {
    const CardDatabase& db = db_with_tokens();
    GameState s = create_game(db, deck0, rakdos_deck(), seed);
    start_game(s, db, StartOptions{static_cast<PlayerId>(seed % 2), true}, &log);
    RandomAgent agent(seed);
    for (std::size_t i = 0; i < max_decisions && s.pending.kind != DecisionKind::GameOver; ++i) {
        const auto actions = legal_actions(s, db);
        apply(s, db, agent.choose(s, actions), &log);
    }
    return s;
}

std::vector<CardId> sorted(std::vector<CardId> v) {
    std::sort(v.begin(), v.end());
    return v;
}

std::vector<CardId> sorted_zone(const PlayerZone& z) { return sorted({z.begin(), z.end()}); }

}  // namespace

TEST_CASE("Registro: mano iniziale pescata prima del primo mulligan", "[log]") {
    GameLog log;
    const GameState s = play_random(11, log, uw_sim_deck(), 0);  // solo l'avvio
    REQUIRE(s.pending.kind == DecisionKind::Mulligan);
    int draws[kNumPlayers] = {0, 0};
    for (const LogEntry& e : log.entries) {
        REQUIRE(e.kind == Kind::ZoneChange);  // nessuna decisione ancora presa
        CHECK(e.from == Zone::Library);
        CHECK(e.to == Zone::Hand);
        CHECK(e.cause == Cause::Draw);
        CHECK(e.turn == 0);
        CHECK(e.def == s.objects[e.action.card].def);
        ++draws[e.player];
    }
    CHECK(draws[0] == 7);
    CHECK(draws[1] == 7);
}

TEST_CASE("Registro: ogni pescata dello step di pescata porta il turno e il giocatore attivo", "[log]") {
    GameLog log;
    (void)play_random(5, log, uw_sim_deck());
    int checked = 0;
    for (std::size_t i = 0; i < log.entries.size(); ++i) {
        const LogEntry& e = log.entries[i];
        if (e.kind != Kind::StepStart || e.step != Step::Draw || e.turn == 1) continue;
        // La prima voce dopo l'inizio dello step di pescata è la pescata del giocatore attivo.
        REQUIRE(i + 1 < log.entries.size());
        const LogEntry& draw = log.entries[i + 1];
        if (draw.kind == Kind::StepStart && draw.step != Step::Draw) continue;  // grimorio vuoto: partita finita
        CHECK(draw.kind == Kind::ZoneChange);
        CHECK(draw.cause == Cause::Draw);
        CHECK(draw.player == e.player);
        CHECK(draw.turn == e.turn);
        CHECK(draw.step == Step::Draw);
        ++checked;
    }
    CHECK(checked > 5);
}

TEST_CASE("Registro: gli step iniziano in ordine e i turni si alternano", "[log]") {
    GameLog log;
    (void)play_random(8, log, rakdos_deck());
    std::uint16_t last_turn = 0;
    PlayerId last_active = kNoPlayer;
    int turns = 0;
    for (const LogEntry& e : log.entries) {
        if (e.kind != Kind::StepStart || e.step != Step::Untap) continue;
        CHECK(e.turn == last_turn + 1);
        if (last_active != kNoPlayer) CHECK(e.player != last_active);
        last_turn = e.turn;
        last_active = e.player;
        ++turns;
    }
    CHECK(turns > 4);
}

TEST_CASE("Registro: dai cambi di zona si ricostruiscono mano e cimitero", "[log]") {
    for (const std::uint64_t seed : {1u, 2u, 3u, 4u}) {
        INFO("seed " << seed);
        GameLog log;
        const GameState s = play_random(seed, log, seed % 2 == 0 ? rakdos_deck() : uw_sim_deck());
        std::map<CardId, Zone> zone;  // assente = grimorio (o token non ancora creato)
        for (const LogEntry& e : log.entries) {
            if (e.kind != Kind::ZoneChange) continue;
            const auto it = zone.find(e.action.card);
            const Zone before = it == zone.end() ? Zone::Library : it->second;
            CHECK(before == e.from);
            zone[e.action.card] = e.to;
        }
        for (PlayerId p = 0; p < kNumPlayers; ++p) {
            std::vector<CardId> hand, graveyard;
            for (const auto& [card, z] : zone) {
                if (s.objects[card].owner != p) continue;
                if (z == Zone::Hand) hand.push_back(card);
                if (z == Zone::Graveyard) graveyard.push_back(card);
            }
            CHECK(sorted(hand) == sorted_zone(s.players[p].hand));
            CHECK(sorted(graveyard) == sorted_zone(s.players[p].graveyard));
        }
    }
}

TEST_CASE("Registro: la vita cambia con la sua fonte e la sua causa", "[log]") {
    // Rakdos rompe una fetch (paga 1 vita) e lancia Lightning Bolt su UW (3 danni).
    Staged st = staged(kRakdos, {"Plains"}, {"Lightning Bolt"}, {}, {"Blood Crypt", "Marsh Flats"});
    const ObjRef fetch = find_on_battlefield(st.s, "Marsh Flats");
    act(st, Action{ActionType::ActivateAbility, fetch.card, 0});
    act(st, pass());  // la fetch si risolve: cerca
    act(st, Action{ActionType::ChooseOption, kNoCard, kDeclineSearch});
    const ObjRef bolt = find_cards(st.s, kRakdos, Zone::Hand, "Lightning Bolt")[0];
    act(st, Action{ActionType::CastSpell, bolt.card, 0});
    act(st, Action{ActionType::TargetPlayer, kNoCard, kUw});
    act(st, pass());

    std::vector<LogEntry> life;
    for (const LogEntry& e : st.log.entries) {
        if (e.kind == Kind::LifeChange) life.push_back(e);
    }
    REQUIRE(life.size() == 2);
    CHECK(life[0].player == kRakdos);
    CHECK(life[0].amount == -1);
    CHECK(life[0].cause == Cause::Cost);
    CHECK(life[0].def == db_with_tokens().id_of("Marsh Flats"));
    CHECK(life[1].player == kUw);
    CHECK(life[1].amount == -3);
    CHECK(life[1].cause == Cause::Damage);
    CHECK(life[1].def == db_with_tokens().id_of("Lightning Bolt"));
    CHECK(life[1].action.card == bolt.card);
}

TEST_CASE("Registro: il danno di combattimento ha come fonte l'attaccante", "[log]") {
    Staged st = staged(kRakdos, {"Plains"}, {}, {}, {"Marsh Flats", "Stitcher's Supplier"});
    for (int guard = 0; guard < 10 && st.s.pending.kind == DecisionKind::Priority; ++guard) act(st, pass());
    REQUIRE(st.s.pending.kind == DecisionKind::DeclareAttacker);
    act(st, Action{ActionType::TargetPlayer, kNoCard, kUw});
    for (int guard = 0; guard < 20 && st.s.players[kUw].life == 20; ++guard) act(st, pass());
    const auto it = std::find_if(st.log.entries.begin(), st.log.entries.end(),
                                 [](const LogEntry& e) { return e.kind == Kind::LifeChange; });
    REQUIRE(it != st.log.entries.end());
    CHECK(it->player == kUw);
    CHECK(it->amount == -1);
    CHECK(it->cause == Cause::Damage);
    CHECK(it->def == db_with_tokens().id_of("Stitcher's Supplier"));
    CHECK(it->step == Step::CombatDamage);
}

TEST_CASE("Registro: le decisioni di attacco portano la creatura che decide", "[log]") {
    Staged st = staged(kRakdos, {"Plains"}, {}, {}, {"Marsh Flats", "Stitcher's Supplier"});
    for (int guard = 0; guard < 10 && st.s.pending.kind == DecisionKind::Priority; ++guard) act(st, pass());
    REQUIRE(st.s.pending.kind == DecisionKind::DeclareAttacker);
    const CardId supplier = st.s.pending.subject.card;
    act(st, Action{ActionType::TargetPlayer, kNoCard, kUw});
    const LogEntry& e = *std::find_if(st.log.entries.rbegin(), st.log.entries.rend(),
                                      [](const LogEntry& x) { return x.kind == Kind::Action; });
    CHECK(e.decision == DecisionKind::DeclareAttacker);
    CHECK(e.subject == supplier);
    CHECK(e.def == db_with_tokens().id_of("Stitcher's Supplier"));
}

TEST_CASE("Registro: stesso seed, stesso registro completo", "[log]") {
    GameLog a, b;
    (void)play_random(21, a, rakdos_deck());
    (void)play_random(21, b, rakdos_deck());
    CHECK(a.entries == b.entries);
}
