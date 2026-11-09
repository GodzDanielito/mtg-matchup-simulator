// Test dei cambi di zona: spostamento di gruppo, evento unico, riepilogo del
// cimitero incrementale, incarnation e slot dei token.
// Dopo OGNI spostamento si confronta il riepilogo con il ricalcolo da zero.

#include "test_helpers.hpp"

using namespace mtg;
using test::db_with_tokens;
using test::find_cards;
using test::require_invariants;

namespace {

constexpr PlayerId kUw = 0;
constexpr PlayerId kRakdos = 1;

// Questi test verificano l'EVENTO di gruppo prodotto dagli spostamenti. Il trigger
// vero di Moonshadow, con i sei casi giocati davvero, è in test_rakdos_cards.cpp;
// qui la regola resta scritta come condizione sull'evento.
int moonshadow_counters_removed(const ZoneChangeBatch& batch, const CardObject& moonshadow) {
    if (moonshadow.counters.minus_one == 0) return 0;
    return permanent_cards_put_into_graveyard(batch, db_with_tokens(), moonshadow.controller) ? 1 : 0;
}

// Mette Moonshadow in campo con sei segnalini -1/-1 (spostamento di test, senza passare dal motore).
CardObject& moonshadow_on_battlefield(GameState& s, std::uint8_t minus_counters = 6) {
    const ObjRef card = find_cards(s, kRakdos, Zone::Library, "Moonshadow")[0];
    const ObjRef in_play = move_object(s, db_with_tokens(), card, Zone::Battlefield, Cause::Test).moves[0].new_ref;
    s.objects[in_play.card].counters.minus_one = minus_counters;
    require_invariants(s);
    return s.objects[in_play.card];
}

std::vector<ObjRef> concat(std::vector<ObjRef> a, const std::vector<ObjRef>& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

}  // namespace

// --- Sezione 12 di docs/stato_di_gioco.md: raggruppamento degli eventi ---

TEST_CASE("Moonshadow 1: tre terre macinate insieme sono UN evento", "[zones][moonshadow]") {
    GameState s = test::new_game(11);
    CardObject& moon = moonshadow_on_battlefield(s);

    const auto lands = concat(find_cards(s, kRakdos, Zone::Library, "Marsh Flats", 2),
                              find_cards(s, kRakdos, Zone::Library, "Swamp", 1));
    const ZoneChangeBatch batch = move_objects(s, db_with_tokens(), lands, Zone::Graveyard, Cause::Mill);
    require_invariants(s);

    CHECK(batch.moves.size() == 3);
    CHECK(batch.cause == Cause::Mill);
    for (const auto& m : batch.moves) {
        CHECK(m.from == Zone::Library);
        CHECK(m.to == Zone::Graveyard);
    }
    CHECK(moonshadow_counters_removed(batch, moon) == 1);
    CHECK(s.players[kRakdos].gy.card_count == 3);
}

TEST_CASE("Moonshadow 2: una terra e due istantanei, un evento, un segnalino", "[zones][moonshadow]") {
    GameState s = test::new_game(12);
    CardObject& moon = moonshadow_on_battlefield(s);

    const auto cards = concat(find_cards(s, kRakdos, Zone::Library, "Swamp", 1),
                              find_cards(s, kRakdos, Zone::Library, "Lightning Bolt", 2));
    const ZoneChangeBatch batch = move_objects(s, db_with_tokens(), cards, Zone::Graveyard, Cause::Mill);
    require_invariants(s);

    CHECK(batch.moves.size() == 3);
    CHECK(moonshadow_counters_removed(batch, moon) == 1);
}

TEST_CASE("Moonshadow 3: tre istantanei non contano", "[zones][moonshadow]") {
    GameState s = test::new_game(13);
    CardObject& moon = moonshadow_on_battlefield(s);

    const auto cards = concat(find_cards(s, kRakdos, Zone::Library, "Lightning Bolt", 2),
                              find_cards(s, kRakdos, Zone::Library, "Flare of Malice", 1));
    const ZoneChangeBatch batch = move_objects(s, db_with_tokens(), cards, Zone::Graveyard, Cause::Mill);
    require_invariants(s);

    CHECK(batch.moves.size() == 3);
    CHECK_FALSE(permanent_cards_put_into_graveyard(batch, db_with_tokens(), kRakdos));
    CHECK(moonshadow_counters_removed(batch, moon) == 0);
}

TEST_CASE("Moonshadow 4: due istruzioni separate sono due eventi", "[zones][moonshadow]") {
    GameState s = test::new_game(14);
    CardObject& moon = moonshadow_on_battlefield(s);

    const auto swamps = find_cards(s, kRakdos, Zone::Library, "Swamp", 2);
    const ZoneChangeBatch first = move_object(s, db_with_tokens(), swamps[0], Zone::Graveyard, Cause::Mill);
    require_invariants(s);
    const ZoneChangeBatch second = move_object(s, db_with_tokens(), swamps[1], Zone::Graveyard, Cause::Mill);
    require_invariants(s);

    CHECK(first.moves.size() == 1);
    CHECK(second.moves.size() == 1);
    CHECK(moonshadow_counters_removed(first, moon) + moonshadow_counters_removed(second, moon) == 2);
}

TEST_CASE("Moonshadow 5: un token che muore non conta come carta", "[zones][moonshadow]") {
    GameState s = test::new_game(15);
    // Moonshadow e il token dello stesso giocatore, così il cimitero è quello di Moonshadow.
    CardObject& moon = moonshadow_on_battlefield(s);
    const ObjRef cat = create_token(s, db_with_tokens(), db_with_tokens().id_of("Cat Token"), kRakdos);
    require_invariants(s);

    const ZoneChangeBatch batch = move_object(s, db_with_tokens(), cat, Zone::Graveyard, Cause::StateBased);
    require_invariants(s);

    REQUIRE(batch.moves.size() == 1);
    CHECK(batch.moves[0].is_token);
    CHECK_FALSE(permanent_cards_put_into_graveyard(batch, db_with_tokens(), kRakdos));
    CHECK(moonshadow_counters_removed(batch, moon) == 0);
    // Un token nel cimitero non conta per il riepilogo (delirium, Nethergoyf).
    CHECK(s.players[kRakdos].gy.card_count == 0);
}

TEST_CASE("Moonshadow 6: senza segnalini -1/-1 non perde nulla", "[zones][moonshadow]") {
    GameState s = test::new_game(16);
    CardObject& moon = moonshadow_on_battlefield(s, 0);

    const auto lands = find_cards(s, kRakdos, Zone::Library, "Swamp", 1);
    const ZoneChangeBatch batch = move_objects(s, db_with_tokens(), lands, Zone::Graveyard, Cause::Mill);
    require_invariants(s);

    // L'evento c'è e contiene una carta permanente, ma la condizione di Moonshadow è falsa.
    CHECK(permanent_cards_put_into_graveyard(batch, db_with_tokens(), kRakdos));
    CHECK(moonshadow_counters_removed(batch, moon) == 0);
}

TEST_CASE("Carte nel cimitero dell'avversario non contano per Moonshadow", "[zones][moonshadow]") {
    GameState s = test::new_game(17);
    CardObject& moon = moonshadow_on_battlefield(s);
    const auto plains = find_cards(s, kUw, Zone::Library, "Plains", 1);
    const ZoneChangeBatch batch = move_objects(s, db_with_tokens(), plains, Zone::Graveyard, Cause::Mill);
    require_invariants(s);
    CHECK(moonshadow_counters_removed(batch, moon) == 0);
}

// --- Riepilogo del cimitero ---

TEST_CASE("Riepilogo del cimitero: tipi, delirium, valore di mana", "[zones][graveyard]") {
    GameState s = test::new_game(21);
    const auto& gy = s.players[kRakdos].gy;
    auto mill = [&](std::string_view name, std::size_t n = 1) {
        move_objects(s, db_with_tokens(), find_cards(s, kRakdos, Zone::Library, name, n), Zone::Graveyard,
                     Cause::Mill);
        require_invariants(s);
    };

    mill("Lightning Bolt", 2);  // istantaneo ×2, valore 1+1
    CHECK(gy.card_count == 2);
    CHECK(gy.distinct_types() == 1);
    CHECK(gy.total_mana_value == 2);

    mill("Fear of Missing Out");  // incantesimo + creatura in una carta
    CHECK(gy.distinct_types() == 3);
    CHECK_FALSE(gy.delirium());

    mill("Swamp");  // terra: quarto tipo
    CHECK(gy.distinct_types() == 4);
    CHECK(gy.delirium());
    CHECK(gy.total_mana_value == 4);

    // Esiliare UN Bolt lascia l'altro: il tipo istantaneo resta.
    const ObjRef bolt = find_cards(s, kRakdos, Zone::Graveyard, "Lightning Bolt")[0];
    move_object(s, db_with_tokens(), bolt, Zone::Exile, Cause::Test);
    require_invariants(s);
    CHECK(gy.distinct_types() == 4);
    CHECK(gy.total_mana_value == 3);

    // Esiliare anche il secondo spegne il tipo e il delirium.
    const ObjRef bolt2 = find_cards(s, kRakdos, Zone::Graveyard, "Lightning Bolt")[0];
    move_object(s, db_with_tokens(), bolt2, Zone::Exile, Cause::Test);
    require_invariants(s);
    CHECK(gy.distinct_types() == 3);
    CHECK_FALSE(gy.delirium());
    CHECK(gy.card_count == 2);
}

TEST_CASE("Il cimitero mantiene l'ordine di arrivo", "[zones][graveyard]") {
    GameState s = test::new_game(22);
    const ObjRef a = find_cards(s, kRakdos, Zone::Library, "Swamp")[0];
    const ObjRef b = find_cards(s, kRakdos, Zone::Library, "Nethergoyf")[0];
    move_object(s, db_with_tokens(), a, Zone::Graveyard, Cause::Test);
    move_object(s, db_with_tokens(), b, Zone::Graveyard, Cause::Test);
    require_invariants(s);
    REQUIRE(s.players[kRakdos].graveyard.size() == 2);
    CHECK(s.players[kRakdos].graveyard[0] == a.card);
    CHECK(s.players[kRakdos].graveyard[1] == b.card);
}

// --- Incarnation: identità dell'oggetto di regole ---

TEST_CASE("Dopo un cambio di zona il vecchio ObjRef diventa invalido", "[zones][incarnation]") {
    GameState s = test::new_game(31);
    const ObjRef in_library = find_cards(s, kRakdos, Zone::Library, "Nethergoyf")[0];
    REQUIRE(s.is_valid(in_library));

    const ZoneChangeBatch b1 = move_object(s, db_with_tokens(), in_library, Zone::Battlefield, Cause::Test);
    const ObjRef on_battlefield = b1.moves[0].new_ref;
    require_invariants(s);

    CHECK_FALSE(s.is_valid(in_library));
    CHECK(s.is_valid(on_battlefield));
    CHECK(on_battlefield.card == in_library.card);  // stessa carta fisica
    CHECK(on_battlefield.incarnation != in_library.incarnation);
    CHECK(b1.moves[0].old_ref == in_library);

    // Ephemerate: esilio e ritorno, oggetto nuovo una seconda volta.
    const ObjRef exiled = move_object(s, db_with_tokens(), on_battlefield, Zone::Exile, Cause::Test).moves[0].new_ref;
    const ObjRef back = move_object(s, db_with_tokens(), exiled, Zone::Battlefield, Cause::Test).moves[0].new_ref;
    require_invariants(s);
    CHECK_FALSE(s.is_valid(on_battlefield));  // un bersaglio di Solitude sarebbe ora illegale
    CHECK(s.is_valid(back));
}

TEST_CASE("Spostare un ObjRef invalido produce un errore", "[zones][incarnation]") {
    GameState s = test::new_game(32);
    const ObjRef old = find_cards(s, kRakdos, Zone::Library, "Swamp")[0];
    move_object(s, db_with_tokens(), old, Zone::Graveyard, Cause::Test);
    CHECK_THROWS_AS(move_object(s, db_with_tokens(), old, Zone::Exile, Cause::Test), EngineError);
    require_invariants(s);
}

TEST_CASE("Spostamenti non validi vengono rifiutati senza modificare lo stato", "[zones]") {
    GameState s = test::new_game(33);
    const ObjRef swamp = find_cards(s, kRakdos, Zone::Library, "Swamp")[0];
    const ObjRef bolt = find_cards(s, kRakdos, Zone::Library, "Lightning Bolt")[0];

    // La stessa carta due volte nello stesso gruppo: rifiutato prima di muovere
    // qualsiasi carta, anche quelle valide del gruppo (il Bolt).
    const std::vector<ObjRef> twice{bolt, swamp, swamp};
    CHECK_THROWS_AS(move_objects(s, db_with_tokens(), twice, Zone::Graveyard, Cause::Test), EngineError);
    // Verso la zona in cui già si trova.
    CHECK_THROWS_AS(move_object(s, db_with_tokens(), swamp, Zone::Library, Cause::Test), EngineError);

    // Niente è cambiato: stessi oggetti validi, stesse zone.
    CHECK(s.is_valid(swamp));
    CHECK(s.is_valid(bolt));
    CHECK(s.objects[bolt.card].zone == Zone::Library);
    CHECK(s.players[kRakdos].library.size() == 60);
    CHECK(s.players[kRakdos].graveyard.empty());
    require_invariants(s);
}

TEST_CASE("Entrare in campo azzera lo stato dell'oggetto", "[zones]") {
    GameState s = test::new_game(34);
    const ObjRef goyf = find_cards(s, kRakdos, Zone::Library, "Nethergoyf")[0];
    const ObjRef in_play = move_object(s, db_with_tokens(), goyf, Zone::Battlefield, Cause::Test).moves[0].new_ref;
    CardObject& o = s.objects[in_play.card];
    CHECK(o.has_flag(ObjectFlag::SummoningSick));
    o.damage = 2;
    o.counters.plus_one = 1;
    o.set_flag(ObjectFlag::Tapped);
    o.set_flag(ObjectFlag::AttackedThisTurn);

    const ObjRef exiled = move_object(s, db_with_tokens(), in_play, Zone::Exile, Cause::Test).moves[0].new_ref;
    const CardObject& e = s.objects[exiled.card];
    CHECK(e.damage == 0);
    CHECK(e.counters.plus_one == 0);
    CHECK_FALSE(e.has_flag(ObjectFlag::Tapped));
    CHECK_FALSE(e.has_flag(ObjectFlag::AttackedThisTurn));  // Fear of Missing Out: oggetto nuovo
    require_invariants(s);
}

// --- Token: 32 slot riciclabili ---

TEST_CASE("Token: slot riciclato con incarnation che sale", "[zones][tokens]") {
    GameState s = test::new_game(41);
    const CardDefId cat = db_with_tokens().id_of("Cat Token");

    const ObjRef first = create_token(s, db_with_tokens(), cat, kUw);
    CHECK(first.card >= kMaxCards);
    CHECK(s.objects[first.card].has_flag(ObjectFlag::Token));
    require_invariants(s);

    const ObjRef dead = move_object(s, db_with_tokens(), first, Zone::Graveyard, Cause::Test).moves[0].new_ref;
    remove_token(s, dead);  // azione di stato 704.5d (arriverà con le azioni di stato)
    require_invariants(s);
    CHECK(s.objects[first.card].zone == Zone::None);

    const ObjRef second = create_token(s, db_with_tokens(), cat, kUw);
    CHECK(second.card == first.card);  // stesso slot
    CHECK(second.incarnation > dead.incarnation);
    CHECK_FALSE(s.is_valid(first));
    CHECK_FALSE(s.is_valid(dead));
    CHECK(s.is_valid(second));
    require_invariants(s);
}

TEST_CASE("Token: un token oltre gli slot disponibili produce un errore esplicito", "[zones][tokens]") {
    GameState s = test::new_game(42);
    const CardDefId cat = db_with_tokens().id_of("Cat Token");
    for (std::size_t i = 0; i < kMaxTokens; ++i) (void)create_token(s, db_with_tokens(), cat, kUw);
    require_invariants(s);
    CHECK_THROWS_AS(create_token(s, db_with_tokens(), cat, kUw), EngineLimitError);
}

TEST_CASE("Solo i token si possono rimuovere dal gioco", "[zones][tokens]") {
    GameState s = test::new_game(43);
    const ObjRef swamp = find_cards(s, kRakdos, Zone::Library, "Swamp")[0];
    CHECK_THROWS_AS(remove_token(s, swamp), EngineError);
}
