// Quantum Riddler — {3}{U}{U} — Creature — Sphinx 4/6
// Oracle (Scryfall, 8/11/2026):
//   Flying
//   When this creature enters, draw a card.
//   As long as you have one or fewer cards in hand, if you would draw one or more
//   cards, you draw that many cards plus one instead.
//   Warp {1}{U}
// Dichiarato per ora solo volare.
#include "card_db.hpp"

namespace mtg::cards {
void register_quantum_riddler(CardDatabase& db) {
    CardRules r;  // non implementata: solo le parole chiave di combattimento
    r.combat.keywords = keyword_bit(Keyword::Flying);
    db.set_rules("Quantum Riddler", r);
}
}  // namespace mtg::cards
