// Witch Enchanter // Witch-Blessed Meadow (modal_dfc)
// Oracle (Scryfall, 8/11/2026):
//   Fronte — Creature — Human Warlock:
//     When this creature enters, destroy target artifact or enchantment an
//     opponent controls.                       (NON ancora implementato)
//   Retro — Land:
//     As this land enters, you may pay 3 life. If you don't, it enters tapped.
//     {T}: Add {W}.
// Per ora solo il retro: il fronte resta non implementato (nessuna azione).
#include "land_templates.hpp"

namespace mtg::cards {
void register_witch_enchanter(CardDatabase& db) {
    db.set_back_face_rules("Witch Enchanter", pay_life_or_tapped(3, color_bit(Color::White)));
}
}  // namespace mtg::cards
