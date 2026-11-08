#pragma once

// Supporto comune ai test del motore: dati reali caricati una volta sola,
// ricerca di carte per nome e controllo degli invarianti dopo ogni spostamento.

#include "card_db.hpp"
#include "deck_loader.hpp"
#include "game_setup.hpp"
#include "game_state.hpp"
#include "zones.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace test {

inline const mtg::CardDatabase& real_db() {
    static const mtg::CardDatabase db = mtg::CardDatabase::load(CARDS_JSON);
    return db;
}

// Database reale più un token Gatto 1/1 bianco (come quelli di Ocelot Pride),
// che non sta in cards.json perché non è una carta delle liste.
inline const mtg::CardDatabase& db_with_tokens() {
    static const mtg::CardDatabase db = [] {
        mtg::CardDatabase d = real_db();
        mtg::CardDef cat;
        cat.name = "Cat Token";
        cat.full_name = "Cat Token";
        cat.layout = "token";
        cat.types = mtg::type_bit(mtg::CardType::Creature);
        cat.subtypes = {"Cat"};
        cat.colors = mtg::color_bit(mtg::Color::White);
        cat.has_pt = true;
        cat.base_power = 1;
        cat.base_toughness = 1;
        cat.is_token = true;
        d.add(std::move(cat));
        return d;
    }();
    return db;
}

inline const mtg::Decklist& uw_sim_deck() {
    static const mtg::Decklist d = mtg::load_decklist(std::string(DECKS_DIR) + "/uw_blink_sim.json");
    return d;
}

inline const mtg::Decklist& rakdos_deck() {
    static const mtg::Decklist d = mtg::load_decklist(std::string(DECKS_DIR) + "/rakdos_delirium.json");
    return d;
}

// Partita standard per i test: UW Blink (sim) giocatore 0, Rakdos giocatore 1.
inline mtg::GameState new_game(std::uint64_t seed = 1) {
    return mtg::create_game(db_with_tokens(), uw_sim_deck(), rakdos_deck(), seed);
}

// Le prime `n` carte con quel nome nella zona per-giocatore indicata.
inline std::vector<mtg::ObjRef> find_cards(const mtg::GameState& s, mtg::PlayerId player, mtg::Zone zone,
                                           std::string_view name, std::size_t n = 1) {
    const mtg::CardDatabase& db = db_with_tokens();
    const auto& p = s.players[player];
    const auto& list = zone == mtg::Zone::Library ? p.library
                       : zone == mtg::Zone::Hand  ? p.hand
                                                  : p.graveyard;
    std::vector<mtg::ObjRef> found;
    for (const mtg::CardId id : list) {
        if (db.get(s.objects[id].def).name == name && found.size() < n) found.push_back(s.ref(id));
    }
    REQUIRE(found.size() == n);
    return found;
}

// Ricalcolo da zero di controllo: va chiamato dopo ogni spostamento.
inline void require_invariants(const mtg::GameState& s) {
    const auto problems = mtg::check_invariants(s, db_with_tokens());
    for (const auto& p : problems) UNSCOPED_INFO(p);
    REQUIRE(problems.empty());
}

}  // namespace test
