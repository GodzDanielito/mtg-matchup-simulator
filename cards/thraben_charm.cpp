// Thraben Charm — {1}{W} — Instant
// Oracle (Scryfall, 9/11/2026):
//   Choose one —
//   • Thraben Charm deals damage equal to twice the number of creatures you control
//     to target creature.
//   • Destroy target enchantment.
//   • Exile any number of target players' graveyards.
#include "card_db.hpp"

namespace mtg::cards {
void register_thraben_charm(CardDatabase& db) {
    CardRules r;
    r.implemented = true;
    r.spell.mode_count = 3;
    r.spell.modes[0] = SpellMode{SpellEffect::DamageTwiceCreatures, TargetKind::Creature};
    r.spell.modes[1] = SpellMode{SpellEffect::Destroy, TargetKind::Enchantment};
    r.spell.modes[2] = SpellMode{SpellEffect::ExileGraveyards, TargetKind::Players};
    db.set_rules("Thraben Charm", r);
}
}  // namespace mtg::cards
