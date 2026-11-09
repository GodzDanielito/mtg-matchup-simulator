// Meticulous Archive — Land — Plains Island
// Oracle (Scryfall, 8/11/2026):
//   ({T}: Add {W} or {U}.)
//   This land enters tapped.
//   When this land enters, surveil 1. (Look at the top card of your library.
//   You may put it into your graveyard.)
#include "land_templates.hpp"

namespace mtg::cards {
void register_meticulous_archive(CardDatabase& db) { db.set_rules("Meticulous Archive", tapped_surveil_land()); }
}  // namespace mtg::cards
