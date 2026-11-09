// Polluted Delta — Land
// Oracle (Scryfall, 8/11/2026):
//   {T}, Pay 1 life, Sacrifice this land: Search your library for a Island or Swamp
//   card, put it onto the battlefield, then shuffle.
#include "land_templates.hpp"

namespace mtg::cards {
void register_polluted_delta(CardDatabase& db) {
    db.set_rules("Polluted Delta", fetchland(LandType::Island, LandType::Swamp));
}
}  // namespace mtg::cards
