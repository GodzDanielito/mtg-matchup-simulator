// Test di Nethergoyf, Moonshadow, Dark Matter Manipulator e Troll of Khazad-dûm.
// Testo Oracle da Scryfall (bulk oracle_cards aggiornato il 9/11/2026, 10:02 UTC),
// riportato prima dei test di ogni carta.

#include "test_helpers.hpp"

#include <algorithm>
#include <iostream>
#include <map>
#include <set>

using namespace mtg;
using namespace test;

namespace {

constexpr PlayerId kUw = 0;
constexpr PlayerId kRakdos = 1;

std::string name_of(const GameState& s, CardId id) { return db_with_tokens().get(s.objects[id].def).name; }

// Mette le carte nominate in cima al grimorio (la prima dell'elenco finisce più in alto).
void stack_top(GameState& s, PlayerId p, std::initializer_list<const char*> names) {
    std::vector<ObjRef> refs;
    for (const char* name : names) {
        const ObjRef card = find_cards(s, p, Zone::Library, name)[0];
        refs.push_back(move_object(s, db_with_tokens(), card, Zone::Exile, Cause::Test).moves[0].new_ref);
    }
    move_objects(s, db_with_tokens(), refs, Zone::Library, Cause::Test, LibraryPosition::Top);
    require_invariants(s);
}

void to_graveyard(GameState& s, PlayerId p, std::initializer_list<const char*> names) {
    for (const char* name : names) {
        move_object(s, db_with_tokens(), find_cards(s, p, Zone::Library, name)[0], Zone::Graveyard, Cause::Test);
    }
    require_invariants(s);
}

// Lancia la carta dalla mano; se servono bersagli o pagamenti con più opzioni, prende la prima.
void cast(Staged& st, const char* name, std::uint8_t value = 0) {
    act(st, find_action(st.s, ActionType::CastSpell, name, value));
}

template <typename Pred>
void pass_until(Staged& st, Pred done) {
    for (int guard = 0; guard < 500 && !done(st.s); ++guard) {
        REQUIRE(st.s.pending.kind == DecisionKind::Priority);
        act(st, pass());
    }
    REQUIRE(done(st.s));
}

ObjRef moonshadow_ref(const GameState& s) { return find_on_battlefield(s, "Moonshadow"); }
int minus_counters(const GameState& s) { return s.objects[moonshadow_ref(s).card].counters.minus_one; }

}  // namespace

// ---------------------------------------------------------------------------
// Nethergoyf — {B} — Creature — Lhurgoyf */1+*
//   Nethergoyf's power is equal to the number of card types among cards in your
//   graveyard and its toughness is equal to that number plus 1.
//   Escape—{2}{B}, Exile any number of other cards from your graveyard with four
//   or more card types among them. (You may cast this card from your graveyard
//   for its escape cost.)
// ---------------------------------------------------------------------------

TEST_CASE("Nethergoyf: forza e costituzione dai tipi nel cimitero, al volo", "[rakdos][nethergoyf]") {
    GameState s = new_game(1);
    const ObjRef goyf = move_object(s, db_with_tokens(), find_cards(s, kRakdos, Zone::Library, "Nethergoyf")[0],
                                    Zone::Battlefield, Cause::Test).moves[0].new_ref;
    const CardDatabase& db = db_with_tokens();
    CHECK(power(s, db, goyf.card) == 0);
    CHECK(toughness(s, db, goyf.card) == 1);

    to_graveyard(s, kRakdos, {"Lightning Bolt", "Lightning Bolt"});  // istantaneo
    CHECK(power(s, db, goyf.card) == 1);
    to_graveyard(s, kRakdos, {"Fear of Missing Out"});  // incantesimo e creatura
    CHECK(power(s, db, goyf.card) == 3);
    CHECK(toughness(s, db, goyf.card) == 4);
    to_graveyard(s, kRakdos, {"Swamp"});
    CHECK(power(s, db, goyf.card) == 4);

    // Il cimitero dell'avversario non conta.
    to_graveyard(s, kUw, {"Thraben Charm", "Plains"});
    CHECK(power(s, db, goyf.card) == 4);
}

