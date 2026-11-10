// Seam Rip — {W} — Enchantment
// Oracle (Scryfall, 9/11/2026):
//   When this enchantment enters, exile target nonland permanent an opponent controls
//   with mana value 2 or less until this enchantment leaves the battlefield.
// Durata "until" (610.3): il ritorno avviene in move_and_collect (engine/game.cpp)
// quando Seam Rip lascia il campo.
#include "card_db.hpp"

namespace mtg::cards {
void register_seam_rip(CardDatabase& db) {
    CardRules r;
    r.implemented = true;
    r.triggers.exile_until_leaves_on_enter = true;
    db.set_rules("Seam Rip", r);
}
}  // namespace mtg::cards
