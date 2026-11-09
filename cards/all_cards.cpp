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
void register_lightning_bolt(CardDatabase& db);
void register_thoughtseize(CardDatabase& db);
void register_street_wraith(CardDatabase& db);
void register_mishras_bauble(CardDatabase& db);
void register_stitchers_supplier(CardDatabase& db);
void register_ocelot_pride(CardDatabase& db);
void register_sanctifier_en_vec(CardDatabase& db);
void register_moonshadow(CardDatabase& db);
void register_troll_of_khazad_dum(CardDatabase& db);
void register_starfield_shepherd(CardDatabase& db);
void register_quantum_riddler(CardDatabase& db);
void register_detectives_phoenix(CardDatabase& db);
void register_solitude(CardDatabase& db);
void register_nethergoyf(CardDatabase& db);
void register_dark_matter_manipulator(CardDatabase& db);
void register_fear_of_missing_out(CardDatabase& db);
void register_flare_of_malice(CardDatabase& db);
void register_guide_of_souls(CardDatabase& db);
void register_recruiter_of_the_guard(CardDatabase& db);
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
    cards::register_lightning_bolt(db);
    cards::register_thoughtseize(db);
    cards::register_street_wraith(db);
    cards::register_mishras_bauble(db);
    cards::register_stitchers_supplier(db);
    cards::register_ocelot_pride(db);
    cards::register_sanctifier_en_vec(db);
    cards::register_moonshadow(db);
    cards::register_troll_of_khazad_dum(db);
    cards::register_starfield_shepherd(db);
    cards::register_quantum_riddler(db);
    cards::register_detectives_phoenix(db);
    cards::register_solitude(db);
    cards::register_nethergoyf(db);
    cards::register_dark_matter_manipulator(db);
    cards::register_fear_of_missing_out(db);
    cards::register_flare_of_malice(db);
    cards::register_guide_of_souls(db);
    cards::register_recruiter_of_the_guard(db);
}

}  // namespace mtg