TEST_CASE("Nethergoyf: muore se il cimitero si svuota mentre ha danno", "[rakdos][nethergoyf]") {
    Staged st = staged(kUw, {"Plains"}, {}, {}, {"Nethergoyf"});
    to_graveyard(st.s, kRakdos, {"Swamp"});  // 1/2
    const ObjRef goyf = find_on_battlefield(st.s, "Nethergoyf");
    st.s.objects[goyf.card].damage = 1;
    act(st, pass());  // nessuna azione di stato: 1 danno su costituzione 2
    CHECK(st.s.objects[goyf.card].zone == Zone::Battlefield);

    // Il cimitero si svuota: costituzione 1, danno 1, muore.
    move_object(st.s, db_with_tokens(), find_cards(st.s, kRakdos, Zone::Graveyard, "Swamp")[0], Zone::Exile, Cause::Test);
    act(st, pass());
    CHECK(st.s.objects[goyf.card].zone == Zone::Graveyard);
}

TEST_CASE("Nethergoyf: si lancia dalla mano per {B}", "[rakdos][nethergoyf]") {
    Staged st = staged(kRakdos, {"Plains"}, {"Nethergoyf"}, {}, {"Swamp"});
    cast(st, "Nethergoyf");
    CHECK(st.s.objects[find_on_battlefield(st.s, "Nethergoyf").card].zone == Zone::Battlefield);
}

TEST_CASE("Nethergoyf: fuga solo con quattro tipi tra le altre carte e {2}{B}", "[rakdos][nethergoyf]") {
    SECTION("Tre tipi non bastano (Nethergoyf stesso non conta)") {
        Staged st = staged(kRakdos, {"Plains"}, {"Raucous Theater"}, {}, {"Swamp", "Swamp", "Blood Crypt"});
        to_graveyard(st.s, kRakdos, {"Nethergoyf", "Lightning Bolt", "Thoughtseize", "Mishra's Bauble"});
        bool escape = false;
        for (const Action& a : legal(st.s)) escape = escape || (a.type == ActionType::CastSpell && a.value == 1);
        CHECK_FALSE(escape);
    }
    SECTION("Quattro tipi e tre mana: si puo'") {
        Staged st = staged(kRakdos, {"Plains"}, {"Raucous Theater"}, {}, {"Swamp", "Swamp", "Blood Crypt"});
        to_graveyard(st.s, kRakdos, {"Nethergoyf", "Lightning Bolt", "Thoughtseize", "Mishra's Bauble", "Marsh Flats"});
        CHECK_NOTHROW(find_action(st.s, ActionType::CastSpell, "Nethergoyf", 1));
    }
    SECTION("Senza mana no") {
        Staged st = staged(kRakdos, {"Plains"}, {"Raucous Theater"}, {}, {"Swamp"});
        to_graveyard(st.s, kRakdos, {"Nethergoyf", "Lightning Bolt", "Thoughtseize", "Mishra's Bauble", "Marsh Flats"});
        bool escape = false;
        for (const Action& a : legal(st.s)) escape = escape || (a.type == ActionType::CastSpell && a.value == 1);
        CHECK_FALSE(escape);
    }
}

TEST_CASE("Nethergoyf: fuga, esilio una carta alla volta, poi in campo", "[rakdos][nethergoyf]") {
    Staged st = staged(kRakdos, {"Plains"}, {"Raucous Theater"}, {}, {"Swamp", "Swamp", "Blood Crypt"});
    to_graveyard(st.s, kRakdos,
                 {"Nethergoyf", "Lightning Bolt", "Thoughtseize", "Mishra's Bauble", "Marsh Flats", "Stitcher's Supplier"});
    cast(st, "Nethergoyf", 1);
    REQUIRE(is_decision(st.s, DecisionKind::ExileFromGraveyard, kRakdos));
    CHECK(st.s.objects[find_cards(st.s, kRakdos, Zone::Graveyard, "Lightning Bolt")[0].card].zone == Zone::Graveyard);

    const auto events = st.s.event_serial;
    while (st.s.pending.kind == DecisionKind::ExileFromGraveyard) act(st, legal(st.s).front());

    // Quattro carte esiliate (un tipo ciascuna) in un solo evento; restano una carta e un tipo.
    CHECK(st.s.exile.size() == 4);
    CHECK(st.s.event_serial >= events + 1);
    const ObjRef goyf = find_on_battlefield(st.s, "Nethergoyf");
    CHECK(st.s.players[kRakdos].graveyard.size() == 1);
    CHECK(power(st.s, db_with_tokens(), goyf.card) == st.s.players[kRakdos].gy.distinct_types());
}

