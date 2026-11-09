// Flare of Malice — {2}{B}{B} — Instant
// Oracle (Scryfall, 9/11/2026):
//   You may sacrifice a nontoken black creature rather than pay this spell's mana
//   cost.
//   Each opponent sacrifices a creature or planeswalker with the greatest mana
//   value among creatures and planeswalkers they control.
// Il sacrificio è un costo: una creatura che muore (Stitcher's Supplier) fa scattare
// i suoi trigger. Tra permanenti a pari valore di mana sceglie l'avversario.
#include "card_db.hpp"

namespace mtg::cards {
void register_flare_of_malice(CardDatabase& db) {
    CardRules r;
    r.implemented = true;
    r.spell.effect = SpellEffect::OpponentsSacrificeGreatest;
    r.alt_sacrifice_black_creature = true;
    db.set_rules("Flare of Malice", r);
}
}  // namespace mtg::cards
