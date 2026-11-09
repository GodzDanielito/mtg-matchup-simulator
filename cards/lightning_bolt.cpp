// Lightning Bolt — {R} — Instant
// Oracle (Scryfall, 8/11/2026):
//   Lightning Bolt deals 3 damage to any target.
#include "card_db.hpp"

namespace mtg::cards {
void register_lightning_bolt(CardDatabase& db) {
    CardRules r;
    r.implemented = true;
    r.spell.effect = SpellEffect::DealDamage;
    r.spell.target = TargetKind::AnyTarget;
    r.spell.amount = 3;
    db.set_rules("Lightning Bolt", r);
}
}  // namespace mtg::cards
