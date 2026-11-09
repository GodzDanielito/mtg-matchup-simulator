// Guide of Souls — {W} — Creature — Human Cleric 1/2
// Oracle (Scryfall, 9/11/2026):
//   Whenever another creature you control enters, you gain 1 life and get {E} (an
//   energy counter).
//   Whenever you attack, you may pay {E}{E}{E}. When you do, put two +1/+1 counters
//   and a flying counter on target attacking creature. It becomes an Angel in
//   addition to its other types.
#include "card_db.hpp"

namespace mtg::cards {
void register_guide_of_souls(CardDatabase& db) {
    CardRules r;
    r.implemented = true;
    r.triggers.life_energy_on_other_creature = true;
    r.triggers.energy_attack_counters = true;
    db.set_rules("Guide of Souls", r);
}
}  // namespace mtg::cards
