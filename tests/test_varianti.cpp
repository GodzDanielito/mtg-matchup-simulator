// Test dei conti del confronto di varianti (tools/varianti.hpp): differenza appaiata e ricariche di
// Quantum Riddler lette dal registro.

#include "goldfish_agent.hpp"
#include "matchup.hpp"
#include "rakdos_agent.hpp"
#include "test_helpers.hpp"
#include "uw_agent.hpp"
#include "varianti.hpp"

#include <catch2/catch_approx.hpp>

#include <functional>

using namespace mtg;
using namespace test;

TEST_CASE("Varianti: differenza appaiata con l'intervallo al 95%", "[varianti]") {
    // d = variante - base = {0, 1, 0, 0}: media 0,25, deviazione standard campionaria 0,5, errore 0,25.
    const PairedDiff d = paired_difference({1, 0, 1, 0}, {1, 1, 1, 0});
    CHECK(d.n == 4);
    CHECK(d.mean == Catch::Approx(0.25));
    CHECK(d.low == Catch::Approx(0.25 - 1.96 * 0.25));
    CHECK(d.high == Catch::Approx(0.25 + 1.96 * 0.25));
}

TEST_CASE("Varianti: due versioni identiche hanno differenza zero e intervallo nullo", "[varianti]") {
    const PairedDiff d = paired_difference({1, 0, 1}, {1, 0, 1});
    CHECK(d.mean == 0.0);
    CHECK(d.low == 0.0);
    CHECK(d.high == 0.0);
}

TEST_CASE("Varianti: le ricariche di Riddler e le carte pescate, dal registro", "[varianti]") {
    GameLog log;
    auto action = [&](PlayerId p, ActionType type) {
        LogEntry e;
        e.kind = LogEntry::Kind::Action;
        e.player = p;
        e.action.type = type;
        log.entries.push_back(e);
        return log.entries.size() - 1;
    };
    auto draw = [&](PlayerId p) {
        LogEntry e;
        e.kind = LogEntry::Kind::ZoneChange;
        e.player = p;
        e.from = Zone::Library;
        e.to = Zone::Hand;
        e.cause = Cause::Draw;
        log.entries.push_back(e);
    };
    auto untap = [&] {
        LogEntry e;
        e.kind = LogEntry::Kind::StepStart;
        e.step = Step::Untap;
        log.entries.push_back(e);
    };
    std::map<std::size_t, std::string> rules;
    rules[action(0, ActionType::CastSpell)] = "UW: Quantum Riddler: warp come ricarica";
    action(0, ActionType::Pass);  // un passaggio non interrompe
    draw(1);                      // le pescate di Rakdos non contano
    draw(0);
    draw(0);
    rules[action(0, ActionType::CastSpell)] = "UW: creatura da un mana: Guide of Souls";  // la giocata dopo: si smette
    draw(0);
    rules[action(0, ActionType::CastSpell)] = "UW: Quantum Riddler: warp come ricarica";
    draw(0);
    untap();  // il turno dopo: la pescata del turno non e' della ricarica
    draw(0);
    rules[action(1, ActionType::CastSpell)] = "Rakdos: Quantum Riddler: warp come ricarica";  // solo UW
    // Seed 96: la ricarica pesca 2, poi Phelia esilia Riddler attaccando e alla sottofase finale Riddler
    // torna e pesca di nuovo. Quella pescata non e' della ricarica: si conta solo la risoluzione del
    // trigger, cioe' le pescate consecutive fino al primo evento dopo.
    rules[action(0, ActionType::CastSpell)] = "UW: Quantum Riddler: warp come ricarica";
    action(0, ActionType::ChooseOption);  // l'ordine dei trigger (Guide of Souls e Riddler): prima delle pescate
    draw(0);
    draw(0);
    action(0, ActionType::TargetPlayer);  // Phelia attacca: non e' una giocata, ma la risoluzione e' finita
    draw(0);                              // Riddler torna con Phelia e pesca: non conta
    const RefillStats r = count_refill_warps(log, rules);
    CHECK(r.casts == 3);
    CHECK(r.cards == 5);
}

TEST_CASE("Varianti: le partenze di W1 e Consign che neutralizza davvero l'esilio, dal registro", "[varianti]") {
    GameLog log;
    std::map<std::size_t, std::string> rules;
    auto action = [&](PlayerId p, CardId card, std::string rule) {
        LogEntry e;
        e.kind = LogEntry::Kind::Action;
        e.player = p;
        e.action.type = ActionType::CastSpell;
        e.action.card = card;
        log.entries.push_back(e);
        rules[log.entries.size() - 1] = std::move(rule);
    };
    auto leaves = [&](CardId card, Zone to) {
        LogEntry e;
        e.kind = LogEntry::Kind::ZoneChange;
        e.player = 0;
        e.action.card = card;
        e.from = Zone::Battlefield;
        e.to = to;
        log.entries.push_back(e);
    };
    auto untap = [&] {
        LogEntry e;
        e.kind = LogEntry::Kind::StepStart;
        e.step = Step::Untap;
        log.entries.push_back(e);
    };
    const std::string w1 = "UW: Quantum Riddler: warp con Consign per l'esilio (variante W1)";
    const std::string consign = "UW: Consign to Memory: l'esilio del warp";
    // 1. Il caso buono: Consign sull'esilio, Riddler ancora in campo al turno dopo.
    action(0, 5, w1);
    action(0, 9, consign);
    untap();
    // 2. Consign lanciato, ma Riddler muore in risposta (Flare of Malice): non neutralizza nulla.
    action(0, 6, w1);
    action(0, 9, consign);
    leaves(6, Zone::Graveyard);
    untap();
    // 3. Consign sparito (Thoughtseize): l'esilio del warp si risolve.
    action(0, 7, w1);
    leaves(7, Zone::Exile);
    untap();
    // 4. Ephemerate salva Riddler prima di Consign: resta in campo, ma non per Consign.
    action(0, 10, w1);
    leaves(10, Zone::Exile);
    {
        LogEntry e;
        e.kind = LogEntry::Kind::ZoneChange;
        e.player = 0;
        e.action.card = 10;
        e.from = Zone::Exile;
        e.to = Zone::Battlefield;
        log.entries.push_back(e);
    }
    untap();
    // 5. Un'altra carta che lascia il campo non conta; la partita finisce prima del turno dopo.
    action(0, 8, w1);
    action(0, 9, consign);
    leaves(3, Zone::Graveyard);
    // Le regole di Rakdos non contano.
    action(1, 4, "Rakdos: Quantum Riddler: warp con Consign per l'esilio (variante W1)");
    const W1Stats w = count_w1(log, rules);
    CHECK(w.casts == 5);
    CHECK(w.consign_cast == 3);
    CHECK(w.saved == 2);
    CHECK(w.stayed == 3);
}