// Forza bruta: gli insiemi minimali di altre carte con almeno 4 tipi (nessuna carta
// superflua) devono essere esattamente quelli raggiunti esplorando le decisioni,
// ognuno con un solo percorso. Le carte identiche sono indistinguibili (per nome).
TEST_CASE("Nethergoyf: scelte di esilio esaustive contro la forza bruta", "[rakdos][nethergoyf][exhaustive]") {
    const std::vector<std::vector<const char*>> graveyards = {
        {"Lightning Bolt", "Lightning Bolt", "Thoughtseize", "Marsh Flats", "Mishra's Bauble", "Stitcher's Supplier"},
        {"Fear of Missing Out", "Stitcher's Supplier", "Polluted Delta", "Blood Crypt", "Lightning Bolt", "Thoughtseize"},
        {"Fear of Missing Out", "Detective's Phoenix", "Mishra's Bauble", "Lightning Bolt", "Thoughtseize",
         "Marsh Flats", "Street Wraith"},
    };
    int total = 0;
    for (const auto& gy : graveyards) {
        Staged st = staged(kRakdos, {"Plains"}, {"Raucous Theater"}, {}, {"Swamp", "Swamp", "Blood Crypt"});
        for (const char* name : gy) to_graveyard(st.s, kRakdos, {name});
        to_graveyard(st.s, kRakdos, {"Nethergoyf"});

        // Forza bruta sulle altre carte del cimitero.
        std::vector<CardId> others(st.s.players[kRakdos].graveyard.begin(), st.s.players[kRakdos].graveyard.end());
        others.erase(std::remove_if(others.begin(), others.end(),
                                    [&](CardId id) { return name_of(st.s, id) == "Nethergoyf"; }),
                     others.end());
        auto types = [&](const std::vector<CardId>& set) {
            TypeMask m = 0;
            for (const CardId id : set) m = static_cast<TypeMask>(m | db_with_tokens().get(st.s.objects[id].def).types);
            return std::popcount(m);
        };
        std::set<std::multiset<std::string>> expected;
        for (std::size_t mask = 1; mask < (std::size_t{1} << others.size()); ++mask) {
            std::vector<CardId> set;
            for (std::size_t i = 0; i < others.size(); ++i) {
                if ((mask >> i) & 1u) set.push_back(others[i]);
            }
            if (types(set) < 4) continue;
            bool minimal = true;
            for (std::size_t i = 0; i < set.size() && minimal; ++i) {
                std::vector<CardId> without = set;
                without.erase(without.begin() + static_cast<std::ptrdiff_t>(i));
                if (types(without) >= 4) minimal = false;
            }
            if (!minimal) continue;
            std::multiset<std::string> names;
            for (const CardId id : set) names.insert(name_of(st.s, id));
            expected.insert(names);
        }

        // Esplorazione di tutti i percorsi di decisione.
        cast(st, "Nethergoyf", 1);
        std::set<std::multiset<std::string>> reached;
        int paths = 0;
        auto explore = [&](auto&& self, const GameState& s) -> void {
            if (s.pending.kind != DecisionKind::ExileFromGraveyard) {
                ++paths;
                std::multiset<std::string> names;
                for (const CardId id : s.exile) names.insert(name_of(s, id));
                reached.insert(names);
                return;
            }
            for (const Action& a : legal_actions(s, db_with_tokens())) {
                GameState next = s;
                apply(next, db_with_tokens(), a);
                self(self, next);
            }
        };
        explore(explore, st.s);
        CHECK(reached == expected);
        CHECK(paths == static_cast<int>(expected.size()));
        total += paths;
    }
    std::cout << "\n  fuga di Nethergoyf: " << total << " insiemi di esilio confrontati con la forza bruta\n";
    CHECK(total > 10);
}

// ---------------------------------------------------------------------------
// Moonshadow — {B} — Creature — Elemental 7/7
//   Menace
//   This creature enters with six -1/-1 counters on it.
//   Whenever one or more permanent cards are put into your graveyard from
//   anywhere while this creature has a -1/-1 counter on it, remove a -1/-1
//   counter from this creature.
// I sei casi della sezione 12 di docs/stato_di_gioco.md, con il trigger vero.
// ---------------------------------------------------------------------------

