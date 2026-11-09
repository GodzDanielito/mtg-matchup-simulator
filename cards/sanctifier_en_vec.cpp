// Sanctifier en-Vec — {W}{W} — Creature — Human Cleric 2/2
// Oracle (Scryfall, 8/11/2026):
//   Protection from black and from red
//   When this creature enters, exile all cards that are black or red from all
//   graveyards.
//   If a black or red permanent, spell, or card not on the battlefield would be
//   put into a graveyard, exile it instead.
// Dichiarata per ora solo la protezione (blocchi, danno, bersagli: 702.16).
#include "card_db.hpp"

namespace mtg::cards {
void register_sanctifier_en_vec(CardDatabase& db) {
    CardRules r;  // non implementata: solo le parole chiave di combattimento
    r.combat.protection = static_cast<ColorMask>(color_bit(Color::Black) | color_bit(Color::Red));
    db.set_rules("Sanctifier en-Vec", r);
}
}  // namespace mtg::cards
