// Ocelot Pride — {W} — Creature — Cat 1/1
// Oracle (Scryfall, 8/11/2026):
//   First strike, lifelink
//   Ascend (If you control ten or more permanents, you get the city's blessing
//   for the rest of the game.)
//   At the beginning of your end step, if you gained life this turn, create a 1/1
//   white Cat creature token. Then if you have the city's blessing, for each token
//   you control that entered this turn, create a token that's a copy of it.
// Dichiarate per ora solo le parole chiave (attacco improvviso, legame vitale).
#include "card_db.hpp"

namespace mtg::cards {
void register_ocelot_pride(CardDatabase& db) {
    CardRules r;  // non implementata: solo le parole chiave di combattimento
    r.combat.keywords = static_cast<KeywordMask>(keyword_bit(Keyword::FirstStrike) | keyword_bit(Keyword::Lifelink));
    db.set_rules("Ocelot Pride", r);
}
}  // namespace mtg::cards
