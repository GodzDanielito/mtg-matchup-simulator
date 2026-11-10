// Ephemerate — {W} — Instant
// Oracle (Scryfall, 9/11/2026):
//   Exile target creature you control, then return it to the battlefield under its
//   owner's control.
//   Rebound (If you cast this spell from your hand, exile it as it resolves. At the
//   beginning of your next upkeep, you may cast this card from exile without paying
//   its mana cost.)
#include "card_db.hpp"

namespace mtg::cards {
void register_ephemerate(CardDatabase& db) {
    CardRules r;
    r.implemented = true;
    r.spell.effect = SpellEffect::Blink;
    r.spell.target = TargetKind::CreatureYouControl;
    r.spell.rebound = true;
    db.set_rules("Ephemerate", r);
}
}  // namespace mtg::cards
