#pragma once

// Creazione di una partita da due liste e un seed.

#include "card_db.hpp"
#include "deck_loader.hpp"
#include "game_state.hpp"

#include <cstdint>

namespace mtg {

// Il generatore del giocatore `player` per la partita `game_seed`, prima di qualsiasi mescolamento: seme e stream
// PCG32 da SplitMix64 del seed della partita (docs/ricerca.md, passo 0). Ogni mescolamento del grimorio del
// giocatore usa questo generatore, quindi le sue pescate non dipendono dalle scelte dell'altro.
[[nodiscard]] Pcg32 player_generator(std::uint64_t game_seed, PlayerId player);

// Le carte del giocatore 0 hanno CardId 0..59 nell'ordine della lista, quelle
// del giocatore 1 hanno 60..119; poi il grimorio di ogni giocatore viene
// mescolato con il generatore di quel giocatore.
// Lancia EngineError se una lista non è legale, CardDataError se contiene un
// nome assente dal database.
[[nodiscard]] GameState create_game(const CardDatabase& db, const Decklist& player0, const Decklist& player1,
                                    std::uint64_t seed);

}  // namespace mtg
