#pragma once

// Creazione di una partita da due liste e un seed.

#include "card_db.hpp"
#include "deck_loader.hpp"
#include "game_state.hpp"

#include <cstdint>

namespace mtg {

// Le carte del giocatore 0 hanno CardId 0..59 nell'ordine della lista, quelle
// del giocatore 1 hanno 60..119; poi i due grimori vengono mescolati con il
// generatore della partita (prima il giocatore 0, poi l'1).
// Lancia EngineError se una lista non è legale, CardDataError se contiene un
// nome assente dal database.
[[nodiscard]] GameState create_game(const CardDatabase& db, const Decklist& player0, const Decklist& player1,
                                    std::uint64_t seed);

}  // namespace mtg
