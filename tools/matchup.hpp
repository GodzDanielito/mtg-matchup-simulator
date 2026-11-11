#pragma once

// Matchup UW Blink contro Rakdos Delirium, gara 1, solo i mazzi di main: agente euristico di
// UW (docs/agente_uw.md) contro agente euristico di Rakdos (docs/agente_rakdos.md).
//
// UW è il giocatore 0, Rakdos il giocatore 1. Con il seed dispari UW gioca per primo: i seed
// da 1 a N danno metà partite con UW in play e metà con UW in draw.

#include "card_db.hpp"
#include "deck_loader.hpp"
#include "game.hpp"
#include "rakdos_agent.hpp"
#include "uw_agent.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace mtg {

inline constexpr PlayerId kMatchupUw = 0;
inline constexpr PlayerId kMatchupRakdos = 1;

struct MatchupConfig {
    const CardDatabase* db = nullptr;
    Decklist uw;
    Decklist rakdos;
    int turn_limit = 60;  // turni della partita (di entrambi); oltre, "limite di turni"
    UwAgentOptions uw_agent;
    RakdosAgentOptions rakdos_agent;
};

struct MatchupGame {
    std::uint64_t seed = 0;
    bool uw_on_play = false;
    PlayerId winner = kNoPlayer;  // kNoPlayer: patta o limite di turni
    std::string ending;           // "vita a zero", "grimorio vuoto", "patta", "limite di turni"
    int turns = 0;                // turni della partita (il motore li conta per entrambi)
    int uw_mulligans = 0, rakdos_mulligans = 0;
    int uw_life = 0, rakdos_life = 0;
    std::uint64_t uw_decisions = 0, uw_defaults = 0;
    std::uint64_t rakdos_decisions = 0, rakdos_defaults = 0;
};

// Un osservatore passivo della partita (diagnostica): riceve lo stato in sola lettura PRIMA di ogni
// azione, con le azioni legali e quella scelta, e alla fine. Non puo' cambiare la partita.
class MatchupObserver {
public:
    virtual ~MatchupObserver() = default;
    virtual void on_decision(const GameState& s, const std::vector<Action>& legal, const Action& chosen) = 0;
    virtual void on_end(const GameState& s) = 0;
};

// Gioca una partita. `log` (facoltativo) riceve il registro completo; `rules` (facoltativo) la
// regola di ogni decisione, per indice della voce nel registro ("UW: ..." o "Rakdos: ...");
// `uw_rules` e `rakdos_rules` accumulano i conteggi delle regole.
MatchupGame play_matchup(const MatchupConfig& config, std::uint64_t seed, GameLog* log = nullptr,
                         std::map<std::size_t, std::string>* rules = nullptr,
                         std::map<std::string, std::uint64_t>* uw_rules = nullptr,
                         std::map<std::string, std::uint64_t>* rakdos_rules = nullptr,
                         MatchupObserver* observer = nullptr);

// La partita come una riga JSON (data/matchup/partite.jsonl).
[[nodiscard]] std::string matchup_game_json(const MatchupGame& game);

// Le partite da raccontare in un replay leggibile: 10 seed, 5 con UW in play e 5 in draw, meta' vinte
// da UW e meta' da Rakdos. Con 5 per ogni lato non si puo' dividere pari ovunque: UW vince 3 in play
// e 2 in draw, Rakdos vince 2 con UW in play e 3 con UW in draw. Sono le prime partite di ogni
// gruppo, nell'ordine dato; in ordine crescente di seed. Se un gruppo ha meno partite, ne restano meno.
[[nodiscard]] std::vector<std::uint64_t> pick_replay_seeds(const std::vector<MatchupGame>& games);

}  // namespace mtg
