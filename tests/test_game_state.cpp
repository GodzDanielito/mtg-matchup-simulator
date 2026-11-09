// Test della struttura dello stato di gioco: copia, dimensione, valori iniziali.

#include "test_helpers.hpp"

#include <chrono>
#include <iostream>
#include <type_traits>

using namespace mtg;

static_assert(std::is_trivially_copyable_v<GameState>,
              "GameState deve copiarsi con un memcpy (ricerca ad albero)");

TEST_CASE("Dimensione di GameState", "[state]") {
    const GameState s{};
    const CombatState& c = s.combat;
    auto row = [](const char* name, std::size_t bytes) {
        std::cout << "  " << name << std::string(30 - std::string(name).size(), ' ') << bytes << " byte\n";
    };
    std::cout << '\n';
    row("GameState", sizeof(GameState));
    row("  objects (184 CardObject)", sizeof(s.objects));
    row("  players (2 PlayerState)", sizeof(s.players));
    row("    di cui zone (3 x 2)", 6 * sizeof(PlayerZone));
    row("  battlefield", sizeof(s.battlefield));
    row("  exile", sizeof(s.exile));
    row("  stack (zona)", sizeof(s.stack));
    row("  stack_items", sizeof(s.stack_items));
    row("  pending_triggers", sizeof(s.pending_triggers));
    row("  legend_keep", sizeof(s.legend_keep));
    row("  delayed", sizeof(s.delayed));
    row("  combat", sizeof(s.combat));
    row("    attackers", sizeof(c.attackers));
    row("    blocks", sizeof(c.blocks));
    row("    candidates+class+choice", sizeof(c.candidates) + sizeof(c.candidate_class) + sizeof(c.choice));
    row("    assignments", sizeof(c.assignments));
    row("  pending", sizeof(s.pending));
    row("  turn", sizeof(s.turn));
    row("  rng", sizeof(s.rng));
    row("CardObject", sizeof(CardObject));
    row("StackItem", sizeof(StackItem));

    // Costo di una copia (quello che paga la ricerca ad albero a ogni simulazione).
    constexpr int kCopies = 200000;
    std::vector<GameState> buffer(2, test::new_game(1));
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < kCopies; ++i) buffer[static_cast<std::size_t>(i & 1)] = buffer[static_cast<std::size_t>((i + 1) & 1)];
    const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / kCopies;
    std::cout << "  copia di un GameState          " << ns << " ns\n";
    CHECK(buffer[0].players[0].library.size() == 60);
    // Tetto indicativo: se lo stato cresce oltre, va discusso prima di accettarlo.
    CHECK(sizeof(GameState) <= 12 * 1024);
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
