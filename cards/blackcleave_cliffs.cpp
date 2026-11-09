// Blackcleave Cliffs — Land
// Oracle (Scryfall, 8/11/2026):
//   This land enters tapped unless you control two or fewer other lands.
//   {T}: Add {B} or {R}.
#include "land_templates.hpp"

namespace mtg::cards {
void register_blackcleave_cliffs(CardDatabase& db) {
    CardRules r = basic_typed_land();
    r.land.enters = EntersTapped::IfMoreThanTwoOtherLands;
    r.land.mana = static_cast<ColorMask>(color_bit(Color::Black) | color_bit(Color::Red));
    db.set_rules("Blackcleave Cliffs", r);
}
}  // namespace mtg::cards