namespace {

// Rakdos lancia Moonshadow (turno 1, fase principale) e la lascia risolvere.
Staged with_moonshadow(std::initializer_list<const char*> hand, std::initializer_list<const char*> field) {
    std::vector<const char*> h{"Moonshadow"};
    h.insert(h.end(), hand.begin(), hand.end());
    Staged st = staged(kRakdos, {"Plains"}, {}, {}, field);
    for (const char* name : h) {
        move_object(st.s, db_with_tokens(), find_cards(st.s, kRakdos, Zone::Library, name)[0], Zone::Hand, Cause::Test);
    }
    require_invariants(st.s);
    pass_until(st, [](const GameState& s) { return s.turn.step == Step::Main1 && s.pending.player == kRakdos; });
    cast(st, "Moonshadow");
    pass_until(st, [](const GameState& s) {
        return std::any_of(s.battlefield.begin(), s.battlefield.end(),
                           [&](CardId id) { return name_of(s, id) == "Moonshadow"; });
    });
    REQUIRE(minus_counters(st.s) == 6);  // entra con sei segnalini -1/-1: è una 1/1
    CHECK(power(st.s, db_with_tokens(), moonshadow_ref(st.s).card) == 1);
    return st;
}

}  // namespace

TEST_CASE("Moonshadow 1: tre terre macinate insieme, un segnalino", "[rakdos][moonshadow]") {
    Staged st = with_moonshadow({"Stitcher's Supplier"}, {"Marsh Flats", "Swamp", "Swamp"});
    stack_top(st.s, kRakdos, {"Bloodstained Mire", "Polluted Delta", "Blood Crypt"});
    cast(st, "Stitcher's Supplier");  // entra e macina tre terre
    pass_until(st, [](const GameState& s) { return s.stack_items.empty() && s.pending_triggers.empty(); });
    CHECK(st.s.players[kRakdos].graveyard.size() == 3);
    CHECK(minus_counters(st.s) == 5);
}

TEST_CASE("Moonshadow 2: una terra e due istantanei, un segnalino", "[rakdos][moonshadow]") {
    Staged st = with_moonshadow({"Stitcher's Supplier"}, {"Marsh Flats", "Swamp", "Swamp"});
    stack_top(st.s, kRakdos, {"Lightning Bolt", "Polluted Delta", "Lightning Bolt"});
    cast(st, "Stitcher's Supplier");
    pass_until(st, [](const GameState& s) { return s.stack_items.empty() && s.pending_triggers.empty(); });
    CHECK(minus_counters(st.s) == 5);
}

TEST_CASE("Moonshadow 3: tre istantanei non contano", "[rakdos][moonshadow]") {
    Staged st = with_moonshadow({"Stitcher's Supplier"}, {"Marsh Flats", "Swamp", "Swamp"});
    stack_top(st.s, kRakdos, {"Lightning Bolt", "Lightning Bolt", "Thoughtseize"});
    cast(st, "Stitcher's Supplier");
    pass_until(st, [](const GameState& s) { return s.stack_items.empty() && s.pending_triggers.empty(); });
    CHECK(st.s.players[kRakdos].graveyard.size() == 3);
    CHECK(minus_counters(st.s) == 6);
}

TEST_CASE("Moonshadow 4: due eventi separati, due segnalini", "[rakdos][moonshadow]") {
    Staged st = with_moonshadow({"Street Wraith"}, {"Marsh Flats", "Swamp"});
    act(st, find_action(st.s, ActionType::ActivateAbility, "Marsh Flats"));  // sacrificio: una carta permanente
    pass_until(st, [](const GameState& s) { return s.pending.kind == DecisionKind::SearchLibrary; });
    act(st, option(kDeclineSearch));
    pass_until(st, [](const GameState& s) { return s.stack_items.empty() && s.pending_triggers.empty(); });
    CHECK(minus_counters(st.s) == 5);
    act(st, find_action(st.s, ActionType::ActivateAbility, "Street Wraith"));  // scarto: un'altra carta permanente
    pass_until(st, [](const GameState& s) { return s.stack_items.empty() && s.pending_triggers.empty(); });
    CHECK(minus_counters(st.s) == 4);
}

