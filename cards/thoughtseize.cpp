// Thoughtseize — {B} — Sorcery
// Oracle (Scryfall, 8/11/2026):
//   Target player reveals their hand. You choose a nonland card from it. That
//   player discards that card. You lose 2 life.
#include "card_db.hpp"

namespace mtg::cards {
void register_thoughtseize(CardDatabase& db) {
    CardRules r;
    r.implemented = true;
    r.spell.effect = SpellEffect::Thoughtseize;
    r.spell.target = TargetKind::Player;
    r.spell.life_loss = 2;
    db.set_rules("Thoughtseize", r);
}
}  // namespace mtg::cards
