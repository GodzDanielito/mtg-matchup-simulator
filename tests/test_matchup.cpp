// Test dello strumento del matchup (tools/matchup.hpp): agente di UW contro agente di Rakdos.

#include "matchup.hpp"
#include "test_helpers.hpp"

#include <algorithm>
#include <set>
#include <string>

using namespace mtg;
using namespace test;

namespace {

MatchupConfig config() {
    MatchupConfig c;
    c.db = &db_with_tokens();
    c.uw = uw_sim_deck();
    c.rakdos = rakdos_deck();
    return c;
}

}  // namespace

TEST_CASE("Matchup: UW in play con il seed dispari, in draw con il pari", "[matchup]") {
    const MatchupConfig c = config();
    CHECK(play_matchup(c, 1).uw_on_play);
    CHECK_FALSE(play_matchup(c, 2).uw_on_play);
}

TEST_CASE("Matchup: stesso seed, stessa partita", "[matchup]") {
    const MatchupConfig c = config();
    CHECK(matchup_game_json(play_matchup(c, 7)) == matchup_game_json(play_matchup(c, 7)));
}

TEST_CASE("Matchup: ogni partita finisce con un esito e il suo motivo", "[matchup]") {
    const MatchupConfig c = config();
    const std::set<std::string> endings = {"vita a zero", "grimorio vuoto", "patta", "limite di turni"};
    for (std::uint64_t seed = 1; seed <= 12; ++seed) {
        const MatchupGame g = play_matchup(c, seed);
        INFO("seed " << seed);
        CHECK(endings.contains(g.ending));
        CHECK((g.winner == kNoPlayer) == (g.ending == "patta" || g.ending == "limite di turni"));
        if (g.ending == "vita a zero") CHECK((g.winner == kMatchupUw ? g.rakdos_life : g.uw_life) <= 0);
        CHECK(g.uw_decisions > 0);
        CHECK(g.rakdos_decisions > 0);
        CHECK(g.uw_defaults == 0);
    }
}

TEST_CASE("Matchup: il registro ha la regola di ogni decisione, con il giocatore", "[matchup]") {
    const MatchupConfig c = config();
    GameLog log;
    std::map<std::size_t, std::string> rules;
    std::map<std::string, std::uint64_t> uw_rules, rakdos_rules;
    const MatchupGame g = play_matchup(c, 3, &log, &rules, &uw_rules, &rakdos_rules);
    CHECK(rules.size() == g.uw_decisions + g.rakdos_decisions);
    bool uw = false, rakdos = false;
    for (const auto& [index, rule] : rules) {
        REQUIRE(index < log.entries.size());
        CHECK(log.entries[index].kind == LogEntry::Kind::Action);
        uw = uw || rule.starts_with("UW: ");
        rakdos = rakdos || rule.starts_with("Rakdos: ");
    }
    CHECK(uw);
    CHECK(rakdos);
    std::uint64_t counted = 0;
    for (const auto& [rule, n] : uw_rules) counted += n;
    CHECK(counted == g.uw_decisions);
}

TEST_CASE("Matchup: le dieci partite per i replay leggibili, cinque in play e cinque in draw", "[matchup][leggibile]") {
    std::vector<MatchupGame> games;
    for (std::uint64_t seed = 1; seed <= 100; ++seed) {
        MatchupGame g;
        g.seed = seed;
        g.uw_on_play = seed % 2 == 1;
        g.winner = seed % 3 == 0 ? kMatchupUw : kMatchupRakdos;
        games.push_back(g);
    }
    const std::vector<std::uint64_t> seeds = pick_replay_seeds(games);
    REQUIRE(seeds.size() == 10);
    CHECK(std::is_sorted(seeds.begin(), seeds.end()));
    int uw_wins_play = 0, uw_wins_draw = 0, rakdos_wins_play = 0, rakdos_wins_draw = 0;
    for (const std::uint64_t seed : seeds) {
        const MatchupGame& g = games[seed - 1];
        if (g.winner == kMatchupUw) (g.uw_on_play ? uw_wins_play : uw_wins_draw)++;
        else (g.uw_on_play ? rakdos_wins_play : rakdos_wins_draw)++;
    }
    CHECK(uw_wins_play == 3);
    CHECK(uw_wins_draw == 2);
    CHECK(rakdos_wins_play == 2);
    CHECK(rakdos_wins_draw == 3);
    // Le prime di ogni gruppo: dispari e multipli di 3 sono UW in play e vincitrice (3, 9, 15).
    CHECK(std::count(seeds.begin(), seeds.end(), 3) == 1);
    CHECK(std::count(seeds.begin(), seeds.end(), 9) == 1);
    CHECK(std::count(seeds.begin(), seeds.end(), 15) == 1);
    CHECK(std::count(seeds.begin(), seeds.end(), 21) == 0);
}

TEST_CASE("Matchup: con poche partite si scelgono quelle che ci sono", "[matchup][leggibile]") {
    std::vector<MatchupGame> games(2);
    games[0].seed = 1;
    games[0].uw_on_play = true;
    games[0].winner = kMatchupUw;
    games[1].seed = 2;
    games[1].winner = kMatchupRakdos;
    const std::vector<std::uint64_t> seeds = pick_replay_seeds(games);
    CHECK(seeds == std::vector<std::uint64_t>{1, 2});
}
