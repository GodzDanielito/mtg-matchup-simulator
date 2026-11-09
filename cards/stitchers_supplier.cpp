// Stitcher's Supplier — {B} — Creature — Zombie 1/1
// Oracle (Scryfall, 8/11/2026):
//   When this creature enters or dies, mill three cards. (Put the top three
//   cards of your library into your graveyard.)
#include "card_db.hpp"

namespace mtg::cards {
void register_stitchers_supplier(CardDatabase& db) {
    CardRules r;
    r.implemented = true;
    r.triggers.mill_on_enter = 3;
    r.triggers.mill_on_death = 3;
    db.set_rules("Stitcher's Supplier", r);
}
}  // namespace mtg::cards
