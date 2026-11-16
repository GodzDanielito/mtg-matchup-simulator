#include "game_setup.hpp"

#include <string>

namespace mtg {

namespace {

// Riempie gli oggetti e il grimorio di un giocatore nell'ordine della lista.
void place_deck(GameState& s, const CardDatabase& db, const Decklist& deck, PlayerId player) {
    if (const auto errors = validate_decklist(deck); !errors.empty()) {
        throw EngineError("lista \"" + deck.name + "\" non legale: " + errors.front());
    }
    auto next_id = static_cast<std::size_t>(player) * kDeckSize;
    for (const DeckEntry& entry : deck.main) {
        const CardDefId def = db.id_of(entry.card);  // CardDataError se sconosciuta
        for (int copy = 0; copy < entry.count; ++copy) {
            const auto id = static_cast<CardId>(next_id++);
            CardObject& o = s.objects[id];
            o.def = def;
            o.owner = player;
            o.controller = player;
            o.zone = Zone::Library;
            s.players[player].library.push_back(id);
        }
    }
}

}  // namespace

// SplitMix64 dal seed della partita, quattro uscite in ordine: seme e stream del giocatore 0, poi del giocatore 1.
// Seme e stream diversi per giocatore: due sequenze PCG indipendenti, senza correlazioni tra seed vicini.
Pcg32 player_generator(std::uint64_t game_seed, PlayerId player) {
    std::uint64_t state = game_seed;
    std::uint64_t seed = 0, stream = 0;
    for (PlayerId p = 0; p <= player; ++p) {
        seed = splitmix64(state);
        stream = splitmix64(state);
    }
    return Pcg32(seed, stream);
}

GameState create_game(const CardDatabase& db, const Decklist& player0, const Decklist& player1,
                      std::uint64_t seed) {
    GameState s{};
    place_deck(s, db, player0, 0);
    place_deck(s, db, player1, 1);
    for (PlayerId p = 0; p < kNumPlayers; ++p) {
        PlayerState& ps = s.players[p];
        ps.rng = player_generator(seed, p);
        shuffle(std::span<CardId>(ps.library.begin(), ps.library.size()), ps.rng);
    }
    return s;
}

}  // namespace mtg
