// Moonshadow — {B} — Creature — Elemental 7/7
// Oracle (Scryfall, 8/11/2026):
//   Menace
//   This creature enters with six -1/-1 counters on it.
//   Whenever one or more permanent cards are put into your graveyard from anywhere
//   while this creature has a -1/-1 counter on it, remove a -1/-1 counter from
//   this creature.
// Dichiarata per ora solo la minaccia.
#include "card_db.hpp"

namespace mtg::cards {
void register_moonshadow(CardDatabase& db) {
    CardRules r;  // non implementata: solo le parole chiave di combattimento
    r.combat.keywords = keyword_bit(Keyword::Menace);
    db.set_rules("Moonshadow", r);
}
}  // namespace mtg::cards