TEST_CASE("Varianti: i conteggi di W1 si sommano campo per campo", "[varianti]") {
    W1Stats total{1, 2, 3, 4};
    total += W1Stats{10, 20, 30, 40};
    CHECK(total.casts == 11);
    CHECK(total.consign_cast == 22);
    CHECK(total.saved == 33);
    CHECK(total.stayed == 44);
}

TEST_CASE("Varianti: in partite vere W1 parte e Consign tiene Riddler in campo", "[varianti][matchup]") {
    MatchupConfig c;
    c.db = &db_with_tokens();
    c.uw = uw_sim_deck();
    c.rakdos = rakdos_deck();
    c.uw_agent.riddler_warp_with_consign = true;
    W1Stats total;
    for (std::uint64_t seed = 1; seed <= 200 && total.saved < 3; ++seed) {
        GameLog log;
        std::map<std::size_t, std::string> rules;
        (void)play_matchup(c, seed, &log, &rules);
        const W1Stats w = count_w1(log, rules);
        CHECK(w.saved <= w.consign_cast);
        CHECK(w.consign_cast <= w.casts);
        CHECK(w.saved <= w.stayed);  // neutralizzato l'esilio, Riddler e' ancora in campo
        CHECK(w.stayed <= w.casts);
        total.casts += w.casts;
        total.saved += w.saved;
    }
    CHECK(total.saved >= 1);
}

// ---------------------------------------------------------------------------
// Varianti di Rakdos (docs/agente_rakdos.md, sezione 21): Teferi (X3) e Thoughtseize al turno 1 (X4).
// ---------------------------------------------------------------------------

namespace {

// Un registro scritto a mano: inizio di step, cambi di zona e decisioni, con il turno.
struct HandLog {
    GameLog log;
    std::map<std::size_t, std::string> rules;
    std::uint16_t turn = 0;

    void step(PlayerId active, Step st) {
        LogEntry e;
        e.kind = LogEntry::Kind::StepStart;
        e.player = active;
        e.step = st;
        e.turn = turn;
        log.entries.push_back(e);
    }
    void begin_turn(PlayerId active) {
        ++turn;
        step(active, Step::Untap);
    }
    void move(PlayerId owner, CardDefId def, Zone from, Zone to, Cause cause) {
        LogEntry e;
        e.kind = LogEntry::Kind::ZoneChange;
        e.player = owner;
        e.def = def;
        e.from = from;
        e.to = to;
        e.cause = cause;
        e.turn = turn;
        log.entries.push_back(e);
    }
    void decide(PlayerId p, std::string rule) {
        LogEntry e;
        e.kind = LogEntry::Kind::Action;
        e.player = p;
        e.turn = turn;
        log.entries.push_back(e);
        rules[log.entries.size() - 1] = std::move(rule);
    }
};

constexpr CardDefId kTeferiDef = 40;
constexpr CardDefId kSeizeDef = 41;
constexpr CardDefId kSolitudeDef = 42;

}  // namespace

TEST_CASE("Varianti: Teferi lanciato, i turni di Rakdos per modalita' e Teferi tolto, dal registro", "[varianti]") {
    HandLog h;
    h.begin_turn(0);
    h.move(0, kTeferiDef, Zone::Hand, Zone::Stack, Cause::Cast);  // UW lancia Teferi
    h.move(0, kTeferiDef, Zone::Stack, Zone::Battlefield, Cause::Resolve);
    h.decide(1, "Rakdos: blocco: nessun blocco conveniente [partita lunga]");  // nel turno di UW: non conta
    // Turno di Rakdos in corsa: conta la prima decisione con modalita', anche se dopo cambia.
    h.begin_turn(1);
    h.decide(1, "Rakdos: terra: fetch");  // senza modalita': non decide il turno
    h.decide(1, "Rakdos: Bolt: in faccia, corsa contro Teferi [corsa]");
    h.decide(1, "Rakdos: attacco: su Teferi [partita lunga]");
    h.decide(0, "UW: passa [corsa]");  // le regole di UW non contano
    h.begin_turn(0);
    // Turno di Rakdos in partita lunga: l'attacco toglie Teferi.
    h.begin_turn(1);
    h.decide(1, "Rakdos: attacco: su Teferi, abbastanza danno da toglierlo [partita lunga]");
    h.move(0, kTeferiDef, Zone::Battlefield, Zone::Graveyard, Cause::StateBased);
    // Turno di Rakdos senza Teferi: nessuna modalita'.
    h.begin_turn(0);
    h.begin_turn(1);
    h.decide(1, "Rakdos: attacco: non puo' essere bloccata");
    const TeferiStats t = count_teferi(h.log, h.rules, kTeferiDef);
    CHECK(t.cast == 1);
    CHECK(t.race_turns == 1);
    CHECK(t.long_turns == 1);
    CHECK(t.removed == 1);
}

TEST_CASE("Varianti: un'altra carta che va al cimitero non e' Teferi tolto", "[varianti]") {
    HandLog h;
    h.begin_turn(1);
    h.move(0, kSolitudeDef, Zone::Battlefield, Zone::Graveyard, Cause::Damage);
    h.move(0, kTeferiDef, Zone::Hand, Zone::Graveyard, Cause::Discard);  // preso da Thoughtseize: non e' in campo
    const TeferiStats t = count_teferi(h.log, h.rules, kTeferiDef);
    CHECK(t.cast == 0);
    CHECK(t.removed == 0);
    CHECK(t.race_turns + t.long_turns == 0);
}

