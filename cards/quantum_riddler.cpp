// Quantum Riddler — {3}{U}{U} — Creature — Sphinx 4/6
// Oracle (Scryfall, 9/11/2026):
//   Flying
//   When this creature enters, draw a card.
//   As long as you have one or fewer cards in hand, if you would draw one or more
//   cards, you draw that many cards plus one instead.
//   Warp {1}{U}
// La sostituzione delle pescate è in draw_cards (engine/game.cpp); il warp riusa quello di
// Starfield Shepherd.
#include "card_db.hpp"

namespace mtg::cards {
void register_quantum_riddler(CardDatabase& db) {
    CardRules r;
    r.implemented = true;
    r.combat.keywords = keyword_bit(Keyword::Flying);
    r.triggers.draw_on_enter = 1;
    r.statics.draw_one_more_small_hand = true;
    r.warp.available = true;
    r.warp.cost = parse_mana_cost("{1}{U}");
    db.set_rules("Quantum Riddler", r);
}
}  // namespace mtg::cards
