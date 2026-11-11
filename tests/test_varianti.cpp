// Test dei conti del confronto di varianti (tools/varianti.hpp): differenza appaiata e ricariche di
// Quantum Riddler lette dal registro.

#include "matchup.hpp"
#include "test_helpers.hpp"
#include "varianti.hpp"

#include <catch2/catch_approx.hpp>

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