TEST_CASE("Varianti: i conteggi di Teferi si sommano campo per campo", "[varianti]") {
    TeferiStats total{1, 2, 3, 4};
    total += TeferiStats{10, 20, 30, 40};
    CHECK(total.cast == 11);
    CHECK(total.race_turns == 22);
    CHECK(total.long_turns == 33);
    CHECK(total.removed == 44);
}

TEST_CASE("Varianti: Thoughtseize al primo turno di Rakdos e la carta presa, dal registro", "[varianti]") {
    SECTION("Rakdos in draw: il suo primo turno e' il turno 2 della partita") {
        HandLog h;
        h.begin_turn(0);
        h.move(1, kSeizeDef, Zone::Hand, Zone::Stack, Cause::Cast);  // non succede, ma il turno e' di UW
        h.begin_turn(1);
        h.move(1, kSeizeDef, Zone::Hand, Zone::Stack, Cause::Cast);
        h.move(0, kSolitudeDef, Zone::Hand, Zone::Graveyard, Cause::Discard);
        h.move(1, kSeizeDef, Zone::Stack, Zone::Graveyard, Cause::Resolve);
        const FirstTurnSeize f = first_turn_thoughtseize(h.log, kSeizeDef);
        CHECK(f.cast);
        CHECK(f.taken == kSolitudeDef);
    }
    SECTION("lanciata senza nulla da prendere") {
        HandLog h;
        h.begin_turn(1);
        h.move(1, kSeizeDef, Zone::Hand, Zone::Stack, Cause::Cast);
        h.move(1, kSeizeDef, Zone::Stack, Zone::Graveyard, Cause::Resolve);
        const FirstTurnSeize f = first_turn_thoughtseize(h.log, kSeizeDef);
        CHECK(f.cast);
        CHECK(f.taken == kNoDef);
    }
    SECTION("lanciata al secondo turno: non conta") {
        HandLog h;
        h.begin_turn(1);
        h.begin_turn(0);
        h.begin_turn(1);
        h.move(1, kSeizeDef, Zone::Hand, Zone::Stack, Cause::Cast);
        h.move(0, kSolitudeDef, Zone::Hand, Zone::Graveyard, Cause::Discard);
        const FirstTurnSeize f = first_turn_thoughtseize(h.log, kSeizeDef);
        CHECK_FALSE(f.cast);
        CHECK(f.taken == kNoDef);
    }
}

TEST_CASE("Varianti: in partite vere le varianti di Teferi non usano la modalita' esclusa", "[varianti][matchup]") {
    MatchupConfig c;
    c.db = &db_with_tokens();
    c.uw = uw_sim_deck();
    c.rakdos = rakdos_deck();
    const CardDefId teferi = db_with_tokens().id_of("Teferi, Time Raveler");
    TeferiStats ignore_total, remove_total;
    for (std::uint64_t seed = 1; seed <= 300 && (ignore_total.race_turns < 3 || remove_total.long_turns < 3); ++seed) {
        for (const int threshold : {RakdosAgentOptions::kTeferiAlwaysRace, 0}) {
            c.rakdos_agent.teferi_race_clock = threshold;
            GameLog log;
            std::map<std::size_t, std::string> rules;
            (void)play_matchup(c, seed, &log, &rules);
            const TeferiStats t = count_teferi(log, rules, teferi);
            CHECK(t.cast <= 1);  // una sola copia nella lista
            CHECK(t.removed <= t.cast);
            if (t.cast == 0) CHECK(t.race_turns + t.long_turns == 0);
            (threshold == 0 ? remove_total : ignore_total) += t;
        }
    }
    CHECK(ignore_total.long_turns == 0);  // ignora: sempre in corsa
    CHECK(remove_total.race_turns == 0);  // togli sempre: mai in corsa
    CHECK(ignore_total.race_turns >= 3);
    CHECK(remove_total.long_turns >= 3);
}

TEST_CASE("Varianti: in partite vere con X4 Thoughtseize al turno 1 prende carte", "[varianti][matchup]") {
    MatchupConfig c;
    c.db = &db_with_tokens();
    c.uw = uw_sim_deck();
    c.rakdos = rakdos_deck();
    c.rakdos_agent.t1_thoughtseize_first = true;
    const CardDefId seize = db_with_tokens().id_of("Thoughtseize");
    int taken = 0;
    for (std::uint64_t seed = 1; seed <= 200 && taken < 3; ++seed) {
        GameLog log;
        (void)play_matchup(c, seed, &log);
        const FirstTurnSeize f = first_turn_thoughtseize(log, seize);
        if (f.taken != kNoDef) {
            CHECK(f.cast);
            ++taken;
        }
    }
    CHECK(taken >= 3);
}

TEST_CASE("Varianti: in partite vere la ricarica pesca almeno una carta, una in piu per ogni Riddler", "[varianti][matchup]") {
    MatchupConfig c;
    c.db = &db_with_tokens();
    c.uw = uw_sim_deck();
    c.rakdos = rakdos_deck();
    RefillStats total;
    for (std::uint64_t seed = 1; seed <= 60 && total.casts < 3; ++seed) {
        GameLog log;
        std::map<std::size_t, std::string> rules;
        (void)play_matchup(c, seed, &log, &rules);
        const RefillStats r = count_refill_warps(log, rules);
        CHECK(r.cards >= r.casts);       // almeno la carta del trigger
        CHECK(r.cards <= 5 * r.casts);   // 1 piu' una carta per ogni Riddler in campo (4 copie)
        total.casts += r.casts;
        total.cards += r.cards;
    }
    CHECK(total.casts >= 1);
}

// ---------------------------------------------------------------------------
// Seguito dei test A/B di Rakdos (docs/agente_rakdos.md, sezione 21): gli osservatori della robustezza
// di X1 (prime giocate), della verifica del turno 1 e delle carte prese da Thoughtseize (X5).
// ---------------------------------------------------------------------------

