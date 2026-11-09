// Bloodstained Mire — Land
// Oracle (Scryfall, 8/11/2026):
//   {T}, Pay 1 life, Sacrifice this land: Search your library for a Swamp or Mountain
//   card, put it onto the battlefield, then shuffle.
#include "land_templates.hpp"

namespace mtg::cards {
void register_bloodstained_mire(CardDatabase& db) {
    db.set_rules("Bloodstained Mire", fetchland(LandType::Swamp, LandType::Mountain));
}
}  // namespace mtg::cards
