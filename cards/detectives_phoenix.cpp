// Detective's Phoenix — {2}{R} — Enchantment Creature — Phoenix 2/2
// Oracle (Scryfall, 8/11/2026):
//   Bestow—{R}, Collect evidence 6.
//   Flying, haste
//   Enchanted creature gets +2/+2 and has flying and haste.
//   You may cast this card from your graveyard using its bestow ability.
// Dichiarate per ora solo le parole chiave della creatura (volare, rapidità).
#include "card_db.hpp"

namespace mtg::cards {
void register_detectives_phoenix(CardDatabase& db) {
    CardRules r;  // non implementata: solo le parole chiave di combattimento
    r.combat.keywords = static_cast<KeywordMask>(keyword_bit(Keyword::Flying) | keyword_bit(Keyword::Haste));
    db.set_rules("Detective's Phoenix", r);
}
}  // namespace mtg::cards
