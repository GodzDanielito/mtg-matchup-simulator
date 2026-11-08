// Test del caricatore delle liste (engine/deck_loader).
// Regole verificate: main di esattamente 60 carte, massimo 4 copie per nome,
// tranne le terre base.

#include "deck_loader.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <map>
#include <string>

using namespace mtg;
using Catch::Matchers::ContainsSubstring;

namespace {

// Costruisce un JSON di lista con un main dato come frammento di array.
std::string deck_json(const std::string& main_entries) {
    return R"({ "name": "Test", "format": "Modern", "source": "", "main": [)" +
           main_entries + "] }";
}

// 15 carte diverse: le prime 14 da 4 copie, l'ultima da `last_count`.
// Con last_count = 4 sono 60 carte valide.
std::string sixty_valid_cards(int last_count = 4) {
    std::string entries;
    for (int i = 0; i < 15; ++i) {
        if (i > 0) entries += ",";
        const int count = (i == 14) ? last_count : 4;
        entries += R"({ "card": "Card )" + std::to_string(i) + R"(", "count": )" +
                   std::to_string(count) + " }";
    }
    return entries;
}

}  // namespace

TEST_CASE("Una lista valida viene letta correttamente", "[decks]") {
    const Decklist deck = parse_decklist(deck_json(sixty_valid_cards()));

    CHECK(deck.name == "Test");
    CHECK(deck.format == "Modern");
    CHECK(deck.main.size() == 15);
    CHECK(deck.main_size() == 60);
    CHECK(validate_decklist(deck).empty());
}

TEST_CASE("Il main deve avere esattamente 60 carte", "[decks]") {
    SECTION("59 carte") {
        const auto deck = parse_decklist(deck_json(sixty_valid_cards(3)));
        REQUIRE(deck.main_size() == 59);
        const auto errors = validate_decklist(deck);
        REQUIRE(errors.size() == 1);
        CHECK_THAT(errors[0], ContainsSubstring("59"));
    }
    SECTION("61 carte") {
        const auto deck = parse_decklist(
            deck_json(sixty_valid_cards() + R"(, { "card": "Plains", "count": 1 })"));
        REQUIRE(deck.main_size() == 61);
        CHECK_FALSE(validate_decklist(deck).empty());
    }
    SECTION("Lista vuota") {
        const auto deck = parse_decklist(deck_json(""));
        CHECK_FALSE(validate_decklist(deck).empty());
    }
}

