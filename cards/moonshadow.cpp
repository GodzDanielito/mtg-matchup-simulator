// Moonshadow — {B} — Creature — Elemental 7/7
// Oracle (Scryfall, 9/11/2026):
//   Menace
//   This creature enters with six -1/-1 counters on it.
//   Whenever one or more permanent cards are put into your graveyard from anywhere
//   while this creature has a -1/-1 counter on it, remove a -1/-1 counter from
//   this creature.
// Il trigger guarda l'evento di gruppo: una volta per evento, non una per carta.
#include "card_db.hpp"

namespace mtg::cards {
void register_moonshadow(CardDatabase& db) {
    CardRules r;
    r.implemented = true;
    r.combat.keywords = keyword_bit(Keyword::Menace);
    r.statics.enters_with_minus_one = 6;
    r.triggers.remove_minus_counter_on_permanent_cards = true;
    db.set_rules("Moonshadow", r);
}
}  // namespace mtg::cards
