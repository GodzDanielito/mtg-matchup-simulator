// Ocelot Pride — {W} — Creature — Cat 1/1
// Oracle (Scryfall, 9/11/2026):
//   First strike, lifelink
//   Ascend (If you control ten or more permanents, you get the city's blessing
//   for the rest of the game.)
//   At the beginning of your end step, if you gained life this turn, create a 1/1
//   white Cat creature token. Then if you have the city's blessing, for each token
//   you control that entered this turn, create a token that's a copy of it.
// La benedizione della città arriva solo dall'ascesa di Ocelot Pride stessa: nessun'altra
// carta delle liste ha l'ascesa (702.131).
#include "card_db.hpp"

namespace mtg::cards {
namespace {

// Il token Gatto 1/1 bianco: non sta in cards.json perché non è una carta delle liste.
CardDefId cat_token(CardDatabase& db) {
    if (const CardDef* existing = db.find("Cat Token")) return existing->id;
    CardDef cat;
    cat.name = "Cat Token";
    cat.full_name = "Cat Token";
    cat.layout = "token";
    cat.types = type_bit(CardType::Creature);
    cat.subtypes = {"Cat"};
    cat.colors = color_bit(Color::White);
    cat.has_pt = true;
    cat.base_power = 1;
    cat.base_toughness = 1;
    cat.is_token = true;
    return db.add(std::move(cat));
}

}  // namespace

void register_ocelot_pride(CardDatabase& db) {
    CardRules r;
    r.implemented = true;
    r.combat.keywords = static_cast<KeywordMask>(keyword_bit(Keyword::FirstStrike) | keyword_bit(Keyword::Lifelink));
    r.statics.ascend = true;
    r.triggers.end_step_token = cat_token(db);
    db.set_rules("Ocelot Pride", r);
}
}  // namespace mtg::cards
