// Street Wraith — {3}{B}{B} — Creature — Wraith 3/4
// Oracle (Scryfall, 8/11/2026):
//   Swampwalk (This creature can't be blocked as long as defending player
//   controls a Swamp.)
//   Cycling—Pay 2 life. (Pay 2 life, Discard this card: Draw a card.)
// Swampwalk riguarda i blocchi: arriverà con il combattimento.
#include "card_db.hpp"

namespace mtg::cards {
void register_street_wraith(CardDatabase& db) {
    CardRules r;
    r.implemented = true;
    r.cycling_life = 2;
    db.set_rules("Street Wraith", r);
}
}  // namespace mtg::cards
