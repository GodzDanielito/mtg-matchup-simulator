// Test degli spostamenti verso il grimorio: cima, fondo, mescolato.
//
// Convenzione: i riferimenti passati a move_objects sono elencati DALL'ALTO
// VERSO IL BASSO del gruppo che si forma nel grimorio. Nel grimorio l'indice 0
// è il fondo e back() è la cima.

#include "test_helpers.hpp"

#include <algorithm>
#include <vector>

using namespace mtg;
using test::db_with_tokens;
using test::find_cards;
using test::require_invariants;

namespace {

constexpr PlayerId kRakdos = 1;

// Pesca `n` carte dalla cima e restituisce i riferimenti in mano.
std::vector<ObjRef> draw(GameState& s, PlayerId p, std::size_t n) {
    std::vector<ObjRef> refs;
    for (std::size_t i = 0; i < n; ++i) {
        const CardId top = s.players[p].library.back();
        refs.push_back(move_object(s, db_with_tokens(), s.ref(top), Zone::Hand, Cause::Draw).moves[0].new_ref);
    }
    require_invariants(s);
    return refs;
}

std::vector<CardId> library_of(const GameState& s, PlayerId p) {
    return {s.players[p].library.begin(), s.players[p].library.end()};
}

}  // namespace

TEST_CASE("Verso il grimorio la posizione e' obbligatoria", "[zones][library]") {
    GameState s = test::new_game(51);
    const auto hand = draw(s, kRakdos, 1);
    CHECK_THROWS_AS(move_object(s, db_with_tokens(), hand[0], Zone::Library, Cause::Test), EngineError);
    // Rifiutato senza modifiche.
    CHECK(s.is_valid(hand[0]));
    CHECK(s.players[kRakdos].library.size() == 59);
    require_invariants(s);
}

TEST_CASE("Posizione solo per il grimorio", "[zones][library]") {
    GameState s = test::new_game(52);
    const auto hand = draw(s, kRakdos, 1);
    CHECK_THROWS_AS(
        move_object(s, db_with_tokens(), hand[0], Zone::Graveyard, Cause::Test, LibraryPosition::Top),
        EngineError);
    require_invariants(s);
}

TEST_CASE("In cima: il primo dell'elenco diventa la carta in cima", "[zones][library]") {
    GameState s = test::new_game(53);
    const auto hand = draw(s, kRakdos, 2);
    const std::vector<ObjRef> order{hand[0], hand[1]};  // hand[0] in cima, poi hand[1]
    const auto batch =
        move_objects(s, db_with_tokens(), order, Zone::Library, Cause::Test, LibraryPosition::Top);
    require_invariants(s);

    REQUIRE(batch.moves.size() == 2);
    const auto& lib = s.players[kRakdos].library;
    CHECK(lib.size() == 60);
    CHECK(lib[59] == hand[0].card);
    CHECK(lib[58] == hand[1].card);
}

TEST_CASE("In fondo nell'ordine scelto (mulligan London)", "[zones][library]") {
    GameState s = test::new_game(54);
    const auto hand = draw(s, kRakdos, 7);
    const std::vector<CardId> before = library_of(s, kRakdos);

    // Il giocatore tiene 4 carte e ne mette 3 in fondo, nell'ordine c, a, b (dall'alto al basso).
    const std::vector<ObjRef> bottom{hand[2], hand[0], hand[1]};
    move_objects(s, db_with_tokens(), bottom, Zone::Library, Cause::Test, LibraryPosition::Bottom);
    require_invariants(s);

    const auto& lib = s.players[kRakdos].library;
    REQUIRE(lib.size() == 56);
    CHECK(lib[0] == hand[1].card);  // la più in fondo è l'ultima dell'elenco
    CHECK(lib[1] == hand[0].card);
    CHECK(lib[2] == hand[2].card);
    // Il resto del grimorio non si è mosso.
    for (std::size_t i = 0; i < before.size(); ++i) CHECK(lib[i + 3] == before[i]);
    CHECK(s.players[kRakdos].hand.size() == 4);
}

TEST_CASE("Mescolato: usa il generatore dello stato ed e' riproducibile", "[zones][library]") {
    GameState a = test::new_game(55);
    const auto hand = draw(a, kRakdos, 1);
    GameState b = a;  // stessa partita, stesso generatore

    move_object(a, db_with_tokens(), hand[0], Zone::Library, Cause::Test, LibraryPosition::Shuffle);
    move_object(b, db_with_tokens(), hand[0], Zone::Library, Cause::Test, LibraryPosition::Shuffle);
    require_invariants(a);

    CHECK(a.players[kRakdos].library.size() == 60);
    CHECK(library_of(a, kRakdos) == library_of(b, kRakdos));

    // Il grimorio è stato davvero rimescolato, non solo allungato.
    GameState c = test::new_game(55);
    (void)draw(c, kRakdos, 1);
    const std::vector<CardId> unshuffled = library_of(c, kRakdos);
    const std::vector<CardId> shuffled = library_of(a, kRakdos);
    CHECK_FALSE(std::equal(unshuffled.begin(), unshuffled.end(), shuffled.begin()));
}

TEST_CASE("Mescolare il grimorio senza spostare carte (fetchland, Troll)", "[zones][library]") {
    GameState a = test::new_game(56);
    // Una fetchland cerca una terra: la carta va in campo, poi il grimorio si mescola.
    const ObjRef swamp = find_cards(a, kRakdos, Zone::Library, "Swamp")[0];
    move_object(a, db_with_tokens(), swamp, Zone::Battlefield, Cause::Test);
    GameState b = a;
    const std::vector<CardId> before = library_of(a, kRakdos);

    shuffle_library(a, kRakdos);
    shuffle_library(b, kRakdos);
    require_invariants(a);

    CHECK(library_of(a, kRakdos) == library_of(b, kRakdos));  // stesso generatore, stesso ordine
    CHECK(library_of(a, kRakdos) != before);

    std::vector<CardId> sorted_before = before, sorted_after = library_of(a, kRakdos);
    std::sort(sorted_before.begin(), sorted_before.end());
    std::sort(sorted_after.begin(), sorted_after.end());
    CHECK(sorted_before == sorted_after);  // stesse carte

    // Il grimorio dell'avversario non si muove.
    CHECK(library_of(a, 0) == library_of(test::new_game(56), 0));
}

TEST_CASE("Mescolare fa dimenticare la posizione delle carte del grimorio", "[zones][library]") {
    // Dopo Mishra's Bauble Rakdos conosce la cima del grimorio avversario; dopo un
    // mescolamento quella conoscenza non vale più. Le carte in mano restano note.
    GameState s = test::new_game(57);
    const auto hand = draw(s, 0, 1);
    s.objects[hand[0].card].known_to = 0b11;
    for (const CardId id : s.players[0].library) s.objects[id].known_to = 0b10;

    shuffle_library(s, 0);
    for (const CardId id : s.players[0].library) CHECK(s.objects[id].known_to == 0);
    CHECK(s.objects[hand[0].card].known_to == 0b11);
    require_invariants(s);
}
