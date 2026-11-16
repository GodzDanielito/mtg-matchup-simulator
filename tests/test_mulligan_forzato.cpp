// Test dell'aggancio del mulligan e delle coppie K/M (docs/metriche.md, sezioni 9.2 e 11; passo 4).
//
// L'aggancio forza una decisione di mulligan di un giocatore senza cambiare gli agenti: l'agente sceglie comunque,
// poi la scelta viene sostituita. Senza aggancio, o con l'aggancio che forza la scelta che l'agente farebbe, i
// registri sono identici; forzando la scelta opposta cambia solo da quella decisione in poi.

#include "matchup.hpp"
#include "metriche.hpp"
#include "test_helpers.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

using namespace mtg;
using namespace test;

namespace {

constexpr PlayerId kU = 0;  // UW
constexpr PlayerId kR = 1;  // Rakdos

MatchupConfig v1_config() {
    MatchupConfig c;
    c.db = &db_with_tokens();
    c.uw = uw_sim_deck();
    c.rakdos = rakdos_deck();
    return c;
}

// Indice della n-esima decisione di mulligan (1 = la prima) di `p` nel registro; entries.size() se non c'e'.
std::size_t mulligan_entry(const GameLog& log, PlayerId p, int n) {
    for (std::size_t i = 0; i < log.entries.size(); ++i) {
        const LogEntry& e = log.entries[i];
        if (e.kind == LogEntry::Kind::Action && e.player == p && e.decision == DecisionKind::Mulligan && --n == 0) {
            return i;
        }
    }
    return log.entries.size();
}

bool is_keep(const LogEntry& e) { return e.action.type == ActionType::Keep; }

}  // namespace

TEST_CASE("Mulligan forzato: senza aggancio, o con un aggancio che non scatta, registri identici", "[mulligan][matchup]") {
    const MatchupConfig base = v1_config();
    for (std::uint64_t seed = 1; seed <= 6; ++seed) {
        GameLog la, lb, lc;
        const MatchupGame a = play_matchup(base, seed, &la);
        MatchupConfig off = v1_config();
        off.forced_mulligan.player = kNoPlayer;
        off.forced_mulligan.keep = false;  // nessun giocatore: il valore non conta
        const MatchupGame b = play_matchup(off, seed, &lb);
        // La decima decisione di mulligan non arriva mai: l'aggancio non scatta.
        MatchupConfig never = v1_config();
        never.forced_mulligan = ForcedMulligan{kU, 10, false};
        const MatchupGame c = play_matchup(never, seed, &lc);
        CHECK(matchup_game_json(a) == matchup_game_json(b));
        CHECK(matchup_game_json(a) == matchup_game_json(c));
        CHECK(la.entries == lb.entries);
        CHECK(la.entries == lc.entries);
        CHECK_FALSE(a.forced_applied);
        CHECK_FALSE(c.forced_applied);
    }
}

TEST_CASE("Mulligan forzato: forzare la scelta che l'agente farebbe lascia la partita identica", "[mulligan][matchup]") {
    const MatchupConfig base = v1_config();
    int second_decisions = 0;
    for (std::uint64_t seed = 1; seed <= 30; ++seed) {
        GameLog ref;
        const MatchupGame g = play_matchup(base, seed, &ref);
        for (PlayerId p = 0; p < kNumPlayers; ++p) {
            for (int n = 1; n <= 2; ++n) {
                const std::size_t i = mulligan_entry(ref, p, n);
                if (i == ref.entries.size()) continue;
                second_decisions += n == 2;
                INFO("seed " << seed << ", giocatore " << int(p) << ", decisione " << n);
                MatchupConfig c = v1_config();
                c.forced_mulligan = ForcedMulligan{p, n, is_keep(ref.entries[i])};
                GameLog log;
                const MatchupGame f = play_matchup(c, seed, &log);
                CHECK(matchup_game_json(f) == matchup_game_json(g));
                CHECK(log.entries == ref.entries);
                CHECK(f.forced_applied);
                CHECK(f.forced_agent_keep == is_keep(ref.entries[i]));
                CHECK_FALSE(f.forced_agent_rule.empty());
            }
        }
    }
    CHECK(second_decisions >= 5);
}

TEST_CASE("Mulligan forzato: la scelta opposta cambia la partita da quella decisione", "[mulligan][matchup]") {
    const MatchupConfig base = v1_config();
    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
        GameLog ref;
        play_matchup(base, seed, &ref);
        for (PlayerId p = 0; p < kNumPlayers; ++p) {
            INFO("seed " << seed << ", giocatore " << int(p));
            const std::size_t i = mulligan_entry(ref, p, 1);
            REQUIRE(i < ref.entries.size());
            const bool agent_keep = is_keep(ref.entries[i]);
            MatchupConfig c = v1_config();
            c.forced_mulligan = ForcedMulligan{p, 1, !agent_keep};
            GameLog log;
            const MatchupGame f = play_matchup(c, seed, &log);
            REQUIRE(log.entries.size() > i);
            for (std::size_t k = 0; k < i; ++k) CHECK(log.entries[k] == ref.entries[k]);
            CHECK(is_keep(log.entries[i]) == !agent_keep);
            CHECK(f.forced_applied);
            CHECK(f.forced_agent_keep == agent_keep);
            const int mulligans = p == kU ? f.uw_mulligans : f.rakdos_mulligans;
            if (!agent_keep) CHECK(mulligans == 0);  // forzata a tenere la 7
            if (agent_keep) CHECK(mulligans >= 1);
        }
    }
}

