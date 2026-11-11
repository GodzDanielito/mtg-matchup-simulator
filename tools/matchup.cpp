#include "matchup.hpp"

#include "game_setup.hpp"

#include <nlohmann/json.hpp>

namespace mtg {

MatchupGame play_matchup(const MatchupConfig& config, std::uint64_t seed, GameLog* log_out,
                         std::map<std::size_t, std::string>* rules, std::map<std::string, std::uint64_t>* uw_rules,
                         std::map<std::string, std::uint64_t>* rakdos_rules) {
    const CardDatabase& db = *config.db;
    GameLog local;
    GameLog& log = log_out != nullptr ? *log_out : local;

    MatchupGame g;
    g.seed = seed;
    g.uw_on_play = seed % 2 == 1;
    GameState s = create_game(db, config.uw, config.rakdos, seed);
    start_game(s, db, StartOptions{g.uw_on_play ? kMatchupUw : kMatchupRakdos, true}, &log);
    UwAgent uw(db, seed, config.uw_agent);
    RakdosAgent rakdos(db, seed, config.rakdos_agent);

    while (s.pending.kind != DecisionKind::GameOver && s.turn.turn_number <= config.turn_limit) {
        const std::vector<Action> actions = legal_actions(s, db);
        Action a;
        if (s.pending.player == kMatchupUw) {
            a = uw.choose(s, actions);
            ++g.uw_decisions;
            if (uw.last_rule() == UwAgent::kDefaultRule) ++g.uw_defaults;
            if (rules != nullptr) (*rules)[log.entries.size()] = "UW: " + std::string(uw.last_rule());
        } else {
            a = rakdos.choose(s, actions);
            ++g.rakdos_decisions;
            if (rakdos.last_rule() == RakdosAgent::kDefaultRule) ++g.rakdos_defaults;
            if (rules != nullptr) (*rules)[log.entries.size()] = "Rakdos: " + std::string(rakdos.last_rule());
        }
        apply(s, db, a, &log);
    }

    g.turns = s.turn.turn_number;
    if (s.pending.kind != DecisionKind::GameOver) {
        g.ending = "limite di turni";
    } else if (s.turn.winner == kNoPlayer) {
        g.ending = "patta";
    } else {
        g.winner = s.turn.winner;
        const PlayerState& loser = s.players[1 - s.turn.winner];
        g.ending = loser.drew_from_empty_library && loser.life > 0 ? "grimorio vuoto" : "vita a zero";
    }
    g.uw_mulligans = s.players[kMatchupUw].mulligans;
    g.rakdos_mulligans = s.players[kMatchupRakdos].mulligans;
    g.uw_life = s.players[kMatchupUw].life;
    g.rakdos_life = s.players[kMatchupRakdos].life;
    if (uw_rules != nullptr) {
        for (const auto& [rule, n] : uw.rule_counts()) (*uw_rules)[rule] += n;
    }
    if (rakdos_rules != nullptr) {
        for (const auto& [rule, n] : rakdos.rule_counts()) (*rakdos_rules)[rule] += n;
    }
    return g;
}

std::string matchup_game_json(const MatchupGame& g) {
    nlohmann::ordered_json j;
    j["seed"] = g.seed;
    j["uw_in_play"] = g.uw_on_play;
    j["vincitore"] = g.winner == kMatchupUw ? "UW" : g.winner == kMatchupRakdos ? "Rakdos" : "nessuno";
    j["fine"] = g.ending;
    j["turni"] = g.turns;
    j["mulligan_uw"] = g.uw_mulligans;
    j["mulligan_rakdos"] = g.rakdos_mulligans;
    j["vita_uw"] = g.uw_life;
    j["vita_rakdos"] = g.rakdos_life;
    j["decisioni_uw"] = g.uw_decisions;
    j["predefinite_uw"] = g.uw_defaults;
    j["decisioni_rakdos"] = g.rakdos_decisions;
    j["predefinite_rakdos"] = g.rakdos_defaults;
    return j.dump();
}

}  // namespace mtg
