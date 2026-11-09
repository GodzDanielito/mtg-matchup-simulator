// Solitude — {3}{W}{W} — Creature — Elemental Incarnation 3/2
// Oracle (Scryfall, 8/11/2026):
//   Flash
//   Lifelink
//   When this creature enters, exile up to one other target creature. That
//   creature's controller gains life equal to its power.
//   Evoke—Exile a white card from your hand.
// Dichiarato per ora solo legame vitale.
#include "card_db.hpp"

namespace mtg::cards {
void register_solitude(CardDatabase& db) {
    CardRules r;  // non implementata: solo le parole chiave di combattimento
    r.combat.keywords = keyword_bit(Keyword::Lifelink);
    db.set_rules("Solitude", r);
}
}  // namespace mtg::cards
