#pragma once

// Un agente per lato del matchup, scelto dalla configurazione (docs/ricerca.md, passo 1).
//
// play_matchup costruisce un "posto" (Seat) per giocatore e gli chiede ogni decisione di quel giocatore. Due tipi:
//  - Heuristic, "euristico v1": UwAgent o RakdosAgent con le opzioni di MatchupConfig. Legge lo stato vero, come
//    sempre; che non usi l'informazione nascosta lo verificano i test dello stato gemello degli agenti.
//  - View: un ViewAgent (il giocatore forte, dal passo 2). Il posto costruisce a ogni decisione la vista del
//    giocatore (PlayerView, engine/player_view.hpp) e passa all'agente solo quella: il ViewAgent non riceve mai
//    lo stato vero, e il suo codice non ha un tipo con cui leggerlo.
// Con i due euristici i registri sono identici byte per byte a quelli di prima dell'interfaccia (riferimenti nella
// sezione 1 di docs/metriche.md).

#include "card_db.hpp"
#include "game.hpp"
#include "player_view.hpp"
#include "rakdos_agent.hpp"
#include "uw_agent.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace mtg {

enum class AgentKind : std::uint8_t {
    Heuristic,  // l'euristico del lato (UW o Rakdos), con le opzioni di MatchupConfig
    View,       // un ViewAgent costruito da SeatConfig::view_agent
};

// Costruisce il ViewAgent di una partita dal seed della partita (la sua casualita' deve venire da li').
using ViewAgentFactory = std::function<std::unique_ptr<ViewAgent>(std::uint64_t game_seed)>;

struct SeatConfig {
    AgentKind kind = AgentKind::Heuristic;
    ViewAgentFactory view_agent;  // obbligatoria con AgentKind::View
};

class Seat {
public:
    virtual ~Seat() = default;
    // Solo play_matchup la chiama, con lo stato vero: per un ViewAgent lo stato diventa qui una vista.
    virtual Action choose(const GameState& s, std::span<const Action> legal) = 0;
    [[nodiscard]] virtual std::string_view last_rule() const = 0;
    // L'ultima scelta e' la rete di sicurezza dell'agente ("predefinito").
    [[nodiscard]] virtual bool last_was_default() const = 0;
    virtual void add_rule_counts(std::map<std::string, std::uint64_t>& out) const = 0;
};

// Il posto di UW (giocatore `player`) e quello di Rakdos. EngineError se AgentKind::View non ha la fabbrica.
[[nodiscard]] std::unique_ptr<Seat> make_uw_seat(const SeatConfig& config, PlayerId player, const CardDatabase& db,
                                                 std::uint64_t seed, const UwAgentOptions& options);
[[nodiscard]] std::unique_ptr<Seat> make_rakdos_seat(const SeatConfig& config, PlayerId player, const CardDatabase& db,
                                                     std::uint64_t seed, const RakdosAgentOptions& options);

}  // namespace mtg
