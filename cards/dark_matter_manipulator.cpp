// Dark Matter Manipulator — {B} — Creature — Human Warlock 1/2
// Oracle (Scryfall, 9/11/2026):
//   When this creature enters, mill three cards.
//   This creature gets +2/+0 for every seven cards in your graveyard.
// Il bonus è un'abilità statica calcolata al volo dal numero di carte (non token)
// nel cimitero del controllore.
#include "card_db.hpp"

namespace mtg::cards {
void register_dark_matter_manipulator(CardDatabase& db) {
    CardRules r;
    r.implemented = true;
    r.triggers.mill_on_enter = 3;
    r.statics.power_per_graveyard_cards = 7;
    r.statics.power_bonus = 2;
    db.set_rules("Dark Matter Manipulator", r);
}
}  // namespace mtg::cards
