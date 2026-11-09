#pragma once

// Agenti giocatori. Un agente vede solo le azioni legali e ne sceglie una.
//
// Ogni agente ha un generatore casuale suo, separato da quello della partita:
// choose() riceve lo stato in sola lettura, quindi non può consumare i numeri
// casuali della partita. Due agenti diversi con lo stesso seed di partita
// giocano con gli stessi grimori, le stesse mani e (a parità di azioni) gli
// stessi mescolamenti: "numeri casuali comuni" per confrontare agenti e mazzi.

#include "game.hpp"
#include "rng.hpp"

#include <span>

namespace mtg {

// Stream PCG riservato agli agenti: anche con lo stesso seed numerico della
// partita, la sequenza dell'agente è indipendente da quella delle regole.
inline constexpr std::uint64_t kAgentStream = 0x9e3779b97f4a7c15ULL;

// Livello 1: sceglie a caso tra le azioni legali. Serve a trovare bug nel motore.
class RandomAgent {
public:
    explicit RandomAgent(std::uint64_t seed) : rng_(seed, kAgentStream) {}

    Action choose(const GameState& /*s*/, std::span<const Action> legal) {
        if (legal.empty()) throw EngineError("RandomAgent: nessuna azione legale");
        return legal[rng_.bounded(static_cast<std::uint32_t>(legal.size()))];
    }

private:
    Pcg32 rng_;
};

}  // namespace mtg
