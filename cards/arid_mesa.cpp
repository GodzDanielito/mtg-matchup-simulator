// Arid Mesa — Land
// Oracle (Scryfall, 8/11/2026):
//   {T}, Pay 1 life, Sacrifice this land: Search your library for a Mountain or Plains
//   card, put it onto the battlefield, then shuffle.
#include "land_templates.hpp"

namespace mtg::cards {
void register_arid_mesa(CardDatabase& db) {
    db.set_rules("Arid Mesa", fetchland(LandType::Mountain, LandType::Plains));
}
}  // namespace mtg::cards