namespace {

constexpr PlayerId kUwP = 0;
constexpr PlayerId kRakdosP = 1;

using Chooser = std::function<Action(const GameState&, const std::vector<Action>&)>;

// Gioca la partita preparata fino all'inizio del turno `turn`: UW passa sempre (agente del goldfish),
// Rakdos sceglie con `rakdos`. Ogni decisione passa prima dall'osservatore, come nel matchup.
void observe(Staged& st, MatchupObserver& obs, const Chooser& rakdos, std::uint16_t turn) {
    const GoldfishAgent fish;
    for (int guard = 0; guard < 3000 && st.s.turn.turn_number < turn && st.s.pending.kind != DecisionKind::GameOver;
         ++guard) {
        const std::vector<Action> actions = legal(st.s);
        const Action chosen = st.s.pending.player == kRakdosP ? rakdos(st.s, actions) : fish.choose(st.s, actions);
        obs.on_decision(st.s, actions, chosen);
        act(st, chosen);
    }
    REQUIRE(st.s.turn.turn_number >= turn);
    obs.on_end(st.s);
}

Chooser agent_chooser(RakdosAgent& agent) {
    return [&agent](const GameState& s, const std::vector<Action>& a) { return agent.choose(s, a); };
}

Chooser passive() {
    return [](const GameState& s, const std::vector<Action>& a) { return GoldfishAgent{}.choose(s, a); };
}

std::string card_name(const GameState& s, CardId c) { return db_with_tokens().get(s.objects[c].def).name; }

// Rakdos gioca solo nella prima fase principale dei suoi turni indicati (1 = il suo primo turno): la
// terra nel turno `land_turn`, un Mishra's Bauble (senza sacrificarlo) in ciascuno dei turni
// `bauble_turns`, il ciclo di Street Wraith nel turno `cycle_turn`.
Chooser scripted(int land_turn, std::vector<int> bauble_turns, int cycle_turn = 0) {
    return [land_turn, bauble_turns, cycle_turn](const GameState& s, const std::vector<Action>& actions) {
        const int t = s.turn.turn_number;
        const int mine = s.turn.starting_player == kRakdosP ? (t + 1) / 2 : t / 2;
        if (s.pending.kind == DecisionKind::Priority && s.turn.active_player == kRakdosP && s.stack.empty() &&
            s.turn.step == Step::Main1) {
            // Bauble consentiti finora: uno per ogni turno della lista gia' iniziato.
            const auto allowed = std::count_if(bauble_turns.begin(), bauble_turns.end(), [&](int x) { return x <= mine; });
            const auto in_play = std::count_if(s.battlefield.begin(), s.battlefield.end(), [&](CardId c) {
                return s.objects[c].controller == kRakdosP && card_name(s, c) == "Mishra's Bauble";
            });
            for (const Action& a : actions) {
                if (a.type == ActionType::PlayLand && mine == land_turn) return a;
                if (a.card == kNoCard || s.objects[a.card].zone != Zone::Hand) continue;
                if (a.type == ActionType::CastSpell && card_name(s, a.card) == "Mishra's Bauble" && in_play < allowed) {
                    return a;
                }
                if (a.type == ActionType::ActivateAbility && card_name(s, a.card) == "Street Wraith" && mine == cycle_turn) {
                    return a;
                }
            }
        }
        return GoldfishAgent{}.choose(s, actions);
    };
}

}  // namespace

TEST_CASE("Varianti: le prime giocate di Rakdos, terre entro il turno 2 e 3 e magie prima del turno 3",
          "[varianti][x1]") {
    SECTION("seconda terra nel terzo turno; un Bauble nel secondo (conta) e uno nel terzo (no)") {
        for (const PlayerId starting : {kRakdosP, kUwP}) {
            CAPTURE(starting);
            Staged st = staged(starting, {}, {"Swamp", "Mishra's Bauble", "Mishra's Bauble"}, {}, {"Swamp"});
            EarlyRakdosTracker tracker(db_with_tokens());
            const std::uint16_t end = starting == kRakdosP ? 7 : 8;  // l'inizio del quarto turno di Rakdos
            observe(st, tracker, scripted(3, {2, 3}), end);
            CHECK(tracker.stats().lands_by_turn2 == 1);
            CHECK(tracker.stats().lands_by_turn3 == 2);
            CHECK(tracker.stats().spells_before_turn3 == 1);
        }
    }
    SECTION("la seconda terra nel secondo turno conta per entrambe le scadenze") {
        Staged st = staged(kRakdosP, {}, {"Swamp"}, {}, {"Swamp"});
        EarlyRakdosTracker tracker(db_with_tokens());
        observe(st, tracker, scripted(2, {}), 7);
        CHECK(tracker.stats().lands_by_turn2 == 2);
        CHECK(tracker.stats().lands_by_turn3 == 2);
        CHECK(tracker.stats().spells_before_turn3 == 0);
    }
    SECTION("il ciclo di Street Wraith non e' una magia lanciata") {
        Staged st = staged(kRakdosP, {}, {"Street Wraith"}, {}, {"Swamp"});
        EarlyRakdosTracker tracker(db_with_tokens());
        observe(st, tracker, scripted(0, {}, 1), 7);
        CHECK(st.s.players[kRakdosP].life == 18);  // il ciclo e' avvenuto
        CHECK(tracker.stats().spells_before_turn3 == 0);
    }
    SECTION("con l'agente: Swamp e Thoughtseize al primo turno") {
        Staged st = staged(kRakdosP, {}, {"Swamp", "Thoughtseize"});
        RakdosAgent agent(db_with_tokens(), 3);
        EarlyRakdosTracker tracker(db_with_tokens());
        observe(st, tracker, agent_chooser(agent), 2);
        CHECK(tracker.stats().spells_before_turn3 == 1);
        CHECK(tracker.stats().lands_by_turn2 == 1);
    }
}

