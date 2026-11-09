#pragma once

// Registro delle carte implementate: aggancia a ogni CardDef le regole
// dichiarate nel suo file in cards/.

#include "card_db.hpp"

namespace mtg {

// Da chiamare una volta dopo CardDatabase::load. Lancia CardDataError se una
// carta implementata non è nel database.
void register_all_cards(CardDatabase& db);

}  // namespace mtg
