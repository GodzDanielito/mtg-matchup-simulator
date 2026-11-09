// Flooded Strand — Land
// Oracle (Scryfall, 8/11/2026):
//   {T}, Pay 1 life, Sacrifice this land: Search your library for a Plains or Island
//   card, put it onto the battlefield, then shuffle.
#include "land_templates.hpp"

namespace mtg::cards {
void register_flooded_strand(CardDatabase& db) {
    db.set_rules("Flooded Strand", fetchland(LandType::Plains, LandType::Island));
}
}  // namespace mtg::cards