TEST_CASE("Varianti: la verifica del turno 1, Thoughtseize lanciata o il motivo per cui non lo e'",
          "[varianti][t1]") {
    auto check = [](Staged st, const Chooser& rakdos, SeizeT1 expected) {
        FirstTurnSeizeTracker tracker(db_with_tokens());
        observe(st, tracker, rakdos, 2);  // il primo turno di Rakdos
        CHECK(tracker.result() == expected);
        return tracker.detail();
    };
    RakdosAgent agent(db_with_tokens(), 3);
    SECTION("lanciata") { check(staged(kRakdosP, {}, {"Swamp", "Thoughtseize"}), agent_chooser(agent), SeizeT1::Cast); }
    SECTION("non in mano") {
        check(staged(kRakdosP, {}, {"Swamp", "Lightning Bolt"}), agent_chooser(agent), SeizeT1::NotInHand);
    }
    SECTION("una creatura da un mana lanciabile") {
        check(staged(kRakdosP, {}, {"Swamp", "Thoughtseize", "Stitcher's Supplier"}), agent_chooser(agent),
              SeizeT1::CreatureCastable);
    }
    SECTION("nessuna fonte nera: nessuna terra") {
        check(staged(kRakdosP, {}, {"Thoughtseize"}), agent_chooser(agent), SeizeT1::NoBlackSource);
    }
    SECTION("nessuna fonte nera: solo Raucous Theater, che entra tappata") {
        check(staged(kRakdosP, {}, {"Raucous Theater", "Thoughtseize"}), agent_chooser(agent), SeizeT1::NoBlackSource);
    }
    SECTION("soglia di vita: con 6 punti vita i 2 di Thoughtseize lascerebbero 4") {
        Staged st = staged(kRakdosP, {}, {"Swamp", "Thoughtseize"});
        st.s.players[kRakdosP].life = 6;
        check(std::move(st), agent_chooser(agent), SeizeT1::LifeFloor);
    }
    SECTION("senza motivo valido: lanciabile con la vita sopra la soglia, e non lanciata") {
        const std::string detail =
            check(staged(kRakdosP, {}, {"Thoughtseize"}, {}, {"Swamp"}), passive(), SeizeT1::NoValidReason);
        CHECK(detail.find("passa") != std::string::npos);  // l'azione scelta al posto di Thoughtseize
    }
    // Correzione del turno 1 (sezione 22): i due motivi validi nuovi.
    RakdosAgent r1(db_with_tokens(), 3, RakdosAgentOptions::base_r1());
    SECTION("motivo valido: ciclo del Troll senza un'altra terra in mano (base_r1 e base_r2)") {
        check(staged(kRakdosP, {}, {"Swamp", "Thoughtseize", "Troll of Khazad-dûm"}), agent_chooser(agent),
              SeizeT1::TrollNoSecondLand);
        check(staged(kRakdosP, {}, {"Swamp", "Thoughtseize", "Troll of Khazad-dûm"}), agent_chooser(r1),
              SeizeT1::TrollNoSecondLand);
    }
    SECTION("ciclo del Troll con un'altra terra in mano: base_r1 senza motivo valido, base_r2 lancia Thoughtseize") {
        const auto hand = {"Swamp", "Swamp", "Thoughtseize", "Troll of Khazad-dûm"};
        const std::string detail = check(staged(kRakdosP, {}, hand), agent_chooser(r1), SeizeT1::NoValidReason);
        CHECK(detail == "attiva Troll of Khazad-dûm");
        check(staged(kRakdosP, {}, hand), agent_chooser(agent), SeizeT1::Cast);
    }
    SECTION("motivo valido: Bolt a vista su Ocelot Pride o Guide of Souls") {
        for (const char* target : {"Ocelot Pride", "Guide of Souls"}) {
            CAPTURE(target);
            check(staged(kRakdosP, {}, {"Blackcleave Cliffs", "Thoughtseize", "Lightning Bolt"}, {target}),
                  agent_chooser(agent), SeizeT1::BoltOnSight);
        }
    }
    SECTION("Bolt senza Guide ne' Ocelot in campo non e' il motivo valido") {
        // Uno script lancia Bolt in faccia al posto di Thoughtseize: nessuna regola lo giustifica.
        const Chooser bolt_face = [](const GameState& s, const std::vector<Action>& actions) {
            for (const Action& a : actions) {
                if (a.type == ActionType::CastSpell && a.card != kNoCard && card_name(s, a.card) == "Lightning Bolt" &&
                    s.turn.step == Step::Main1) {
                    return a;
                }
                if (a.type == ActionType::TargetPlayer && a.value == kUwP) return a;
            }
            return GoldfishAgent{}.choose(s, actions);
        };
        check(staged(kRakdosP, {}, {"Thoughtseize", "Lightning Bolt"}, {"Phelia, Exuberant Shepherd"}, {"Blackcleave Cliffs"}),
              bolt_face, SeizeT1::NoValidReason);
    }
}

// La correzione del turno 1 sulle partite della verifica (sezione 21): i seed 665, 715, 1151 e 1248
// (ciclo del Troll) e 1821 (Bolt a vista), contro UW base_v2.
TEST_CASE("Varianti: correzione del turno 1 sui seed della verifica, il Troll e Bolt a vista", "[varianti][t1fix][matchup]") {
    MatchupConfig c;
    c.db = &db_with_tokens();
    c.uw = uw_sim_deck();
    c.rakdos = rakdos_deck();
    const CardDefId seize = db_with_tokens().id_of("Thoughtseize");
    RakdosAgentOptions fix = RakdosAgentOptions::base_r1();
    fix.t1_seize_before_troll = true;
    auto play = [&](std::uint64_t seed, const RakdosAgentOptions& o, std::map<std::size_t, std::string>& rules) {
        c.rakdos_agent = o;
        GameLog log;
        (void)play_matchup(c, seed, &log, &rules);
        return first_turn_thoughtseize(log, seize);
    };
    auto used = [](const std::map<std::size_t, std::string>& rules, std::string_view rule) {
        return std::any_of(rules.begin(), rules.end(), [&](const auto& e) { return e.second == rule; });
    };
    for (const std::uint64_t seed : {665ULL, 715ULL, 1151ULL, 1248ULL}) {
        CAPTURE(seed);
        std::map<std::size_t, std::string> before, after;
        CHECK_FALSE(play(seed, RakdosAgentOptions::base_r1(), before).cast);  // base_r1: il ciclo prende il mana
        CHECK(used(before, "Rakdos: ciclo del Troll (serve una terra)"));
        CHECK(play(seed, fix, after).cast);  // con la correzione: Thoughtseize al primo turno
        CHECK(used(after, "Rakdos: Thoughtseize: turno 1 prima del ciclo del Troll"));
    }
    SECTION("seed 1821: Bolt su Guide o Ocelot a vista resta prima di Thoughtseize, con e senza la correzione") {
        for (const RakdosAgentOptions& o : {RakdosAgentOptions::base_r1(), fix}) {
            std::map<std::size_t, std::string> rules;
            CHECK_FALSE(play(1821, o, rules).cast);
            CHECK(used(rules, "Rakdos: Bolt: Ocelot Pride o Guide of Souls a vista"));
            CHECK_FALSE(used(rules, "Rakdos: Thoughtseize: turno 1 prima del ciclo del Troll"));
        }
    }
}

