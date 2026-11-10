// Teferi, Time Raveler — {1}{W}{U} — Legendary Planeswalker — Teferi — Loyalty 4
// Oracle (Scryfall, 9/11/2026):
//   Each opponent can cast spells only any time they could cast a sorcery.
//   +1: Until your next turn, you may cast sorcery spells as though they had flash.
//   −3: Return up to one target artifact, creature, or enchantment to its owner's
//   hand. Draw a card.
// La fedeltà iniziale (4) viene da cards.json; entra con altrettanti segnalini (306.5b).
#include "card_db.hpp"

namespace mtg::cards {
void register_teferi_time_raveler(CardDatabase& db) {
    CardRules r;
    r.implemented = true;
    r.statics.opponents_sorcery_timing = true;
    r.loyalty[0] = LoyaltyAbility{+1, LoyaltyEffect::SorceriesAsFlash, TargetKind::None};
    r.loyalty[1] = LoyaltyAbility{-3, LoyaltyEffect::BounceAndDraw, TargetKind::ArtifactCreatureEnchantment};
    r.loyalty_count = 2;
    db.set_rules("Teferi, Time Raveler", r);
}
}  // namespace mtg::cards
