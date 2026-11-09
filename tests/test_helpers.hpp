#pragma once

// Supporto comune ai test del motore: dati reali caricati una volta sola,
// ricerca di carte per nome, partite preparate e controllo degli invarianti.

#include "agents.hpp"
#include "all_cards.hpp"
#include "card_db.hpp"
#include "combat.hpp"
#include "deck_loader.hpp"
#include "game.hpp"
#include "game_setup.hpp"
#include "game_state.hpp"
#include "mana.hpp"
#include "zones.hpp"

#include <catch2/catch_test_macros.hpp>

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace test {

// Database reale con le regole delle carte implementate (cards/).
inline const mtg::CardDatabase& real_db() {
    static const mtg::CardDatabase db = [] {
        mtg::CardDatabase d = mtg::CardDatabase::load(CARDS_JSON);
        mtg::register_all_cards(d);
        return d;
    }();
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

// Prima carta con quel nome (nome del fronte) nella zona comune indicata.
inline mtg::ObjRef find_on_battlefield(const mtg::GameState& s, std::string_view name, std::size_t skip = 0) {
    for (const mtg::CardId id : s.battlefield) {
        if (db_with_tokens().get(s.objects[id].def).name == name) {
            if (skip == 0) return s.ref(id);
            --skip;
        }
    }
    FAIL("carta non trovata in campo: " << name);
    return {};
}

// Ricalcolo da zero di controllo: va chiamato dopo ogni spostamento.
inline void require_invariants(const mtg::GameState& s) {
    const auto problems = mtg::check_invariants(s, db_with_tokens());
    for (const auto& p : problems) UNSCOPED_INFO(p);
    REQUIRE(problems.empty());
}

// --- Partite preparate ---

struct Staged {
    mtg::GameState s;
    mtg::GameLog log;
};

// Sposta dal grimorio di `player` le carte nominate nella zona indicata (setup dei test).
inline void put(mtg::GameState& s, mtg::PlayerId player, std::initializer_list<const char*> names, mtg::Zone to) {
    for (const char* name : names) {
        const mtg::ObjRef card = find_cards(s, player, mtg::Zone::Library, name)[0];
        mtg::move_object(s, db_with_tokens(), card, to, mtg::Cause::Test);
    }
}

// Partita senza mulligan: mani e campo preparati, poi start_game. Il motore
// avanza fino alla prima decisione.
inline Staged staged(mtg::PlayerId starting, std::initializer_list<const char*> hand0,
                     std::initializer_list<const char*> hand1, std::initializer_list<const char*> field0 = {},
                     std::initializer_list<const char*> field1 = {}, std::uint64_t seed = 1) {
    Staged st{new_game(seed), {}};
    put(st.s, 0, hand0, mtg::Zone::Hand);
    put(st.s, 1, hand1, mtg::Zone::Hand);
    put(st.s, 0, field0, mtg::Zone::Battlefield);
    put(st.s, 1, field1, mtg::Zone::Battlefield);
    mtg::start_game(st.s, db_with_tokens(), mtg::StartOptions{starting, false}, &st.log);
    require_invariants(st.s);
    return st;
}

inline std::vector<mtg::Action> legal(const mtg::GameState& s) { return mtg::legal_actions(s, db_with_tokens()); }

inline void act(Staged& st, const mtg::Action& a) {
    mtg::apply(st.s, db_with_tokens(), a, &st.log);
    require_invariants(st.s);
}

inline mtg::Action pass() { return mtg::Action{mtg::ActionType::Pass}; }
inline mtg::Action option(std::uint8_t value) { return mtg::Action{mtg::ActionType::ChooseOption, mtg::kNoCard, value}; }

// L'azione legale di quel tipo sulla carta con quel nome (del fronte) e quel valore.
inline mtg::Action find_action(const mtg::GameState& s, mtg::ActionType type, std::string_view name,
                               std::uint8_t value = 0) {
    for (const mtg::Action& a : legal(s)) {
        if (a.type == type && a.value == value && a.card != mtg::kNoCard &&
            db_with_tokens().get(s.objects[a.card].def).name == name) {
            return a;
        }
    }
    FAIL("azione legale non trovata per " << name);
    return {};
}

inline bool is_decision(const mtg::GameState& s, mtg::DecisionKind kind, mtg::PlayerId player) {
    return s.pending.kind == kind && s.pending.player == player;
}

}  // namespace test
