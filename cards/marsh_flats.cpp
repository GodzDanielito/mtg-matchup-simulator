// Marsh Flats — Land
// Oracle (Scryfall, 8/11/2026):
//   {T}, Pay 1 life, Sacrifice this land: Search your library for a Plains or Swamp
//   card, put it onto the battlefield, then shuffle.
#include "land_templates.hpp"

namespace mtg::cards {
void register_marsh_flats(CardDatabase& db) {
    db.set_rules("Marsh Flats", fetchland(LandType::Plains, LandType::Swamp));
}
}  // namespace mtg::cards
