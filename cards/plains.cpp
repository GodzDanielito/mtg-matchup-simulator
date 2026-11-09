// Plains — Basic Land — Plains
// Oracle (Scryfall, 8/11/2026): ({T}: Add {W}.)
// Il mana viene dal tipo Plains (regola 305.6).
#include "land_templates.hpp"

namespace mtg::cards {
void register_plains(CardDatabase& db) { db.set_rules("Plains", basic_typed_land()); }
}  // namespace mtg::cards
