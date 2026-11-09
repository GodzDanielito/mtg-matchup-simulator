// Windswept Heath — Land
// Oracle (Scryfall, 8/11/2026):
//   {T}, Pay 1 life, Sacrifice this land: Search your library for a Forest or Plains
//   card, put it onto the battlefield, then shuffle.
#include "land_templates.hpp"

namespace mtg::cards {
void register_windswept_heath(CardDatabase& db) {
    db.set_rules("Windswept Heath", fetchland(LandType::Forest, LandType::Plains));
}
}  // namespace mtg::cards
