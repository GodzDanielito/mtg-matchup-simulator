// Troll of Khazad-dûm — {5}{B} — Creature — Troll 6/5
// Oracle (Scryfall, 8/11/2026):
//   This creature can't be blocked except by three or more creatures.
//   Swampcycling {1} ({1}, Discard this card: Search your library for a Swamp card,
//   reveal it, put it into your hand, then shuffle.)
// Dichiarata per ora solo la restrizione di blocco (minimo tre bloccanti).
#include "card_db.hpp"

namespace mtg::cards {
void register_troll_of_khazad_dum(CardDatabase& db) {
    CardRules r;  // non implementata: solo le parole chiave di combattimento
    r.combat.min_blockers = 3;
    db.set_rules("Troll of Khazad-dûm", r);
}
}  // namespace mtg::cards
