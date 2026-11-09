// Fear of Missing Out — {1}{R} — Enchantment Creature — Nightmare 2/3
// Oracle (Scryfall, 9/11/2026):
//   When this creature enters, discard a card, then draw a card.
//   Delirium — Whenever this creature attacks for the first time each turn, if
//   there are four or more card types among cards in your graveyard, untap target
//   creature. After this phase, there is an additional combat phase.
// "attacks for the first time each turn" usa il flag AttackedThisTurn dell'oggetto;
// l'"if" interposto (603.4) si controlla quando scatta e quando si risolve.
#include "card_db.hpp"

namespace mtg::cards {
void register_fear_of_missing_out(CardDatabase& db) {
    CardRules r;
    r.implemented = true;
    r.triggers.rummage_on_enter = true;
    r.triggers.delirium_first_attack_extra_combat = true;
    db.set_rules("Fear of Missing Out", r);
}
}  // namespace mtg::cards
