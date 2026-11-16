// Un agente per lato del matchup, scelto dalla configurazione (tools/giocatori.hpp; docs/ricerca.md, passo 1).

#include "matchup.hpp"
#include "player_view.hpp"
#include "rakdos_agent.hpp"
#include "test_helpers.hpp"
#include "uw_agent.hpp"

#include <map>
#include <span>
#include <string_view>
#include <memory>
#include <string>
#include <vector>

using namespace mtg;
using namespace test;

namespace {

MatchupConfig matchup_config() {
    MatchupConfig c;
    c.db = &db_with_tokens();
    c.uw = uw_sim_deck();
    c.rakdos = rakdos_deck();
    return c;
}

// Il ciclo di play_matchup com'era prima dell'interfaccia: UwAgent e RakdosAgent costruiti e chiamati direttamente.
struct OldGame {
    GameLog log;
    std::map<std::size_t, std::string> rules;
    std::map<std::string, std::uint64_t> uw_rules, rakdos_rules;
    PlayerId winner = kNoPlayer;
    int turns = 0;
    std::uint64_t uw_defaults = 0, rakdos_defaults = 0;
};

OldGame old_play(const MatchupConfig& config, std::uint64_t seed) {
    const CardDatabase& db = *config.db;
    OldGame g;
    GameState s = create_game(db, config.uw, config.rakdos, seed);
    start_game(s, db, StartOptions{seed % 2 == 1 ? kMatchupUw : kMatchupRakdos, true}, &g.log);
    UwAgent uw(db, seed, config.uw_agent);
    RakdosAgent rakdos(db, seed, config.rakdos_agent);
    while (s.pending.kind != DecisionKind::GameOver && s.turn.turn_number <= config.turn_limit) {
        const std::vector<Action> actions = legal_actions(s, db);
        Action a;
        if (s.pending.player == kMatchupUw) {
            a = uw.choose(s, actions);
            if (uw.last_rule() == UwAgent::kDefaultRule) ++g.uw_defaults;
            g.rules[g.log.entries.size()] = "UW: " + std::string(uw.last_rule());
        } else {
            a = rakdos.choose(s, actions);
            if (rakdos.last_rule() == RakdosAgent::kDefaultRule) ++g.rakdos_defaults;
            g.rules[g.log.entries.size()] = "Rakdos: " + std::string(rakdos.last_rule());
        }
        apply(s, db, a, &g.log);
    }
    g.turns = s.turn.turn_number;
    g.winner = s.pending.kind == DecisionKind::GameOver ? s.turn.winner : kNoPlayer;
    for (const auto& [rule, n] : uw.rule_counts()) g.uw_rules[rule] += n;
    for (const auto& [rule, n] : rakdos.rule_counts()) g.rakdos_rules[rule] += n;
    return g;
}

// Un ViewAgent di prova: passa se puo', altrimenti la prima azione; registra le viste che riceve.
class RecordingViewAgent final : public ViewAgent {
public:
    struct Seen {
        PlayerId viewer;
        DecisionKind decision;
        int hidden_opponent_hand;
    };
    explicit RecordingViewAgent(std::vector<Seen>* seen) : seen_(seen) {}
    Action choose(const PlayerView& view, std::span<const Action> legal) override {
        const PlayerId opponent = static_cast<PlayerId>(1 - view.viewer());
        seen_->push_back(Seen{view.viewer(), view.decision(), view.hidden_in_hand(opponent)});
        // Un mondo campionato dalla vista e' uno stato coerente (lo usera' la ricerca).
        const GameState world = view.sample_world(seen_->size());
        if (!check_invariants(world, view.db()).empty()) throw EngineError("mondo incoerente");
        for (const Action& a : legal) {
            if (a.type == ActionType::Pass || a.type == ActionType::Keep) return a;
        }
        return legal.front();
    }
    [[nodiscard]] std::string_view last_rule() const override { return "prova: passa o la prima"; }

private:
    std::vector<Seen>* seen_;
};

}  // namespace

