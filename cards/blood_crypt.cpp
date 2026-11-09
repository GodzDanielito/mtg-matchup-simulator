// Blood Crypt — Land — Swamp Mountain
// Oracle (Scryfall, 8/11/2026):
//   ({T}: Add {B} or {R}.)
//   As this land enters, you may pay 2 life. If you don't, it enters tapped.
#include "land_templates.hpp"

namespace mtg::cards {
void register_blood_crypt(CardDatabase& db) { db.set_rules("Blood Crypt", pay_life_or_tapped(2)); }
}  // namespace mtg::cards
