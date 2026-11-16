#pragma once

// Le dieci posizioni test della ricerca (docs/ricerca.md, sezione 5.1), come stati preparati.
//
// Ogni posizione e' uno stato vero alla decisione di UW (giocatore 0) che la linea guida riguarda. Le posizioni sono
// **linee guida di Daniele** (pilota di UW Blink): si misurano con l'accordo, non si usano come verdetti. La ricerca
// (dal passo 2) le vede come insiemi di informazione: la mano di Rakdos e i grimori si campionano dalla vista.
//
// Per ogni posizione c'e' una contro-posizione ("c" nell'identificativo, o la variante indicata) in cui la
// condizione della linea guida non vale. I test (tests/test_posizioni.cpp) verificano che la giocata indicata sia
// tra le azioni legali del motore e registrano cosa fa oggi l'euristico.

#include "card_db.hpp"
#include "deck_loader.hpp"
#include "game_state.hpp"

#include <string>
#include <vector>

namespace mtg {

struct TestPosition {
    std::string id;         // "1", "1c", ..., "7a", "7c", "8a", "8b", "8c", "9a", "9b", "10a", "10b", "10c"
    std::string situation;  // la posizione, in parole
    std::string indicated;  // la giocata indicata dalla linea guida (per una contro-posizione: perche' non vale)
    GameState state;        // alla decisione di UW
};

// Gli identificativi, nell'ordine della tabella.
[[nodiscard]] std::vector<std::string> test_position_ids();

// La posizione `id`, preparata con le due liste (UW giocatore 0, Rakdos giocatore 1). EngineError se l'id non esiste
// o se la preparazione non arriva alla decisione attesa.
[[nodiscard]] TestPosition test_position(const CardDatabase& db, const Decklist& uw, const Decklist& rakdos,
                                         const std::string& id);

}  // namespace mtg
