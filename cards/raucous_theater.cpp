// Raucous Theater — Land — Swamp Mountain
// Oracle (Scryfall, 8/11/2026):
//   ({T}: Add {B} or {R}.)
//   This land enters tapped.
//   When this land enters, surveil 1. (Look at the top card of your library.
//   You may put it into your graveyard.)
#include "land_templates.hpp"

namespace mtg::cards {
void register_raucous_theater(CardDatabase& db) { db.set_rules("Raucous Theater", tapped_surveil_land()); }
}  // namespace mtg::cards