TEST_CASE("Varianti: le carte prese da Thoughtseize e le partite in cui X5 sceglie diversamente dalla base",
          "[varianti][x5]") {
    auto run = [](std::initializer_list<const char*> uw_hand, RakdosAgentOptions o) {
        Staged st = staged(kRakdosP, uw_hand, {"Thoughtseize"}, {"Plains"}, {"Swamp"});
        st.s.turn.turn_number = 3;  // secondo turno di Rakdos: Thoughtseize prima di tutto
        RakdosAgent agent(db_with_tokens(), 3, o);
        SeizePickTracker tracker(db_with_tokens());
        observe(st, tracker, agent_chooser(agent), 4);
        return tracker;
    };
    RakdosAgentOptions x5;
    x5.thoughtseize_answer_or_threat = true;
    const CardDefId teferi = db_with_tokens().id_of("Teferi, Time Raveler");
    const CardDefId guide = db_with_tokens().id_of("Guide of Souls");
    SECTION("Teferi e Guide: X5 prende Guide, la base Teferi; la scelta cambia solo con X5") {
        const SeizePickTracker t = run({"Teferi, Time Raveler", "Guide of Souls"}, x5);
        REQUIRE(t.picks().contains(guide));
        CHECK(t.picks().at(guide) == 1);
        CHECK(t.picks().size() == 1);
        CHECK(t.changed());
        const SeizePickTracker b = run({"Teferi, Time Raveler", "Guide of Souls"}, RakdosAgentOptions::base_r1());
        REQUIRE(b.picks().contains(teferi));
        CHECK(b.picks().at(teferi) == 1);
        CHECK_FALSE(b.changed());
    }
    SECTION("Sanctifier e Solitude senza Flare: la stessa scelta della base") {
        const SeizePickTracker t = run({"Sanctifier en-Vec", "Solitude"}, x5);
        CHECK(t.picks().size() == 1);
        CHECK(t.picks().contains(db_with_tokens().id_of("Sanctifier en-Vec")));
        CHECK_FALSE(t.changed());
    }
}

// Variante X6 (sezione 23) su partite vere contro UW base_v2: i seed 13 (Rakdos in draw) e 30 (in play),
// trovati con varianti_rakdos --x6. base_r2 cicla il Troll al primo turno; con X6 (predefinita da base_r3)
// lancia la creatura, e la regola di X6 non compare mai senza l'opzione.
TEST_CASE("Varianti: X6 sui seed 13 e 30, la creatura invece del ciclo del Troll", "[varianti][x6][matchup]") {
    MatchupConfig c;
    c.db = &db_with_tokens();
    c.uw = uw_sim_deck();
    c.rakdos = rakdos_deck();
    const RakdosAgentOptions x6;  // base_r3
    auto first_rule_of = [&](std::uint64_t seed, const RakdosAgentOptions& o, std::string_view a, std::string_view b) {
        c.rakdos_agent = o;
        GameLog log;
        std::map<std::size_t, std::string> rules;
        (void)play_matchup(c, seed, &log, &rules);
        for (const auto& [index, rule] : rules) {
            if (rule == a || rule == b) return rule;
        }
        return std::string{};
    };
    const std::string cycle = "Rakdos: ciclo del Troll (serve una terra)";
    const std::string creature = "Rakdos: creatura: turno 1 prima del ciclo del Troll (variante X6)";
    for (const std::uint64_t seed : {13ULL, 30ULL}) {
        CAPTURE(seed);
        CHECK(first_rule_of(seed, RakdosAgentOptions::base_r2(), cycle, creature) == cycle);
        CHECK(first_rule_of(seed, x6, cycle, creature) == creature);
    }
}

// Secondo scenario indipendente della rimovibilita' di X5 (la mutazione che la ignora faceva fallire solo
// due casi di test, entrambi nei test dell'agente): qui l'osservatore delle scelte di Thoughtseize. Con
// Bolt in mano Phelia (2/2) e' rimovibile, Quantum Riddler (4/6) no: X5 prende Riddler; base_r1 prende
// Phelia, che nel suo ordine viene prima di Riddler. La scelta cambia solo con X5.
TEST_CASE("Varianti: con Bolt in mano X5 prende Riddler e non Phelia, la base Phelia", "[varianti][x5]") {
    auto run = [](RakdosAgentOptions o) {
        Staged st = staged(kRakdosP, {"Phelia, Exuberant Shepherd", "Quantum Riddler", "Plains"},
                           {"Thoughtseize", "Lightning Bolt"}, {"Plains"}, {"Swamp"});
        st.s.turn.turn_number = 3;  // secondo turno di Rakdos: Thoughtseize prima di tutto
        RakdosAgent agent(db_with_tokens(), 3, o);
        SeizePickTracker tracker(db_with_tokens());
        observe(st, tracker, agent_chooser(agent), 4);
        return tracker;
    };
    const CardDefId riddler = db_with_tokens().id_of("Quantum Riddler");
    const CardDefId phelia = db_with_tokens().id_of("Phelia, Exuberant Shepherd");
    RakdosAgentOptions x5 = RakdosAgentOptions::base_r1();
    x5.thoughtseize_answer_or_threat = true;
    for (const RakdosAgentOptions& o : {x5, RakdosAgentOptions{}}) {
        const SeizePickTracker t = run(o);
        CHECK(t.picks().size() == 1);
        CHECK(t.picks().contains(riddler));
        CHECK(t.changed());
    }
    const SeizePickTracker b = run(RakdosAgentOptions::base_r1());
    CHECK(b.picks().size() == 1);
    CHECK(b.picks().contains(phelia));
    CHECK_FALSE(b.changed());
}

