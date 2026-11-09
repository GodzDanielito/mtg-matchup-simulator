// Recruiter of the Guard — {2}{W} — Creature — Human Soldier 1/1
// Oracle (Scryfall, 9/11/2026):
//   When this creature enters, you may search your library for a creature card with
//   toughness 2 or less, reveal it, put it into your hand, then shuffle.
#include "card_db.hpp"

namespace mtg::cards {
void register_recruiter_of_the_guard(CardDatabase& db) {
    CardRules r;
    r.implemented = true;
    r.triggers.search_on_enter = true;
    r.triggers.search.creature = true;
    r.triggers.search.max_toughness = 2;
    r.triggers.search.optional = true;  // "you may search"
    db.set_rules("Recruiter of the Guard", r);
}
}  // namespace mtg::cards
