// Hallowed Fountain — Land — Plains Island
// Oracle (Scryfall, 8/11/2026):
//   ({T}: Add {W} or {U}.)
//   As this land enters, you may pay 2 life. If you don't, it enters tapped.
#include "land_templates.hpp"

namespace mtg::cards {
void register_hallowed_fountain(CardDatabase& db) { db.set_rules("Hallowed Fountain", pay_life_or_tapped(2)); }
}  // namespace mtg::cards
