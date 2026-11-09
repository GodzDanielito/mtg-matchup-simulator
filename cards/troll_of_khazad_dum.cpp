// Troll of Khazad-dûm — {5}{B} — Creature — Troll 6/5
// Oracle (Scryfall, 9/11/2026):
//   This creature can't be blocked except by three or more creatures.
//   Swampcycling {1} ({1}, Discard this card: Search your library for a Swamp card,
//   reveal it, put it into your hand, then shuffle.)
// Il ciclo è un'abilità attivata dalla mano: va in pila; costi {1} e scartare il Troll.
#include "card_db.hpp"

namespace mtg::cards {
void register_troll_of_khazad_dum(CardDatabase& db) {
    CardRules r;
    r.implemented = true;
    r.combat.min_blockers = 3;
    r.landcycling.cost = parse_mana_cost("{1}");
    r.landcycling.types = land_type_bit(LandType::Swamp);
    db.set_rules("Troll of Khazad-dûm", r);
}
}  // namespace mtg::cards
