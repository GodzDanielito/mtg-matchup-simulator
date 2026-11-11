// Test dello strumento del matchup (tools/matchup.hpp): agente di UW contro agente di Rakdos.

#include "matchup.hpp"
#include "test_helpers.hpp"

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
