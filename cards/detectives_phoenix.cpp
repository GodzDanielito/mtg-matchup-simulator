// Detective's Phoenix — {2}{R} — Enchantment Creature — Phoenix 2/2
// Oracle (Scryfall, 9/11/2026):
//   Bestow—{R}, Collect evidence 6. (To pay this bestow cost, pay {R} and exile
//   cards with total mana value 6 or greater from your graveyard.)
//   Flying, haste
//   Enchanted creature gets +2/+2 and has flying and haste.
//   You may cast this card from your graveyard using its bestow ability.
// Conferimento (702.103): Aura con "enchant creature"; se alla risoluzione il
// bersaglio è illegale si risolve come creatura; staccata, smette di essere
// conferita e resta in campo come creatura. Le prove si scelgono con insiemi minimali.
#include "card_db.hpp"

namespace mtg::cards {
void register_detectives_phoenix(CardDatabase& db) {
    CardRules r;
    r.implemented = true;
    r.combat.keywords = static_cast<KeywordMask>(keyword_bit(Keyword::Flying) | keyword_bit(Keyword::Haste));
    r.bestow.cost = parse_mana_cost("{R}");
    r.bestow.evidence = 6;
    r.bestow.from_graveyard = true;
    r.bestow.power = 2;
    r.bestow.toughness = 2;
    r.bestow.grants = static_cast<KeywordMask>(keyword_bit(Keyword::Flying) | keyword_bit(Keyword::Haste));
    db.set_rules("Detective's Phoenix", r);
}
}  // namespace mtg::cards