TEST_CASE("Varianti: gli osservatori nuovi non cambiano la partita", "[varianti][matchup]") {
    MatchupConfig c;
    c.db = &db_with_tokens();
    c.uw = uw_sim_deck();
    c.rakdos = rakdos_deck();
    c.rakdos_agent = RakdosAgentOptions::base_r1();  // la base contro se stessa: X5 spenta
    for (std::uint64_t seed = 1; seed <= 4; ++seed) {
        GameLog without, with1, with2, with3;
        const MatchupGame a = play_matchup(c, seed, &without);
        EarlyRakdosTracker early(db_with_tokens());
        FirstTurnSeizeTracker t1(db_with_tokens());
        SeizePickTracker picks(db_with_tokens());
        const MatchupGame b = play_matchup(c, seed, &with1, nullptr, nullptr, nullptr, &early);
        const MatchupGame d = play_matchup(c, seed, &with2, nullptr, nullptr, nullptr, &t1);
        const MatchupGame e = play_matchup(c, seed, &with3, nullptr, nullptr, nullptr, &picks);
        CHECK(matchup_game_json(a) == matchup_game_json(b));
        CHECK(matchup_game_json(a) == matchup_game_json(d));
        CHECK(matchup_game_json(a) == matchup_game_json(e));
        CHECK(without.entries == with1.entries);
        CHECK(without.entries == with2.entries);
        CHECK(without.entries == with3.entries);
        CHECK_FALSE(picks.changed());  // la base contro se stessa
    }
}

// --- Sezione 29 di docs/agente_uw.md: la mano tenuta da UW e Witch Enchanter come terra -------------
// Oracle (Scryfall oracle_cards del 9/11/2026):
//   Witch Enchanter // Witch-Blessed Meadow: "As this land enters, you may pay 3 life. If you don't, it enters
//     tapped. {T}: Add {W}."
//   Hallowed Fountain: "As this land enters, you may pay 2 life. If you don't, it enters tapped."
//   Arid Mesa: "{T}, Pay 1 life, Sacrifice this land: Search your library for a Mountain or Plains card, put it
//     onto the battlefield, then shuffle."
//   Recruiter of the Guard {2}{W}; Guide of Souls {W}; Force of Negation {1}{U}{U}, Instant.

namespace {

// UW gioca solo nella prima fase principale del suo primo turno: la terra `land` (il retro se `back`), poi
// paga la vita se `pay`; la fetch si rompe e cerca la prima carta. Il resto come il goldfish.
Chooser uw_land_script(std::string land, bool back, bool pay) {
    return [land, back, pay](const GameState& s, const std::vector<Action>& actions) {
        for (const Action& a : actions) {
            if (s.pending.kind == DecisionKind::Priority && s.turn.turn_number == 1 && s.turn.step == Step::Main1 &&
                a.type == ActionType::PlayLand && card_name(s, a.card) == land && a.value == (back ? 1 : 0)) {
                return a;
            }
            if (s.pending.kind == DecisionKind::Priority && a.type == ActionType::ActivateAbility &&
                s.objects[a.card].zone == Zone::Battlefield && card_name(s, a.card) == "Arid Mesa") {
                return a;
            }
            if (s.pending.kind == DecisionKind::PayLifeOrTapped && a.type == ActionType::ChooseOption &&
                a.value == (pay ? kPayLife : kEnterTapped)) {
                return a;
            }
            if (s.pending.kind == DecisionKind::SearchLibrary && a.type == ActionType::ChooseCard) return a;
        }
        return GoldfishAgent{}.choose(s, actions);
    };
}

// Il primo turno di UW (in play) con lo script per UW; Rakdos passa.
WitchLandStats witch_turn(std::initializer_list<const char*> hand, std::initializer_list<const char*> field,
                          const Chooser& uw, GameLog* log = nullptr) {
    Staged st = staged(kUwP, hand, {}, field, {});
    WitchLandTracker tracker(db_with_tokens());
    const GoldfishAgent fish;
    for (int guard = 0; guard < 3000 && st.s.turn.turn_number < 2 && st.s.pending.kind != DecisionKind::GameOver; ++guard) {
        const std::vector<Action> actions = legal(st.s);
        const Action chosen = st.s.pending.player == kUwP ? uw(st.s, actions) : fish.choose(st.s, actions);
        tracker.on_decision(st.s, actions, chosen);
        act(st, chosen);
    }
    REQUIRE(st.s.turn.turn_number >= 2);
    tracker.on_end(st.s);
    if (log != nullptr) *log = st.log;
    return tracker.stats();
}

}  // namespace