TEST_CASE("Moonshadow 5: un token che muore non e' una carta", "[rakdos][moonshadow]") {
    Staged st = with_moonshadow({"Lightning Bolt"}, {"Marsh Flats", "Swamp"});
    put(st.s, kRakdos, {"Blood Crypt"}, Zone::Battlefield);  // dopo Moonshadow: un solo modo di pagare
    const ObjRef cat = create_token(st.s, db_with_tokens(), db_with_tokens().id_of("Cat Token"), kRakdos);
    require_invariants(st.s);
    cast(st, "Lightning Bolt");
    act(st, Action{ActionType::TargetObject, cat.card, 0});
    pass_until(st, [](const GameState& s) { return s.stack_items.empty() && s.pending_triggers.empty(); });
    CHECK(st.s.objects[cat.card].zone == Zone::None);  // il token è morto e ha smesso di esistere
    CHECK(minus_counters(st.s) == 6);                   // Lightning Bolt è un istantaneo, il token non è una carta
}

TEST_CASE("Moonshadow 6: senza segnalini -1/-1 il trigger non scatta", "[rakdos][moonshadow]") {
    Staged st = with_moonshadow({"Stitcher's Supplier"}, {"Marsh Flats", "Swamp", "Swamp"});
    st.s.objects[moonshadow_ref(st.s).card].counters.minus_one = 0;
    stack_top(st.s, kRakdos, {"Bloodstained Mire", "Polluted Delta", "Blood Crypt"});
    cast(st, "Stitcher's Supplier");
    // Il trigger di entrata del Supplier va in pila; dopo la macinata non se ne aggiunge nessuno.
    pass_until(st, [](const GameState& s) { return s.players[kRakdos].graveyard.size() == 3; });
    CHECK(st.s.stack_items.empty());
    CHECK(st.s.pending_triggers.empty());
    CHECK(minus_counters(st.s) == 0);
}

TEST_CASE("Moonshadow: le carte dell'avversario non contano", "[rakdos][moonshadow]") {
    Staged st = with_moonshadow({}, {"Marsh Flats", "Swamp"});
    to_graveyard(st.s, kUw, {"Plains"});
    pass_until(st, [](const GameState& s) { return s.turn.step == Step::BeginCombat; });
    CHECK(minus_counters(st.s) == 6);
}

TEST_CASE("Trigger simultanei diversi: il giocatore sceglie l'ordine", "[rakdos][moonshadow][triggers]") {
    // Il Supplier muore mentre Moonshadow ha segnalini: scattano insieme la macinata
    // del Supplier e il segnalino di Moonshadow. Il primo messo in pila si risolve per ultimo.
    Staged st = with_moonshadow({"Lightning Bolt"}, {"Marsh Flats", "Swamp", "Stitcher's Supplier"});
    put(st.s, kRakdos, {"Blood Crypt"}, Zone::Battlefield);
    cast(st, "Lightning Bolt");
    act(st, Action{ActionType::TargetObject, find_on_battlefield(st.s, "Stitcher's Supplier").card, 0});
    pass_until(st, [](const GameState& s) { return s.pending.kind == DecisionKind::OrderTriggers; });
    REQUIRE(is_decision(st.s, DecisionKind::OrderTriggers, kRakdos));
    CHECK(legal(st.s).size() == 2);
    act(st, legal(st.s).front());
    REQUIRE(st.s.stack_items.size() == 2);  // il secondo è andato in pila da solo
    pass_until(st, [](const GameState& s) { return s.stack_items.empty() && s.pending_triggers.empty(); });
    // Supplier al cimitero (un segnalino), poi la macinata di tre carte (forse un altro).
    CHECK(minus_counters(st.s) <= 5);
}

// ---------------------------------------------------------------------------
// Dark Matter Manipulator — {B} — Creature — Human Warlock 1/2
//   When this creature enters, mill three cards.
//   This creature gets +2/+0 for every seven cards in your graveyard.
// ---------------------------------------------------------------------------

