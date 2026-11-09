// Sanctifier en-Vec — {W}{W} — Creature — Human Cleric 2/2
// Oracle (Scryfall, 9/11/2026):
//   Protection from black and from red
//   When this creature enters, exile all cards that are black or red from all
//   graveyards.
//   If a black or red permanent, spell, or card not on the battlefield would be put
//   into a graveyard, exile it instead.
// La sostituzione vale per entrambi i giocatori e si applica in move_objects
// (engine/zones.cpp), l'unico punto in cui le carte si muovono.
#include "card_db.hpp"

namespace mtg::cards {
void register_sanctifier_en_vec(CardDatabase& db) {
    CardRules r;
    r.implemented = true;
    r.combat.protection = static_cast<ColorMask>(color_bit(Color::Black) | color_bit(Color::Red));
    r.triggers.exile_black_red_graveyards_on_enter = true;
    r.statics.exile_black_red_instead = true;
    db.set_rules("Sanctifier en-Vec", r);
}
}  // namespace mtg::cards
