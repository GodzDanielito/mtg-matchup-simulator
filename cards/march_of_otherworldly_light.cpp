// March of Otherworldly Light — {X}{W} — Instant
// Oracle (Scryfall, 9/11/2026):
//   As an additional cost to cast this spell, you may exile any number of white
//   cards from your hand. This spell costs {2} less to cast for each card exiled
//   this way.
//   Exile target artifact, creature, or enchantment with mana value X or less.
#include "card_db.hpp"

namespace mtg::cards {
void register_march_of_otherworldly_light(CardDatabase& db) {
    CardRules r;
    r.implemented = true;
    r.spell.effect = SpellEffect::ExileTarget;
    r.spell.target = TargetKind::ArtifactCreatureEnchantmentMvX;
    r.spell.reduction_per_white_card = 2;
    db.set_rules("March of Otherworldly Light", r);
}
}  // namespace mtg::cards
