// Nethergoyf — {B} — Creature — Lhurgoyf */1+*
// Oracle (Scryfall, 9/11/2026):
//   Nethergoyf's power is equal to the number of card types among cards in your
//   graveyard and its toughness is equal to that number plus 1.
//   Escape—{2}{B}, Exile any number of other cards from your graveyard with four
//   or more card types among them. (You may cast this card from your graveyard
//   for its escape cost.)
// La forza è un'abilità che definisce la caratteristica (604.3): si calcola al volo
// dal riepilogo del cimitero del controllore. Per la fuga si offrono solo gli insiemi
// minimali di carte da esiliare (docs/stato_di_gioco.md, sezione 14).
#include "card_db.hpp"

namespace mtg::cards {
void register_nethergoyf(CardDatabase& db) {
    CardRules r;
    r.implemented = true;
    r.statics.power_from_graveyard_types = true;
    r.escape.cost = parse_mana_cost("{2}{B}");
    r.escape.min_types = 4;
    db.set_rules("Nethergoyf", r);
}
}  // namespace mtg::cards