TEST_CASE("Mulligan forzato: K e M per seed, una delle due e' la partita normale", "[mulligan][matchup]") {
    const MatchupConfig base = v1_config();
    for (const PlayerId player : {kU, kR}) {
        for (std::uint64_t seed = 1; seed <= 12; ++seed) {
            INFO("seed " << seed << ", giocatore " << int(player));
            GameLog log;
            const MatchupGame g = play_matchup(base, seed, &log);
            const CompactRecord r = build_compact_record(db_with_tokens(), g, log);
            const MulliganPair m = play_mulligan_pair(base, seed, player, true);
            CHECK(m.player == player);
            CHECK(m.seed == seed);
            CHECK(m.first_hand == r.players[player].hands[0]);
            REQUIRE(m.agent7.present);
            CHECK(m.agent7.keep == (r.players[player].mulligans == 0));
            const MatchupGame& same = m.agent7.keep ? m.keep_game : m.mull_game;
            CHECK(matchup_game_json(same) == matchup_game_json(g));
            const CompactRecord& same_record = m.agent7.keep ? m.keep_record : m.mull_record;
            CHECK(compact_record_json(db_with_tokens(), same_record) == compact_record_json(db_with_tokens(), r));
            CHECK((player == kU ? m.keep_game.uw_mulligans : m.keep_game.rakdos_mulligans) == 0);
            CHECK((player == kU ? m.mull_game.uw_mulligans : m.mull_game.rakdos_mulligans) >= 1);
            // La scelta a 6 carte nella partita M e' quella dell'agente (la partita normale se rifiuta la 7).
            const int m_mulligans = player == kU ? m.mull_game.uw_mulligans : m.mull_game.rakdos_mulligans;
            REQUIRE(m.agent6.present);
            CHECK(m.agent6.keep == (m_mulligans == 1));
            CHECK(m.probes.empty() == (player == kR));
            const auto line = nlohmann::json::parse(mulligan_pair_json(db_with_tokens(), m));
            CHECK(line["seed"] == seed);
            CHECK(line["giocatore"] == (player == kU ? "uw" : "rakdos"));
            CHECK(line["mano"].size() == 7);
            CHECK(line["agente"]["7"]["tieni"] == m.agent7.keep);
            CHECK(line["K"]["vincitore"].is_string());
            CHECK(line["M"]["vincitore"].is_string());
        }
    }
}

TEST_CASE("Mulligan forzato: le sonde delle varianti e le loro partite", "[mulligan][matchup]") {
    const MatchupConfig base = v1_config();
    const std::vector<MulliganProbe> probes = uw_mulligan_probes(base.uw_agent);
    REQUIRE(probes.size() == 3);
    CHECK(probes[0].name == "m1");
    CHECK(probes[0].options.mulligan_more_answers);
    CHECK(probes[1].options.mulligan_recruiter_answer);
    CHECK(probes[2].options.mulligan_recruiter_answer_on_play);
    int changed7 = 0, changed6 = 0;
    for (std::uint64_t seed = 1; seed <= 150; ++seed) {
        INFO("seed " << seed);
        const MulliganPair m = play_mulligan_pair(base, seed, kU, false);
        REQUIRE(m.probes.size() == 3);
        for (const auto& [name, choice] : m.probes) {
            REQUIRE(choice[0].present);
            // Le varianti aggiungono solo risposte: tengono ogni mano che D-2 tiene.
            if (m.agent7.keep) CHECK(choice[0].keep);
        }
        for (const MulliganVariantGame& v : m.variants) {
            INFO("variante " << v.name << " a " << v.changed_at << " carte");
            if (v.changed_at == 7) {
                ++changed7;
                // La variante cambia solo il mulligan: la sua partita e' K, registro compreso.
                CHECK(v.equals_pair_game);
                CHECK(matchup_game_json(v.game) == matchup_game_json(m.keep_game));
                CHECK(v.kept_hand.size() == 7);
            } else {
                REQUIRE(v.changed_at == 6);
                ++changed6;
                CHECK_FALSE(m.agent6.keep);
                CHECK(v.game.uw_mulligans == 1);
                CHECK(v.kept_hand.size() == 6);
            }
        }
    }
    CHECK(changed7 >= 5);
    // Le varianti che cambiano solo la decisione a 6 carte sono rare (nessuna nei primi 150 seed): il seed 258 e'
    // il primo, trovato con metriche.exe --mulligan-forzato uw.
    const MulliganPair m = play_mulligan_pair(base, 258, kU, false);
    for (const MulliganVariantGame& v : m.variants) {
        if (v.changed_at != 6) continue;
        ++changed6;
        CHECK_FALSE(m.agent7.keep);
        CHECK_FALSE(m.agent6.keep);
        CHECK(v.game.uw_mulligans == 1);
        CHECK(v.kept_hand.size() == 6);
    }
    CHECK(changed6 >= 1);
}
