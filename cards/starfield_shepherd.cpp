// Starfield Shepherd — {3}{W}{W} — Creature — Angel 3/2
// Oracle (Scryfall, 8/11/2026):
//   Flying
//   When this creature enters, search your library for a basic Plains card or a
//   creature card with mana value 1 or less, reveal it, put it into your hand, then
//   shuffle.
//   Warp {1}{W}
// Dichiarato per ora solo volare.
#include "card_db.hpp"

namespace mtg::cards {
void register_starfield_shepherd(CardDatabase& db) {
    CardRules r;  // non implementata: solo le parole chiave di combattimento
    r.combat.keywords = keyword_bit(Keyword::Flying);
    db.set_rules("Starfield Shepherd", r);
}
}  // namespace mtg::cards
