#include "all_cards.hpp"

// Elenco esplicito: niente registrazione automatica con inizializzatori statici,
// che in una libreria statica il linker può scartare senza errori.

namespace mtg::cards {
void register_plains(CardDatabase& db);
void register_swamp(CardDatabase& db);
void register_blood_crypt(CardDatabase& db);
void register_hallowed_fountain(CardDatabase& db);
void register_arid_mesa(CardDatabase& db);
void register_bloodstained_mire(CardDatabase& db);
void register_flooded_strand(CardDatabase& db);
void register_marsh_flats(CardDatabase& db);
void register_polluted_delta(CardDatabase& db);
void register_windswept_heath(CardDatabase& db);
void register_blackcleave_cliffs(CardDatabase& db);
void register_raucous_theater(CardDatabase& db);
void register_meticulous_archive(CardDatabase& db);
void register_witch_enchanter(CardDatabase& db);
}  // namespace mtg::cards

namespace mtg {

void register_all_cards(CardDatabase& db) {
    cards::register_plains(db);
    cards::register_swamp(db);
    cards::register_blood_crypt(db);
    cards::register_hallowed_fountain(db);
    cards::register_arid_mesa(db);
    cards::register_bloodstained_mire(db);
    cards::register_flooded_strand(db);
    cards::register_marsh_flats(db);
    cards::register_polluted_delta(db);
    cards::register_windswept_heath(db);
    cards::register_blackcleave_cliffs(db);
    cards::register_raucous_theater(db);
    cards::register_meticulous_archive(db);
    cards::register_witch_enchanter(db);
}

}  // namespace mtg