TEST_CASE("Varianti: Witch-Blessed Meadow giocata e pagata, D-1 e vita pagata per le terre", "[varianti][witch]") {
    SECTION("con Recruiter (3) e una terra in campo: la giocata soddisfa D-1, il pagamento no") {
        GameLog log;
        const WitchLandStats w =
            witch_turn({"Witch Enchanter", "Recruiter of the Guard"}, {"Plains"}, uw_land_script("Witch Enchanter", true, true), &log);
        CHECK(w.plays == 1);
        CHECK(w.paid == 1);
        CHECK(w.tapped == 0);
        CHECK(w.plays_not_d1 == 0);
        CHECK(w.payments_not_d1 == 1);  // Recruiter costa 3: con due terre non si lancia comunque in questo turno
        CHECK(w.lowest_life_after_payment == 17);
        CHECK(land_life_paid(log, db_with_tokens(), kUwP) == 3);
    }
    SECTION("con Guide of Souls e nessuna terra in campo: giocata e pagamento soddisfano D-1") {
        const WitchLandStats w = witch_turn({"Witch Enchanter", "Guide of Souls"}, {}, uw_land_script("Witch Enchanter", true, true));
        CHECK(w.plays == 1);
        CHECK(w.paid == 1);
        CHECK(w.plays_not_d1 == 0);
        CHECK(w.payments_not_d1 == 0);
    }
    SECTION("con un'altra terra in mano: la giocata non soddisfa D-1 (a); tappata, nessun pagamento") {
        GameLog log;
        const WitchLandStats w = witch_turn({"Witch Enchanter", "Plains", "Guide of Souls"}, {},
                                            uw_land_script("Witch Enchanter", true, false), &log);
        CHECK(w.plays == 1);
        CHECK(w.paid == 0);
        CHECK(w.tapped == 1);
        CHECK(w.plays_not_d1 == 1);
        CHECK(w.payments_not_d1 == 0);
        CHECK(w.lowest_life_after_payment == WitchLandStats::kNoPayment);
        CHECK(land_life_paid(log, db_with_tokens(), kUwP) == 0);
    }
    SECTION("solo un'istantanea in mano: nessuna giocata prevista, la giocata non soddisfa D-1 (b)") {
        const WitchLandStats w =
            witch_turn({"Witch Enchanter", "Force of Negation"}, {"Plains"}, uw_land_script("Witch Enchanter", true, false));
        CHECK(w.plays == 1);
        CHECK(w.plays_not_d1 == 1);
    }
    SECTION("Witch Enchanter non giocata: nulla") {
        const WitchLandStats w = witch_turn({"Witch Enchanter", "Plains"}, {}, uw_land_script("Plains", false, false));
        CHECK(w.plays == 0);
        CHECK(w.paid == 0);
        CHECK(w.tapped == 0);
    }
    SECTION("Hallowed Fountain pagata e una fetch: 2 + 1 vita per le terre, nessuna Witch") {
        GameLog fountain, fetch;
        const WitchLandStats a = witch_turn({"Hallowed Fountain"}, {}, uw_land_script("Hallowed Fountain", false, true), &fountain);
        CHECK(a.plays == 0);
        CHECK(a.paid == 0);  // conta solo Witch-Blessed Meadow
        CHECK(land_life_paid(fountain, db_with_tokens(), kUwP) == 2);
        (void)witch_turn({"Arid Mesa"}, {}, uw_land_script("Arid Mesa", false, false), &fetch);
        CHECK(land_life_paid(fetch, db_with_tokens(), kUwP) == 1);
        CHECK(land_life_paid(fetch, db_with_tokens(), kRakdosP) == 0);
    }
}

TEST_CASE("Varianti: la mano tenuta da UW dopo i mulligan", "[varianti][m1]") {
    const auto seven = {"Plains", "Hallowed Fountain", "Guide of Souls", "Recruiter of the Guard", "Consign to Memory",
                        "Ephemerate", "Quantum Riddler"};
    auto kept_with = [&](const UwAgentOptions& o) {
        GameState s = new_game(3);
        for (const char* name : seven) {  // le sette carte in cima al grimorio di UW
            const CardId id = find_cards(s, kUwP, Zone::Library, name)[0].card;
            auto& lib = s.players[kUwP].library;
            lib.erase_at(lib.find(id));
            lib.push_back(id);
        }
        start_game(s, db_with_tokens(), StartOptions{kUwP, true});
        UwAgent uw(db_with_tokens(), 5, o);
        KeptHandTracker tracker(kUwP);
        for (int guard = 0; guard < 200 && s.turn.turn_number < 2 && s.pending.kind != DecisionKind::GameOver; ++guard) {
            const std::vector<Action> actions = legal(s);
            const Action chosen = s.pending.player == kUwP ? uw.choose(s, actions) : GoldfishAgent{}.choose(s, actions);
            tracker.on_decision(s, actions, chosen);
            apply(s, db_with_tokens(), chosen);
        }
        tracker.on_end(s);
        return tracker;
    };
    UwAgentOptions m1;
    m1.mulligan_more_answers = true;
    const KeptHandTracker keep = kept_with(m1);  // M1 tiene la mano con Recruiter
    REQUIRE(keep.kept().size() == 7);
    std::vector<std::string> names;
    for (const CardDefId d : keep.kept()) names.push_back(db_with_tokens().get(d).name);
    std::sort(names.begin(), names.end());
    std::vector<std::string> expected(seven.begin(), seven.end());
    std::sort(expected.begin(), expected.end());
    CHECK(names == expected);
    UwAgentOptions v2;
    const KeptHandTracker mull = kept_with(v2);  // base_v2 fa mulligan: tiene 6 carte (o meno)
    CHECK(mull.kept().size() <= 6);
    CHECK_FALSE(mull.kept().empty());
}

TEST_CASE("Varianti: gli osservatori di UW non cambiano la partita", "[varianti][matchup][witch]") {
    MatchupConfig c;
    c.db = &db_with_tokens();
    c.uw = uw_sim_deck();
    c.rakdos = rakdos_deck();
    for (std::uint64_t seed = 1; seed <= 4; ++seed) {
        GameLog without, with1, with2;
        const MatchupGame a = play_matchup(c, seed, &without);
        WitchLandTracker witch(db_with_tokens());
        KeptHandTracker kept(kUwP);
        const MatchupGame b = play_matchup(c, seed, &with1, nullptr, nullptr, nullptr, &witch);
        const MatchupGame d = play_matchup(c, seed, &with2, nullptr, nullptr, nullptr, &kept);
        CHECK(matchup_game_json(a) == matchup_game_json(b));
        CHECK(matchup_game_json(a) == matchup_game_json(d));
        CHECK(without.entries == with1.entries);
        CHECK(without.entries == with2.entries);
        CHECK(kept.kept().size() == static_cast<std::size_t>(7 - a.uw_mulligans));
    }
}
