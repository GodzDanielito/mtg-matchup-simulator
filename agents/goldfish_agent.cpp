#include "goldfish_agent.hpp"

namespace mtg {

Action GoldfishAgent::choose(const GameState& s, std::span<const Action> legal) const {
    if (legal.empty()) throw EngineError("GoldfishAgent: nessuna azione legale");
    for (const Action& a : legal) {
        switch (s.pending.kind) {
            case DecisionKind::Mulligan:
                if (a.type == ActionType::Keep) return a;
                break;
            case DecisionKind::Priority:
                if (a.type == ActionType::Pass) return a;  // non gioca terre e non lancia
                break;
            case DecisionKind::DeclareAttacker:
            case DecisionKind::DeclareBlockers:
                if (a.type == ActionType::ChooseOption) return a;  // non attacca, non blocca
                break;
            default: break;
        }
    }
    return legal.front();  // scarti e scelte obbligate: la prima opzione
}

}  // namespace mtg