TEST_CASE("Massimo 4 copie per carta, tranne le terre base", "[decks]") {
    SECTION("5 copie di una carta non base: errore") {
        // 5 Lightning Bolt + 55 Swamp = 60 carte, ma Bolt supera il limite.
        const auto deck = parse_decklist(deck_json(
            R"({ "card": "Lightning Bolt", "count": 5 }, { "card": "Swamp", "count": 55 })"));
        const auto errors = validate_decklist(deck);
        REQUIRE(errors.size() == 1);
        CHECK_THAT(errors[0], ContainsSubstring("Lightning Bolt"));
    }
    SECTION("Copie della stessa carta su più righe vengono sommate") {
        const auto deck = parse_decklist(deck_json(
            R"({ "card": "Thoughtseize", "count": 3 },
               { "card": "Thoughtseize", "count": 2 },
               { "card": "Swamp", "count": 55 })"));
        const auto errors = validate_decklist(deck);
        REQUIRE(errors.size() == 1);
        CHECK_THAT(errors[0], ContainsSubstring("Thoughtseize"));
    }
    SECTION("Le terre base non hanno limite") {
        const auto deck = parse_decklist(deck_json(
            R"({ "card": "Plains", "count": 30 }, { "card": "Swamp", "count": 30 })"));
        CHECK(validate_decklist(deck).empty());
    }
}

TEST_CASE("Riconoscimento delle terre base", "[decks]") {
    CHECK(is_basic_land("Plains"));
    CHECK(is_basic_land("Swamp"));
    CHECK(is_basic_land("Island"));
    CHECK(is_basic_land("Mountain"));
    CHECK(is_basic_land("Forest"));
    CHECK(is_basic_land("Wastes"));
    CHECK(is_basic_land("Snow-Covered Swamp"));
    CHECK_FALSE(is_basic_land("Blood Crypt"));
    CHECK_FALSE(is_basic_land("Hallowed Fountain"));
    CHECK_FALSE(is_basic_land("plains"));  // i nomi sono esatti, maiuscole comprese
}

TEST_CASE("Voci malformate vengono rifiutate", "[decks]") {
    CHECK_THROWS_AS(parse_decklist("{ non è json"), DeckError);
    CHECK_THROWS_AS(parse_decklist(R"({ "name": "X" })"), DeckError);  // manca main
    CHECK_THROWS_AS(parse_decklist(deck_json(R"({ "card": "Bolt" })")), DeckError);
    CHECK_THROWS_AS(parse_decklist(deck_json(R"({ "card": "", "count": 4 })")), DeckError);
    CHECK_THROWS_AS(parse_decklist(deck_json(R"({ "card": "Bolt", "count": 0 })")), DeckError);
    CHECK_THROWS_AS(parse_decklist(deck_json(R"({ "card": "Bolt", "count": "4" })")), DeckError);
}

TEST_CASE("load_decklist rifiuta un file inesistente", "[decks]") {
    CHECK_THROWS_AS(load_decklist("config/decks/non_esiste.json"), DeckError);
}

// Questi due test controllano le liste vere in config/decks/:
// falliscono se qualcuno modifica una lista rendendola illegale.
TEST_CASE("Lista UW Blink legale", "[decks][config]") {
    const Decklist deck = load_decklist(std::string(DECKS_DIR) + "/uw_blink.json");
    CHECK(deck.name == "UW Blink");
    CHECK(deck.main_size() == 60);
    const auto errors = validate_decklist(deck);
    for (const auto& e : errors) UNSCOPED_INFO(e);
    CHECK(errors.empty());
}

TEST_CASE("Lista Rakdos Delirium legale", "[decks][config]") {
    const Decklist deck = load_decklist(std::string(DECKS_DIR) + "/rakdos_delirium.json");
    CHECK(deck.name == "Rakdos Delirium");
    CHECK(deck.main_size() == 60);
    const auto errors = validate_decklist(deck);
    for (const auto& e : errors) UNSCOPED_INFO(e);
    CHECK(errors.empty());
}

// La lista di simulazione sostituisce le carte rimandate (Aang, Mockingbird).
// Il test la confronta con la lista vera: se aggiorni uw_blink.json e dimentichi
// uw_blink_sim.json, fallisce.
TEST_CASE("Lista UW Blink sim legale e allineata alla lista vera", "[decks][config]") {
    const Decklist real = load_decklist(std::string(DECKS_DIR) + "/uw_blink.json");
    const Decklist sim = load_decklist(std::string(DECKS_DIR) + "/uw_blink_sim.json");

    CHECK(sim.main_size() == 60);
    const auto errors = validate_decklist(sim);
    for (const auto& e : errors) UNSCOPED_INFO(e);
    CHECK(errors.empty());

    auto to_map = [](const Decklist& d) {
        std::map<std::string, int> m;
        for (const auto& e : d.main) m[e.card] += e.count;
        return m;
    };

    // Lista attesa = lista vera con le due sostituzioni.
    auto expected = to_map(real);
    REQUIRE(expected["Aang, Swift Savior"] == 1);
    REQUIRE(expected["Mockingbird"] == 1);
    expected.erase("Aang, Swift Savior");
    expected.erase("Mockingbird");
    expected["Ephemerate"] += 1;       // al posto di Aang
    expected["Witch Enchanter"] += 1;  // al posto di Mockingbird

    CHECK(to_map(sim) == expected);
}
