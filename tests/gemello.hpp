#pragma once

// Lo stato gemello dei test della determinizzazione e della scelta (docs/ricerca.md, sezione 3.5, test (a) e (b)).

#include "game_state.hpp"
#include "rng.hpp"

#include <span>
#include <vector>

namespace test {

// Uno stato che differisce dal vero solo in cio' che `viewer` non sa.
//
// Per ogni proprietario, le posizioni nascoste a `viewer` (mano avversaria non nota, grimorio nelle posizioni non
// note) si scambiano gli oggetti (i CardId, con la loro incarnation: la traccia che la forma canonica deve togliere)
// e, separatamente, le definizioni. Ogni posizione tiene le sue proprieta' pubbliche: zona, turno di arrivo, chi la
// conosce. I generatori dei due giocatori diventano altri.
inline mtg::GameState twin_of(const mtg::GameState& s, mtg::PlayerId viewer, mtg::Pcg32& rng) {
    using namespace mtg;
    GameState t = s;
    const auto bit = static_cast<std::uint8_t>(1u << viewer);
    for (PlayerId p = 0; p < kNumPlayers; ++p) {
        struct Pos {
            PlayerZone* list;
            std::size_t index;
            Zone zone;
            std::uint16_t zone_turn;
            std::uint8_t known_to;
        };
        std::vector<Pos> pos;
        std::vector<CardId> ids;
        std::vector<CardDefId> defs;
        auto collect = [&](PlayerZone& list, Zone zone) {
            for (std::size_t i = 0; i < list.size(); ++i) {
                const CardObject& o = t.objects[list[i]];
                if ((o.known_to & bit) != 0) continue;
                pos.push_back(Pos{&list, i, zone, o.zone_turn, o.known_to});
                ids.push_back(list[i]);
                defs.push_back(o.def);
            }
        };
        if (p != viewer) collect(t.players[p].hand, Zone::Hand);
        collect(t.players[p].library, Zone::Library);
        shuffle(std::span<CardId>(ids), rng);
        shuffle(std::span<CardDefId>(defs), rng);
        for (std::size_t k = 0; k < pos.size(); ++k) {
            (*pos[k].list)[pos[k].index] = ids[k];
            CardObject& o = t.objects[ids[k]];
            o.zone = pos[k].zone;
            o.zone_turn = pos[k].zone_turn;
            o.known_to = pos[k].known_to;
            o.def = defs[k];
        }
    }
    for (PlayerId p = 0; p < kNumPlayers; ++p) t.players[p].rng = Pcg32(rng.next(), 0x1234 + p);
    return t;
}

}  // namespace test
