// Force of Negation — {1}{U}{U} — Instant
// Oracle (Scryfall, 9/11/2026):
//   If it's not your turn, you may exile a blue card from your hand rather than pay
//   this spell's mana cost.
//   Counter target noncreature spell. If that spell is countered this way, exile it
//   instead of putting it into its owner's graveyard.
#include "card_db.hpp"

namespace mtg::cards {
void register_force_of_negation(CardDatabase& db) {
    CardRules r;
    r.implemented = true;
    r.spell.effect = SpellEffect::CounterAndExile;
    r.spell.target = TargetKind::NoncreatureSpell;
    r.pitch_blue = true;
    db.set_rules("Force of Negation", r);
}
}  // namespace mtg::cards