TEST_CASE("Dark Matter Manipulator: +2/+0 ogni sette carte nel cimitero", "[rakdos][dmm]") {
    GameState s = new_game(1);
    const ObjRef dmm = move_object(s, db_with_tokens(), find_cards(s, kRakdos, Zone::Library, "Dark Matter Manipulator")[0],
                                   Zone::Battlefield, Cause::Test).moves[0].new_ref;
    const CardDatabase& db = db_with_tokens();
    auto mill = [&](std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) {
            move_object(s, db, s.ref(s.players[kRakdos].library.back()), Zone::Graveyard, Cause::Test);
        }
    };
    CHECK(power(s, db, dmm.card) == 1);
    mill(6);
    CHECK(power(s, db, dmm.card) == 1);
    mill(1);  // 7 carte
    CHECK(power(s, db, dmm.card) == 3);
    CHECK(toughness(s, db, dmm.card) == 2);  // solo forza
    mill(7);  // 14 carte
    CHECK(power(s, db, dmm.card) == 5);

    // Un token nel cimitero non è una carta; le carte dell'avversario non contano.
    const ObjRef cat = create_token(s, db, db.id_of("Cat Token"), kRakdos);
    move_object(s, db, cat, Zone::Graveyard, Cause::Test);
    for (int i = 0; i < 7; ++i) move_object(s, db, s.ref(s.players[kUw].library.back()), Zone::Graveyard, Cause::Test);
    CHECK(power(s, db, dmm.card) == 5);
}

TEST_CASE("Dark Matter Manipulator: entra e macina tre carte", "[rakdos][dmm]") {
    Staged st = staged(kRakdos, {"Plains"}, {"Dark Matter Manipulator"}, {}, {"Swamp"});
    cast(st, "Dark Matter Manipulator");
    CHECK(st.s.players[kRakdos].graveyard.size() == 3);
    CHECK(find_on_battlefield(st.s, "Dark Matter Manipulator").card != kNoCard);
}

// ---------------------------------------------------------------------------
// Troll of Khazad-dûm — {5}{B} — Creature — Troll 6/5
//   This creature can't be blocked except by three or more creatures.
//   Swampcycling {1} ({1}, Discard this card: Search your library for a Swamp
//   card, reveal it, put it into your hand, then shuffle.)
// ---------------------------------------------------------------------------

TEST_CASE("Troll of Khazad-dum: swampcycling, cerca una Swamp e mescola", "[rakdos][troll]") {
    Staged st = staged(kRakdos, {"Plains"}, {"Troll of Khazad-dûm"}, {}, {"Blackcleave Cliffs"});
    REQUIRE(is_decision(st.s, DecisionKind::Priority, kRakdos));  // velocità istantaneo: già nel mantenimento
    act(st, find_action(st.s, ActionType::ActivateAbility, "Troll of Khazad-dûm"));

    // Costi pagati: {1} (Blackcleave Cliffs tappata) e lo scarto del Troll.
    CHECK(st.s.objects[find_on_battlefield(st.s, "Blackcleave Cliffs").card].has_flag(ObjectFlag::Tapped));
    CHECK(st.s.players[kRakdos].graveyard.size() == 1);
    REQUIRE(is_decision(st.s, DecisionKind::SearchLibrary, kRakdos));
    // Carte Swamp: Swamp, Blood Crypt, Raucous Theater, più "non trovare".
    CHECK(legal(st.s).size() == 4);

    const Action take = find_action(st.s, ActionType::ChooseCard, "Blood Crypt");
    std::vector<CardId> before(st.s.players[kRakdos].library.begin(), st.s.players[kRakdos].library.end());
    before.erase(std::find(before.begin(), before.end(), take.card));
    act(st, take);
    CHECK(st.s.objects[take.card].zone == Zone::Hand);
    CHECK(st.s.objects[take.card].known_to == 0b11);  // rivelata
    CHECK(std::vector<CardId>(st.s.players[kRakdos].library.begin(), st.s.players[kRakdos].library.end()) != before);
}

TEST_CASE("Troll of Khazad-dum: niente ciclo senza mana, si lancia per {5}{B}", "[rakdos][troll]") {
    Staged st = staged(kRakdos, {"Plains"}, {"Troll of Khazad-dûm"}, {},
                       {"Swamp", "Swamp", "Blood Crypt", "Blood Crypt", "Blood Crypt", "Blackcleave Cliffs"});
    pass_until(st, [](const GameState& s) { return s.turn.step == Step::Main1; });
    cast(st, "Troll of Khazad-dûm");
    CHECK(st.s.objects[find_on_battlefield(st.s, "Troll of Khazad-dûm").card].zone == Zone::Battlefield);
    // Nessuna terra stappata: il ciclo non sarebbe più possibile (e il Troll è in campo).
    for (const CardId id : st.s.battlefield) {
        if (db_with_tokens().get(st.s.objects[id].def).has_type(CardType::Land)) {
            CHECK(st.s.objects[id].has_flag(ObjectFlag::Tapped));
        }
    }
}
