#pragma once

// Schemi comuni alle terre del pool. Ogni carta in cards/ li usa dichiarando
// esplicitamente i propri parametri (tipi cercati, vita da pagare...).

#include "card_db.hpp"

namespace mtg::cards {

// Terra il cui mana viene solo dai tipi di terra base (regola 305.6).
inline CardRules basic_typed_land() {
    CardRules r;
    r.implemented = true;
    return r;
}

// "As this land enters, you may pay N life. If you don't, it enters tapped."
inline CardRules pay_life_or_tapped(std::uint8_t life, ColorMask extra_mana = 0) {
    CardRules r = basic_typed_land();
    r.land.enters = EntersTapped::PayLifeOrTapped;
    r.land.life_to_untap = life;
    r.land.mana = extra_mana;
    return r;
}

// "{T}, Pay 1 life, Sacrifice this land: Search your library for a <A> or <B>
// card, put it onto the battlefield, then shuffle."
inline CardRules fetchland(LandType a, LandType b) {
    CardRules r = basic_typed_land();
    r.land.fetch_types = static_cast<LandTypeMask>(land_type_bit(a) | land_type_bit(b));
    r.land.fetch_life = 1;
    return r;
}

// "This land enters tapped. When this land enters, surveil 1."
inline CardRules tapped_surveil_land() {
    CardRules r = basic_typed_land();
    r.land.enters = EntersTapped::Always;
    r.land.surveil_on_enter = 1;
    return r;
}

}  // namespace mtg::cards
