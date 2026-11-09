// Mishra's Bauble — {0} — Artifact
// Oracle (Scryfall, 8/11/2026):
//   {T}, Sacrifice this artifact: Look at the top card of target player's
//   library. Draw a card at the beginning of the next turn's upkeep.
#include "card_db.hpp"

namespace mtg::cards {
void register_mishras_bauble(CardDatabase& db) {
    CardRules r;
    r.implemented = true;
    r.activated.effect = ActivatedEffect::LookAtTopThenDelayedDraw;
    r.activated.target = TargetKind::Player;
    db.set_rules("Mishra's Bauble", r);
}
}  // namespace mtg::cards
