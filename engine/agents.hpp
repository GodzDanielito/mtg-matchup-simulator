#pragma once

// Agenti giocatori. Un agente vede solo le azioni legali e ne sceglie una.

#include "game.hpp"

#include <span>

namespace mtg {

// Livello 1: sceglie a caso tra le azioni legali. Serve a trovare bug nel motore.
// Usa il generatore dello stato, quindi la partita resta riproducibile dal seed.
class RandomAgent {
public:
    Action choose(GameState& s, std::span<const Action> legal) {
        if (legal.empty()) throw EngineError("RandomAgent: nessuna azione legale");
        return legal[s.rng.bounded(static_cast<std::uint32_t>(legal.size()))];
    }
};

}  // namespace mtg
