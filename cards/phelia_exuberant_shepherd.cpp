// Phelia, Exuberant Shepherd — {1}{W} — Legendary Creature — Dog 2/2
// Oracle (Scryfall, 9/11/2026):
//   Flash
//   Whenever Phelia attacks, exile up to one other target nonland permanent. At the
//   beginning of the next end step, return that card to the battlefield under its
//   owner's control. If it entered under your control, put a +1/+1 counter on Phelia.
#include "card_db.hpp"

namespace mtg::cards {
void register_phelia_exuberant_shepherd(CardDatabase& db) {
    CardRules r;
    r.implemented = true;
    r.flash = true;
    r.triggers.attack_exile_until_end_step = true;
    db.set_rules("Phelia, Exuberant Shepherd", r);
}
}  // namespace mtg::cards
