// Test del pagamento del mana (docs/stato_di_gioco.md, sezione 10).
//
// Le terre stappate si raggruppano per insieme di colori prodotti; un modo di
// pagare è un vettore di conteggi per classe. Ogni opzione lascia un insieme
// DIVERSO di fonti di mana disponibili.

#include "test_helpers.hpp"

#include <algorithm>
#include <set>

using namespace mtg;
using namespace test;

namespace {

constexpr PlayerId kUw = 0;
constexpr PlayerId kRakdos = 1;

std::vector<PaymentOption> options(const GameState& s, PlayerId p, const char* cost) {
    return payment_options(s, db_with_tokens(), p, parse_mana_cost(cost));
}

// Nomi delle terre tappate da un'opzione, ordinati.
std::multiset<std::string> names(const GameState& s, const PaymentOption& o) {
    std::multiset<std::string> out;
    for (const CardId id : o.lands) out.insert(db_with_tokens().get(s.objects[id].def).name);
    return out;
}

}  // namespace

TEST_CASE("Pagamento: terre identiche danno un solo modo", "[mana]") {
    GameState s = new_game(1);
    put(s, kRakdos, {"Swamp", "Swamp"}, Zone::Battlefield);
    const auto opts = options(s, kRakdos, "{B}");
    REQUIRE(opts.size() == 1);
    CHECK(opts[0].lands.size() == 1);
}

TEST_CASE("Pagamento: due classi, due insiemi rimanenti diversi", "[mana]") {
    GameState s = new_game(1);
    put(s, kRakdos, {"Swamp", "Blood Crypt"}, Zone::Battlefield);
    const auto opts = options(s, kRakdos, "{B}");
    REQUIRE(opts.size() == 2);
    CHECK(names(s, opts[0]) != names(s, opts[1]));  // tappare la Swamp o la Blood Crypt
}

TEST_CASE("Pagamento: il colore obbligato riduce le opzioni", "[mana]") {
    GameState s = new_game(1);
    put(s, kRakdos, {"Swamp", "Blood Crypt"}, Zone::Battlefield);
    const auto opts = options(s, kRakdos, "{R}");
    REQUIRE(opts.size() == 1);
    CHECK(names(s, opts[0]) == std::multiset<std::string>{"Blood Crypt"});
}

TEST_CASE("Pagamento: costo impossibile, nessuna opzione", "[mana]") {
    GameState s = new_game(1);
    put(s, kRakdos, {"Swamp", "Swamp"}, Zone::Battlefield);
    CHECK(options(s, kRakdos, "{R}").empty());
    CHECK(options(s, kRakdos, "{2}{B}").empty());  // servono 3 mana, ci sono 2 terre
}

TEST_CASE("Pagamento: le terre con gli stessi colori sono una sola classe", "[mana]") {
    // 2 Swamp, 2 Blood Crypt, Blackcleave Cliffs, Raucous Theater: 6 sottoinsiemi
    // da una terra, ma solo due risultati diversi (resta una Swamp in meno, o una
    // terra B/R in meno).
    GameState s = new_game(1);
    put(s, kRakdos, {"Swamp", "Swamp", "Blood Crypt", "Blood Crypt", "Blackcleave Cliffs", "Raucous Theater"},
        Zone::Battlefield);
    CHECK(options(s, kRakdos, "{B}").size() == 2);
    CHECK(options(s, kRakdos, "{R}").size() == 1);
}

TEST_CASE("Pagamento: Street Wraith {3}{B}{B}", "[mana]") {
    GameState s = new_game(1);
    put(s, kRakdos, {"Swamp", "Swamp", "Blood Crypt", "Blood Crypt", "Blood Crypt"}, Zone::Battlefield);
    CHECK(options(s, kRakdos, "{3}{B}{B}").size() == 1);  // si tappa tutto

    put(s, kRakdos, {"Blackcleave Cliffs"}, Zone::Battlefield);
    // 2 Swamp + 4 terre B/R, si tappano 5 terre: resta una Swamp o una B/R.
    CHECK(options(s, kRakdos, "{3}{B}{B}").size() == 2);
}

TEST_CASE("Pagamento: assegnazione dei colori (teorema di Hall)", "[mana]") {
    GameState s = new_game(1);
    put(s, kUw, {"Plains", "Plains", "Hallowed Fountain"}, Zone::Battlefield);
    // {W}{U}: due Plains non bastano, serve la Fountain per il blu.
    const auto wu = options(s, kUw, "{W}{U}");
    REQUIRE(wu.size() == 1);
    CHECK(names(s, wu[0]) == std::multiset<std::string>{"Hallowed Fountain", "Plains"});
    CHECK(options(s, kUw, "{U}{U}").empty());  // una sola fonte blu
    CHECK(options(s, kUw, "{1}{W}").size() == 2);  // resta una Plains o la Fountain
}

TEST_CASE("Pagamento: contano solo le terre stappate del giocatore", "[mana]") {
    GameState s = new_game(1);
    put(s, kRakdos, {"Swamp", "Blood Crypt"}, Zone::Battlefield);
    put(s, kUw, {"Plains"}, Zone::Battlefield);
    const ObjRef swamp = find_on_battlefield(s, "Swamp");
    s.objects[swamp.card].set_flag(ObjectFlag::Tapped);
    const auto opts = options(s, kRakdos, "{B}");
    REQUIRE(opts.size() == 1);
    CHECK(names(s, opts[0]) == std::multiset<std::string>{"Blood Crypt"});
}

TEST_CASE("Pagamento: costo zero, un'opzione senza terre", "[mana]") {
    GameState s = new_game(1);
    const auto opts = options(s, kRakdos, "{0}");
    REQUIRE(opts.size() == 1);
    CHECK(opts[0].lands.empty());
}

TEST_CASE("Pagamento: si tappano le terre e la riserva torna a zero", "[mana]") {
    GameState s = new_game(1);
    put(s, kRakdos, {"Swamp", "Blood Crypt"}, Zone::Battlefield);
    const ManaCost cost = parse_mana_cost("{B}");
    const auto opts = payment_options(s, db_with_tokens(), kRakdos, cost);
    const auto it = std::find_if(opts.begin(), opts.end(), [&](const PaymentOption& o) {
        return names(s, o) == std::multiset<std::string>{"Blood Crypt"};
    });
    REQUIRE(it != opts.end());
    pay_mana(s, db_with_tokens(), kRakdos, cost, *it);

    CHECK(s.objects[find_on_battlefield(s, "Blood Crypt").card].has_flag(ObjectFlag::Tapped));
    CHECK_FALSE(s.objects[find_on_battlefield(s, "Swamp").card].has_flag(ObjectFlag::Tapped));
    for (const auto amount : s.players[kRakdos].pool.amount) CHECK(amount == 0);
}

TEST_CASE("Pagamento: enumerazione deterministica", "[mana]") {
    GameState s = new_game(1);
    put(s, kRakdos, {"Swamp", "Swamp", "Blood Crypt", "Raucous Theater"}, Zone::Battlefield);
    const auto a = options(s, kRakdos, "{1}{B}");
    const auto b = options(s, kRakdos, "{1}{B}");
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) CHECK(a[i].lands == b[i].lands);
}
