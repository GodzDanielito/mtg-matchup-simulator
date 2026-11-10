// Solitude — {3}{W}{W} — Creature — Elemental Incarnation 3/2
// Oracle (Scryfall, 9/11/2026):
//   Flash
//   Lifelink
//   When this creature enters, exile up to one other target creature. That
//   creature's controller gains life equal to its power.
//   Evoke—Exile a white card from your hand.
// Evocazione (702.74a): costo alternativo; entrando, il controllore la sacrifica. Il
// trigger di sacrificio scatta insieme a quello di entrata (engine/game.cpp, resolve_spell).
#include "card_db.hpp"

namespace mtg::cards {
void register_solitude(CardDatabase& db) {
    CardRules r;
    r.implemented = true;
    r.flash = true;
    r.combat.keywords = keyword_bit(Keyword::Lifelink);
    r.triggers.exile_other_creature_on_enter = true;
    r.evoke_exile_white = true;
    db.set_rules("Solitude", r);
}
}  // namespace mtg::cards
