// Test del database delle carte (CardDef caricate da config/cards.json).

#include "test_helpers.hpp"

#include <catch2/matchers/catch_matchers_string.hpp>

using namespace mtg;

TEST_CASE("Analisi dei costi di mana", "[cards]") {
    SECTION("Costo con generico e colorato") {
        const ManaCost c = parse_mana_cost("{3}{B}{B}");
        CHECK(c.generic == 3);
        CHECK(c.colored[static_cast<int>(Color::Black)] == 2);
        CHECK(c.x == 0);
    }
    SECTION("X") {
        const ManaCost c = parse_mana_cost("{X}{W}");
        CHECK(c.x == 1);
        CHECK(c.colored[static_cast<int>(Color::White)] == 1);
    }
    SECTION("Costo vuoto e {0}") {
        CHECK(parse_mana_cost("") == ManaCost{});
        CHECK(parse_mana_cost("{0}") == ManaCost{});
    }
    SECTION("Simboli non gestiti: errore esplicito") {
        CHECK_THROWS_AS(parse_mana_cost("{W/U}"), CardDataError);
        CHECK_THROWS_AS(parse_mana_cost("{B/P}"), CardDataError);
        CHECK_THROWS_AS(parse_mana_cost("{3"), CardDataError);
    }
}

TEST_CASE("Ogni carta delle tre liste presente in cards.json", "[cards][config]") {
    const CardDatabase& db = test::real_db();
    for (const char* file : {"/uw_blink.json", "/uw_blink_sim.json", "/rakdos_delirium.json"}) {
        const Decklist deck = load_decklist(std::string(DECKS_DIR) + file);
        for (const auto& entry : deck.main) {
            INFO(file << ": " << entry.card);
            CHECK(db.find(entry.card) != nullptr);
        }
    }
}

TEST_CASE("Caratteristiche lette da cards.json", "[cards]") {
    const CardDatabase& db = test::real_db();

    SECTION("Fear of Missing Out: incantesimo e creatura") {
        const CardDef& c = *db.find("Fear of Missing Out");
        CHECK(c.has_type(CardType::Enchantment));
        CHECK(c.has_type(CardType::Creature));
        CHECK(c.mana_value == 2);
        CHECK(c.is_permanent_card());
    }
    SECTION("Nethergoyf: forza e costituzione con asterisco") {
        const CardDef& c = *db.find("Nethergoyf");
        CHECK(c.power_star);
        CHECK(c.base_power == 0);
        CHECK(c.toughness_star);
        CHECK(c.base_toughness == 1);
    }
    SECTION("Moonshadow stampata 7/7") {
        const CardDef& c = *db.find("Moonshadow");
        CHECK(c.base_power == 7);
        CHECK(c.base_toughness == 7);
        CHECK_FALSE(c.power_star);
    }
    SECTION("Teferi: leggendario con fedeltà") {
        const CardDef& c = *db.find("Teferi, Time Raveler");
        CHECK(c.has_supertype(Supertype::Legendary));
        CHECK(c.loyalty == 4);
        CHECK(c.colors == (color_bit(Color::White) | color_bit(Color::Blue)));
    }
    SECTION("Lightning Bolt non è una carta permanente") {
        CHECK_FALSE(db.find("Lightning Bolt")->is_permanent_card());
    }
    SECTION("Terra base") {
        CHECK(db.find("Swamp")->has_supertype(Supertype::Basic));
        CHECK(db.find("Swamp")->has_type(CardType::Land));
    }
    SECTION("Nome con caratteri non ASCII") {
        REQUIRE(db.find("Troll of Khazad-dûm") != nullptr);
        CHECK(db.find("Troll of Khazad-dûm")->mana_value == 6);
    }
    SECTION("MDFC: il nome della lista è il fronte, il retro è collegato") {
        const CardDef& front = *db.find("Witch Enchanter");
        CHECK(front.has_type(CardType::Creature));
        CHECK_FALSE(front.has_type(CardType::Land));
        REQUIRE(front.back_face != kNoDef);
        const CardDef& back = db.get(front.back_face);
        CHECK(back.name == "Witch-Blessed Meadow");
        CHECK(back.has_type(CardType::Land));
        // Il retro non si può usare come nome nelle liste.
        CHECK(db.find("Witch-Blessed Meadow") == nullptr);
    }
}

TEST_CASE("Errori del database", "[cards]") {
    CHECK_THROWS_AS(CardDatabase::from_json("{ non json"), CardDataError);
    CHECK_THROWS_AS(CardDatabase::load("non_esiste.json"), CardDataError);
    CHECK_THROWS_AS(test::real_db().id_of("Lightning Blot"), CardDataError);

    CardDatabase db = test::real_db();
    CardDef dup;
    dup.name = "Lightning Bolt";
    CHECK_THROWS_AS(db.add(dup), CardDataError);
}
