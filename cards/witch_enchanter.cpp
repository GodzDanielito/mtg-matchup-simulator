// Witch Enchanter // Witch-Blessed Meadow (modal_dfc)
// Oracle (Scryfall, 9/11/2026):
//   Fronte — {3}{W} — Creature — Human Warlock 2/2:
//     When this creature enters, destroy target artifact or enchantment an
//     opponent controls.
//   Retro — Land:
//     As this land enters, you may pay 3 life. If you don't, it enters tapped.
//     {T}: Add {W}.
#include "land_templates.hpp"

namespace mtg::cards {
void register_witch_enchanter(CardDatabase& db) {
    CardRules front;
    front.implemented = true;
    front.triggers.destroy_artifact_enchantment_on_enter = true;
    db.set_rules("Witch Enchanter", front);
    db.set_back_face_rules("Witch Enchanter", pay_life_or_tapped(3, color_bit(Color::White)));
}
}  // namespace mtg::cards