TEST_CASE("Matchup: con gli euristici scelti dalla configurazione i registri sono quelli di prima",
          "[matchup][giocatori]") {
    // Il controllo byte per byte sono gli MD5 della sezione 1 di docs/metriche.md; qui, voce per voce, contro il ciclo
    // di prima, con le opzioni di base_v2 e base_v1 contro base_r3 e base_r1.
    for (const bool v1 : {false, true}) {
        MatchupConfig config = matchup_config();
        if (v1) {
            config.uw_agent = UwAgentOptions::base_v1();
            config.rakdos_agent = RakdosAgentOptions::base_r1();
        }
        REQUIRE(config.uw_seat.kind == AgentKind::Heuristic);
        REQUIRE(config.rakdos_seat.kind == AgentKind::Heuristic);
        for (std::uint64_t seed = 1; seed <= 30; ++seed) {
            INFO((v1 ? "base_v1 contro base_r1" : "base_v2 contro base_r3") << ", seed " << seed);
            const OldGame before = old_play(config, seed);
            GameLog log;
            std::map<std::size_t, std::string> rules;
            std::map<std::string, std::uint64_t> uw_rules, rakdos_rules;
            const MatchupGame g = play_matchup(config, seed, &log, &rules, &uw_rules, &rakdos_rules);
            CHECK(log.entries == before.log.entries);
            CHECK(rules == before.rules);
            CHECK(uw_rules == before.uw_rules);
            CHECK(rakdos_rules == before.rakdos_rules);
            CHECK(g.winner == before.winner);
            CHECK(g.turns == before.turns);
            CHECK(g.uw_defaults == before.uw_defaults);
            CHECK(g.rakdos_defaults == before.rakdos_defaults);
        }
    }
}

TEST_CASE("Matchup: un agente della vista riceve solo la vista del giocatore che decide", "[matchup][giocatori]") {
    for (const PlayerId side : {kMatchupUw, kMatchupRakdos}) {
        std::vector<RecordingViewAgent::Seen> seen;
        MatchupConfig config = matchup_config();
        SeatConfig& seat = side == kMatchupUw ? config.uw_seat : config.rakdos_seat;
        seat.kind = AgentKind::View;
        seat.view_agent = [&seen](std::uint64_t) { return std::make_unique<RecordingViewAgent>(&seen); };
        std::map<std::size_t, std::string> rules;
        GameLog log;
        const MatchupGame g = play_matchup(config, 3, &log, &rules);
        INFO("lato " << static_cast<int>(side));
        REQUIRE_FALSE(seen.empty());
        const std::uint64_t decisions = side == kMatchupUw ? g.uw_decisions : g.rakdos_decisions;
        CHECK(seen.size() == decisions);
        for (const auto& v : seen) CHECK(v.viewer == side);
        // Alla prima decisione (il mulligan) la mano avversaria e' tutta nascosta.
        CHECK(seen.front().decision == DecisionKind::Mulligan);
        CHECK(seen.front().hidden_opponent_hand == 7);
        // Le regole dell'agente finiscono nel registro delle regole, con il nome del lato.
        const std::string prefix = side == kMatchupUw ? "UW: " : "Rakdos: ";
        bool found = false;
        for (const auto& [index, rule] : rules) found = found || rule == prefix + "prova: passa o la prima";
        CHECK(found);
        CHECK((side == kMatchupUw ? g.uw_defaults : g.rakdos_defaults) == 0);
        // Stesso seed, stessa partita.
        std::vector<RecordingViewAgent::Seen> again;
        seat.view_agent = [&again](std::uint64_t) { return std::make_unique<RecordingViewAgent>(&again); };
        GameLog log2;
        play_matchup(config, 3, &log2);
        CHECK(log2.entries == log.entries);
    }
}

TEST_CASE("Matchup: un agente della vista senza fabbrica e' un errore", "[matchup][giocatori]") {
    MatchupConfig config = matchup_config();
    config.uw_seat.kind = AgentKind::View;
    CHECK_THROWS_AS(play_matchup(config, 1), EngineError);
}
