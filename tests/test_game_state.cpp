// Test della struttura dello stato di gioco: copia, dimensione, valori iniziali.

#include "test_helpers.hpp"

#include <iostream>
#include <type_traits>

using namespace mtg;

static_assert(std::is_trivially_copyable_v<GameState>,
              "GameState deve copiarsi con un memcpy (ricerca ad albero)");

TEST_CASE("Dimensione di GameState", "[state]") {
    std::cout << "\n  sizeof(GameState)   = " << sizeof(GameState) << " byte\n"
              << "  sizeof(CardObject)  = " << sizeof(CardObject) << " byte\n"
              << "  sizeof(PlayerState) = " << sizeof(PlayerState) << " byte\n";
    // Tetto indicativo: se lo stato cresce oltre, va discusso prima di accettarlo.
    CHECK(sizeof(GameState) <= 8 * 1024);
}

TEST_CASE("La copia dello stato non tocca l'originale", "[state]") {
    GameState original = test::new_game(5);
    GameState copy = original;

    const ObjRef bolt = test::find_cards(copy, 1, Zone::Library, "Lightning Bolt")[0];
    move_object(copy, test::db_with_tokens(), bolt, Zone::Graveyard, Cause::Test);
    copy.players[0].life = 7;

    CHECK(original.players[0].life == 20);
    CHECK(original.players[1].graveyard.empty());
    CHECK(original.objects[bolt.card].zone == Zone::Library);
    CHECK(copy.players[1].graveyard.size() == 1);
}

TEST_CASE("StaticVector: capacita fissa ed errore esplicito", "[state]") {
    StaticVector<int, 3> v;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);
    CHECK_THROWS_AS(v.push_back(4), EngineLimitError);

    v.erase_at(0);  // mantiene l'ordine
    REQUIRE(v.size() == 2);
    CHECK(v[0] == 2);
    CHECK(v[1] == 3);

    v.insert(0, 9);
    CHECK(v[0] == 9);
    CHECK(v.find(3) == 2);
    CHECK(v.find(42) == v.npos);
}
