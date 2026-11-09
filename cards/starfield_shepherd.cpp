// Starfield Shepherd — {3}{W}{W} — Creature — Angel 3/2
// Oracle (Scryfall, 9/11/2026):
//   Flying
//   When this creature enters, search your library for a basic Plains card or a
//   creature card with mana value 1 or less, reveal it, put it into your hand, then
//   shuffle.
//   Warp {1}{W} (You may cast this card from your hand for its warp cost. Exile this
//   creature at the beginning of the next end step, then you may cast it from exile
//   on a later turn.)
#include "card_db.hpp"

namespace mtg::cards {
void register_starfield_shepherd(CardDatabase& db) {
    CardRules r;
    r.implemented = true;
    r.combat.keywords = keyword_bit(Keyword::Flying);
    r.triggers.search_on_enter = true;
    r.triggers.search.basic_land_types = land_type_bit(LandType::Plains);
    r.triggers.search.creature = true;
    r.triggers.search.max_mana_value = 1;
    r.warp.available = true;
    r.warp.cost = parse_mana_cost("{1}{W}");
    db.set_rules("Starfield Shepherd", r);
}
}  // namespace mtg::cards
