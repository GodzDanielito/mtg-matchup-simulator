// Swamp — Basic Land — Swamp
// Oracle (Scryfall, 8/11/2026): ({T}: Add {B}.)
// Il mana viene dal tipo Swamp (regola 305.6).
#include "land_templates.hpp"

namespace mtg::cards {
void register_swamp(CardDatabase& db) { db.set_rules("Swamp", basic_typed_land()); }
}  // namespace mtg::cards
