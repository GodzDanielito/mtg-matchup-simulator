#pragma once

// Avversario del goldfish (docs/agente_rakdos.md, sezione 16): non fa nulla.
// Tiene sette carte, non gioca terre, non lancia, non attacca e non blocca;
// scarta quando deve (la prima carta) e passa sempre.

#include "game.hpp"

#include <span>

namespace mtg {

class GoldfishAgent {
public:
    Action choose(const GameState& s, std::span<const Action> legal) const;
};

}  // namespace mtg
