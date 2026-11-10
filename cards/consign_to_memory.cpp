// Consign to Memory — {U} — Instant
// Oracle (Scryfall, 9/11/2026):
//   Replicate {1} (When you cast this spell, copy it for each time you paid its
//   replicate cost. You may choose new targets for the copies.)
//   Counter target triggered ability or colorless spell.
// Bersaglio: qualsiasi abilità innescata in pila, anche ritardata e anche propria.
#include "card_db.hpp"

namespace mtg::cards {
void register_consign_to_memory(CardDatabase& db) {
    CardRules r;
    r.implemented = true;
    r.spell.effect = SpellEffect::CounterTarget;
    r.spell.target = TargetKind::TriggeredAbilityOrColorlessSpell;
    r.spell.replicate_generic = 1;
    db.set_rules("Consign to Memory", r);
}
}  // namespace mtg::cards
