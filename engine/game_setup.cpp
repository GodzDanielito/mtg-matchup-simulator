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

GameState create_game(const CardDatabase& db, const Decklist& player0, const Decklist& player1,
                      std::uint64_t seed) {
    GameState s{};
    s.rng = Pcg32(seed);
    place_deck(s, db, player0, 0);
    place_deck(s, db, player1, 1);
    for (PlayerId p = 0; p < kNumPlayers; ++p) {
        auto& library = s.players[p].library;
        shuffle(std::span<CardId>(library.begin(), library.size()), s.rng);
    }
    return s;
}

}  // namespace mtg
