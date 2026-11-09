// Test di Fear of Missing Out, Detective's Phoenix e Flare of Malice.
// Testo Oracle da Scryfall (bulk oracle_cards del 9/11/2026, 10:02 UTC); regole
// del conferimento e della raccolta delle prove dalle Comprehensive Rules del
// 6/11/2026 (702.103, 701.59).

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
constexpr std::uint8_t kNormal = static_cast<std::uint8_t>(CastMethod::Normal);
constexpr std::uint8_t kBestow = static_cast<std::uint8_t>(CastMethod::Bestow);
constexpr std::uint8_t kSacrificeAlt = static_cast<std::uint8_t>(CastMethod::SacrificeAlternative);

std::string name_of(const GameState& s, CardId id) { return db_with_tokens().get(s.objects[id].def).name; }

void to_graveyard(GameState& s, PlayerId p, std::initializer_list<const char*> names) {
    for (const char* name : names) {
        move_object(s, db_with_tokens(), find_cards(s, p, Zone::Library, name)[0], Zone::Graveyard, Cause::Test);
    }
    require_invariants(s);
}

template <typename Pred>
void pass_until(Staged& st, Pred done) {
    for (int guard = 0; guard < 500 && !done(st.s); ++guard) {
        REQUIRE(st.s.pending.kind == DecisionKind::Priority);
        act(st, pass());
    }
    REQUIRE(done(st.s));
}

// Come staged, ma con le carte già nel cimitero di Rakdos PRIMA di start_game: il motore
// avanza fino alla prima decisione, e quali decisioni esistono dipende dal cimitero
// (con il cimitero riempito dopo, la fase principale senza azioni verrebbe già superata).
Staged staged_with_graveyard(const std::vector<const char*>& graveyard1, std::initializer_list<const char*> hand1,
                             std::initializer_list<const char*> field1, std::initializer_list<const char*> field0 = {}) {
    Staged st{new_game(1), {}};
    put(st.s, kUw, {"Plains"}, Zone::Hand);
    put(st.s, kRakdos, hand1, Zone::Hand);
    put(st.s, kUw, field0, Zone::Battlefield);
    put(st.s, kRakdos, field1, Zone::Battlefield);
    for (const char* name : graveyard1) put(st.s, kRakdos, {name}, Zone::Graveyard);
    start_game(st.s, db_with_tokens(), StartOptions{kRakdos, false}, &st.log);
    require_invariants(st.s);
    return st;
}

bool on_battlefield(const GameState& s, std::string_view name) {
    return std::any_of(s.battlefield.begin(), s.battlefield.end(), [&](CardId id) { return name_of(s, id) == name; });
}

bool can_cast(const GameState& s, std::string_view name, std::uint8_t method) {
    for (const Action& a : legal(s)) {
        if (a.type == ActionType::CastSpell && a.value == method && name_of(s, a.card) == name) return true;
    }
    return false;
}

Action target(const GameState& s, std::string_view name, std::size_t skip = 0) {
    for (const Action& a : legal(s)) {
        if (a.type == ActionType::TargetObject && name_of(s, a.card) == name) {
            if (skip == 0) return a;
            --skip;
        }
    }
    FAIL("bersaglio non offerto: " << name);
    return {};
}

int count_decisions(const GameLog& log, PlayerId p, std::uint16_t turn, Step step) {
    return static_cast<int>(std::count_if(log.entries.begin(), log.entries.end(), [&](const LogEntry& e) {
        return e.kind == LogEntry::Kind::Action && e.player == p && e.turn == turn && e.step == step &&
               e.decision == DecisionKind::Priority;
    }));
}

// Quattro tipi nel cimitero di Rakdos: istantaneo, stregoneria, terra, artefatto.
void give_delirium(GameState& s) { to_graveyard(s, kRakdos, {"Lightning Bolt", "Thoughtseize", "Swamp", "Mishra's Bauble"}); }

void attack_with_all(Staged& st) {
    pass_until(st, [](const GameState& s) { return s.pending.kind == DecisionKind::DeclareAttacker; });
    while (st.s.pending.kind == DecisionKind::DeclareAttacker) {
        act(st, Action{ActionType::TargetPlayer, kNoCard, static_cast<PlayerId>(1 - st.s.turn.active_player)});
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Fear of Missing Out — {1}{R} — Enchantment Creature — Nightmare 2/3
//   When this creature enters, discard a card, then draw a card.
//   Delirium — Whenever this creature attacks for the first time each turn, if
//   there are four or more card types among cards in your graveyard, untap
//   target creature. After this phase, there is an additional combat phase.
// ---------------------------------------------------------------------------

TEST_CASE("Fear of Missing Out: entrando scarta una carta, poi pesca", "[fomo]") {
    Staged st = staged(kRakdos, {"Plains"}, {"Fear of Missing Out", "Lightning Bolt", "Thoughtseize"}, {},
                       {"Blood Crypt", "Swamp"});
    pass_until(st, [](const GameState& s) { return s.turn.step == Step::Main1; });
    act(st, find_action(st.s, ActionType::CastSpell, "Fear of Missing Out"));
    pass_until(st, [](const GameState& s) { return s.pending.kind == DecisionKind::DiscardCard; });
    REQUIRE(is_decision(st.s, DecisionKind::DiscardCard, kRakdos));
    CHECK(legal(st.s).size() == 2);
    act(st, find_action(st.s, ActionType::ChooseCard, "Lightning Bolt"));
    CHECK(st.s.players[kRakdos].graveyard.size() == 1);
    CHECK(st.s.players[kRakdos].hand.size() == 2);  // Thoughtseize e la carta pescata
}

TEST_CASE("Fear of Missing Out: con la mano vuota pesca e basta", "[fomo]") {
    Staged st = staged(kRakdos, {"Plains"}, {"Fear of Missing Out"}, {}, {"Blood Crypt", "Swamp"});
    pass_until(st, [](const GameState& s) { return s.turn.step == Step::Main1; });
    act(st, find_action(st.s, ActionType::CastSpell, "Fear of Missing Out"));
    CHECK(on_battlefield(st.s, "Fear of Missing Out"));
    CHECK(st.s.players[kRakdos].graveyard.empty());
    CHECK(st.s.players[kRakdos].hand.size() == 1);
}

TEST_CASE("Fear of Missing Out: con delirium stappa e aggiunge un combattimento", "[fomo]") {
    Staged st = staged(kRakdos, {"Plains"}, {}, {}, {"Marsh Flats", "Fear of Missing Out"});
    give_delirium(st.s);
    attack_with_all(st);
    pass_until(st, [](const GameState& s) { return s.pending.kind == DecisionKind::ChooseTarget; });
    act(st, target(st.s, "Fear of Missing Out"));  // stappa se stessa
    pass_until(st, [](const GameState& s) { return s.stack_items.empty() && s.turn.step == Step::DeclareAttackers; });
    CHECK_FALSE(st.s.objects[find_on_battlefield(st.s, "Fear of Missing Out").card].has_flag(ObjectFlag::Tapped));
    CHECK(st.s.turn.extra_combats == 1);

    // Secondo combattimento: la stessa creatura attacca di nuovo e NON riscatta.
    attack_with_all(st);
    pass_until(st, [](const GameState& s) { return s.turn.step == Step::Main2; });
    CHECK(st.s.players[kUw].life == 16);
    CHECK(st.s.turn.extra_combats == 0);
    CHECK(count_decisions(st.log, kRakdos, 1, Step::BeginCombat) == 2);  // due fasi di combattimento
}

TEST_CASE("Fear of Missing Out: senza delirium non scatta", "[fomo]") {
    Staged st = staged(kRakdos, {"Plains"}, {}, {}, {"Marsh Flats", "Fear of Missing Out"});
    to_graveyard(st.s, kRakdos, {"Lightning Bolt", "Thoughtseize", "Swamp"});  // tre tipi
    attack_with_all(st);
    pass_until(st, [](const GameState& s) { return s.turn.step == Step::Main2; });
    CHECK(st.s.players[kUw].life == 18);
    CHECK(count_decisions(st.log, kRakdos, 1, Step::BeginCombat) == 1);
}

TEST_CASE("Fear of Missing Out: due FOMO, due combattimenti aggiuntivi", "[fomo]") {
    Staged st = staged(kRakdos, {"Plains"}, {}, {}, {"Marsh Flats", "Fear of Missing Out", "Fear of Missing Out"});
    give_delirium(st.s);
    attack_with_all(st);
    // Due trigger: ognuno sceglie un bersaglio. Dopo il primo le due FOMO non sono più
    // equivalenti (una è già bersaglio), quindi si può stappare l'altra.
    pass_until(st, [](const GameState& s) { return s.pending.kind == DecisionKind::ChooseTarget; });
    act(st, target(st.s, "Fear of Missing Out"));
    pass_until(st, [](const GameState& s) { return s.pending.kind == DecisionKind::ChooseTarget; });
    REQUIRE(legal(st.s).size() == 2);
    CardId first_target = kNoCard;
    for (const StackItem& item : st.s.stack_items) {
        if (item.target.tag == TargetKindTag::Object) first_target = item.target.object.card;
    }
    REQUIRE(first_target != kNoCard);
    for (const Action& a : legal(st.s)) {
        if (a.card != first_target) {
            act(st, a);  // l'altra FOMO
            break;
        }
    }
    pass_until(st, [](const GameState& s) { return s.stack_items.empty() && s.turn.step == Step::DeclareAttackers; });
    CHECK(st.s.turn.extra_combats == 2);

    attack_with_all(st);  // entrambe stappate: attaccano ancora, senza riscattare
    pass_until(st, [](const GameState& s) { return s.turn.step == Step::Main2; });
    CHECK(st.s.players[kUw].life == 12);  // 4 + 4; nel terzo combattimento sono tappate
    CHECK(count_decisions(st.log, kRakdos, 1, Step::BeginCombat) == 3);
}

TEST_CASE("Fear of Missing Out: bersaglio illegale, nessun combattimento aggiuntivo", "[fomo]") {
    Staged st = staged(kRakdos, {"Plains"}, {"Lightning Bolt"}, {"Guide of Souls"},
                       {"Marsh Flats", "Blood Crypt", "Fear of Missing Out"});
    give_delirium(st.s);
    attack_with_all(st);
    pass_until(st, [](const GameState& s) { return s.pending.kind == DecisionKind::ChooseTarget; });
    act(st, target(st.s, "Guide of Souls"));
    // In risposta, Lightning Bolt uccide il bersaglio del trigger.
    REQUIRE(is_decision(st.s, DecisionKind::Priority, kRakdos));
    act(st, find_action(st.s, ActionType::CastSpell, "Lightning Bolt"));
    act(st, target(st.s, "Guide of Souls"));
    pass_until(st, [](const GameState& s) { return s.stack_items.empty() && s.turn.step == Step::DeclareAttackers; });
    CHECK(st.s.turn.extra_combats == 0);
    CHECK(st.s.objects[find_on_battlefield(st.s, "Fear of Missing Out").card].has_flag(ObjectFlag::Tapped));
}

TEST_CASE("Fear of Missing Out: senza delirium alla risoluzione non fa nulla", "[fomo]") {
    // "if there are four or more card types" si ricontrolla alla risoluzione (603.4).
    Staged st = staged(kRakdos, {"Plains"}, {}, {}, {"Marsh Flats", "Fear of Missing Out"});
    give_delirium(st.s);
    attack_with_all(st);
    pass_until(st, [](const GameState& s) { return s.pending.kind == DecisionKind::ChooseTarget; });
    act(st, target(st.s, "Fear of Missing Out"));
    std::vector<ObjRef> gy;
    for (const CardId id : st.s.players[kRakdos].graveyard) gy.push_back(st.s.ref(id));
    move_objects(st.s, db_with_tokens(), gy, Zone::Exile, Cause::Test);
    pass_until(st, [](const GameState& s) { return s.stack_items.empty() && s.turn.step == Step::DeclareAttackers; });
    CHECK(st.s.turn.extra_combats == 0);
}

// ---------------------------------------------------------------------------
// Detective's Phoenix — {2}{R} — Enchantment Creature — Phoenix 2/2
//   Bestow—{R}, Collect evidence 6. (To pay this bestow cost, pay {R} and exile
//   cards with total mana value 6 or greater from your graveyard.)
//   Flying, haste
//   Enchanted creature gets +2/+2 and has flying and haste.
//   You may cast this card from your graveyard using its bestow ability.
// ---------------------------------------------------------------------------

TEST_CASE("Detective's Phoenix: lancio normale come creatura", "[phoenix]") {
    Staged st = staged(kRakdos, {"Plains"}, {"Detective's Phoenix"}, {}, {"Blood Crypt", "Swamp", "Swamp"});
    pass_until(st, [](const GameState& s) { return s.turn.step == Step::Main1; });
    act(st, find_action(st.s, ActionType::CastSpell, "Detective's Phoenix", kNormal));
    const ObjRef phoenix = find_on_battlefield(st.s, "Detective's Phoenix");
    CHECK_FALSE(st.s.objects[phoenix.card].has_flag(ObjectFlag::Bestowed));
    CHECK(has_keyword(st.s, db_with_tokens(), phoenix.card, Keyword::Haste));
}

TEST_CASE("Detective's Phoenix: conferimento dalla mano", "[phoenix]") {
    Staged st = staged_with_graveyard({"Troll of Khazad-dûm"},  // valore di mana 6
                                      {"Detective's Phoenix"}, {"Blood Crypt", "Stitcher's Supplier"});
    pass_until(st, [](const GameState& s) { return s.turn.step == Step::Main1; });
    act(st, find_action(st.s, ActionType::CastSpell, "Detective's Phoenix", kBestow));
    REQUIRE(is_decision(st.s, DecisionKind::ChooseTarget, kRakdos));
    act(st, target(st.s, "Stitcher's Supplier"));

    const ObjRef phoenix = find_on_battlefield(st.s, "Detective's Phoenix");
    const ObjRef supplier = find_on_battlefield(st.s, "Stitcher's Supplier");
    const CardObject& p = st.s.objects[phoenix.card];
    CHECK(p.has_flag(ObjectFlag::Bestowed));
    CHECK(p.attached_to == supplier);
    CHECK(st.s.exile.size() == 1);  // il Troll come prova
    CHECK(power(st.s, db_with_tokens(), supplier.card) == 3);
    CHECK(toughness(st.s, db_with_tokens(), supplier.card) == 3);
    CHECK(has_keyword(st.s, db_with_tokens(), supplier.card, Keyword::Flying));
    CHECK(has_keyword(st.s, db_with_tokens(), supplier.card, Keyword::Haste));
}

TEST_CASE("Detective's Phoenix: dal cimitero solo con il conferimento, senza contare se stessa", "[phoenix]") {
    Staged st = staged(kRakdos, {"Plains"}, {"Raucous Theater"}, {}, {"Blood Crypt", "Stitcher's Supplier"});
    to_graveyard(st.s, kRakdos, {"Detective's Phoenix", "Street Wraith"});  // 3 + 5: la Phoenix non conta
    pass_until(st, [](const GameState& s) { return s.turn.step == Step::Main1; });
    CHECK_FALSE(can_cast(st.s, "Detective's Phoenix", kBestow));
    CHECK_FALSE(can_cast(st.s, "Detective's Phoenix", kNormal));

    to_graveyard(st.s, kRakdos, {"Lightning Bolt"});  // 5 + 1 = 6
    CHECK(can_cast(st.s, "Detective's Phoenix", kBestow));
    CHECK_FALSE(can_cast(st.s, "Detective's Phoenix", kNormal));
    act(st, find_action(st.s, ActionType::CastSpell, "Detective's Phoenix", kBestow));
    act(st, target(st.s, "Stitcher's Supplier"));
    CHECK(st.s.objects[find_on_battlefield(st.s, "Detective's Phoenix").card].has_flag(ObjectFlag::Bestowed));
    CHECK(st.s.exile.size() == 2);
}

TEST_CASE("Detective's Phoenix: bersaglio illegale, si risolve come creatura", "[phoenix]") {
    Staged st = staged_with_graveyard({"Troll of Khazad-dûm"}, {"Detective's Phoenix", "Lightning Bolt"},
                                      {"Marsh Flats", "Blood Crypt", "Blood Crypt", "Stitcher's Supplier"});
    pass_until(st, [](const GameState& s) { return s.turn.step == Step::Main1; });
    act(st, find_action(st.s, ActionType::CastSpell, "Detective's Phoenix", kBestow));
    act(st, target(st.s, "Stitcher's Supplier"));
    REQUIRE(is_decision(st.s, DecisionKind::Priority, kRakdos));
    act(st, find_action(st.s, ActionType::CastSpell, "Lightning Bolt"));  // in risposta, sul bersaglio
    act(st, target(st.s, "Stitcher's Supplier"));
    pass_until(st, [](const GameState& s) { return s.stack_items.empty() && on_battlefield(s, "Detective's Phoenix"); });
    const CardObject& p = st.s.objects[find_on_battlefield(st.s, "Detective's Phoenix").card];
    CHECK_FALSE(p.has_flag(ObjectFlag::Bestowed));
    CHECK(p.attached_to.is_none());
}

TEST_CASE("Detective's Phoenix: la creatura incantata muore, la Phoenix resta creatura", "[phoenix]") {
    Staged st = staged_with_graveyard({"Troll of Khazad-dûm"}, {"Detective's Phoenix", "Lightning Bolt"},
                                      {"Marsh Flats", "Blood Crypt", "Blood Crypt", "Stitcher's Supplier"});
    pass_until(st, [](const GameState& s) { return s.turn.step == Step::Main1; });
    act(st, find_action(st.s, ActionType::CastSpell, "Detective's Phoenix", kBestow));
    act(st, target(st.s, "Stitcher's Supplier"));
    pass_until(st, [](const GameState& s) { return s.stack_items.empty() && on_battlefield(s, "Detective's Phoenix"); });
    const ObjRef phoenix = find_on_battlefield(st.s, "Detective's Phoenix");
    REQUIRE(st.s.objects[phoenix.card].has_flag(ObjectFlag::Bestowed));

    act(st, find_action(st.s, ActionType::CastSpell, "Lightning Bolt"));
    act(st, target(st.s, "Stitcher's Supplier"));  // 3/3 con la Phoenix: 3 danni la uccidono
    pass_until(st, [](const GameState& s) { return s.stack_items.empty(); });
    CHECK(st.s.is_valid(phoenix));  // non ha cambiato zona
    CHECK_FALSE(st.s.objects[phoenix.card].has_flag(ObjectFlag::Bestowed));
    CHECK(st.s.objects[phoenix.card].attached_to.is_none());
    CHECK(power(st.s, db_with_tokens(), phoenix.card) == 2);
}

// Una Phoenix in mano e una nel cimitero: conferire l'una o l'altra sono scelte diverse
// (la prima lascia la carta del cimitero come prova, la seconda tiene quella in mano).
TEST_CASE("Detective's Phoenix: conferimento dalla mano e dal cimitero sono azioni distinte", "[phoenix]") {
    Staged st = staged_with_graveyard({"Detective's Phoenix", "Troll of Khazad-dûm"}, {"Detective's Phoenix"},
                                      {"Blood Crypt", "Stitcher's Supplier"});
    REQUIRE(is_decision(st.s, DecisionKind::Priority, kRakdos));
    std::vector<Zone> zones;
    for (const Action& a : legal(st.s)) {
        if (a.type == ActionType::CastSpell && a.value == kBestow) zones.push_back(st.s.objects[a.card].zone);
    }
    std::sort(zones.begin(), zones.end());
    CHECK(zones == std::vector<Zone>{Zone::Hand, Zone::Graveyard});
}

TEST_CASE("Detective's Phoenix: non si conferisce su Sanctifier en-Vec (protezione dal rosso)", "[phoenix]") {
    Staged st = staged_with_graveyard({"Troll of Khazad-dûm"}, {"Detective's Phoenix"},
                                      {"Blood Crypt", "Stitcher's Supplier"}, {"Sanctifier en-Vec"});
    pass_until(st, [](const GameState& s) { return s.turn.step == Step::Main1; });
    act(st, find_action(st.s, ActionType::CastSpell, "Detective's Phoenix", kBestow));
    for (const Action& a : legal(st.s)) CHECK(name_of(st.s, a.card) != "Sanctifier en-Vec");
}

TEST_CASE("Detective's Phoenix: nel cimitero conta come incantesimo e creatura", "[phoenix]") {
    GameState s = new_game(1);
    to_graveyard(s, kRakdos, {"Detective's Phoenix"});
    CHECK(s.players[kRakdos].gy.distinct_types() == 2);
}

// Forza bruta: i multinsiemi minimali di carte del cimitero con valore di mana
// totale >= 6 (nessuna carta superflua) contro i percorsi di decisione.
TEST_CASE("Detective's Phoenix: prove esaustive contro la forza bruta", "[phoenix][exhaustive]") {
    const std::vector<std::vector<const char*>> graveyards = {
        {"Lightning Bolt", "Lightning Bolt", "Flare of Malice", "Street Wraith", "Thoughtseize", "Swamp"},
        {"Troll of Khazad-dûm", "Street Wraith", "Stitcher's Supplier", "Stitcher's Supplier", "Mishra's Bauble",
         "Fear of Missing Out"},
        {"Lightning Bolt", "Lightning Bolt", "Lightning Bolt", "Thoughtseize", "Thoughtseize", "Fear of Missing Out",
         "Flare of Malice"},
    };
    int total = 0;
    for (const auto& gy : graveyards) {
        Staged st = staged_with_graveyard(gy, {"Detective's Phoenix"}, {"Blood Crypt", "Stitcher's Supplier"});
        pass_until(st, [](const GameState& s) { return s.turn.step == Step::Main1; });

        const std::vector<CardId> cards(st.s.players[kRakdos].graveyard.begin(), st.s.players[kRakdos].graveyard.end());
        auto mv = [&](CardId id) { return static_cast<int>(db_with_tokens().get(st.s.objects[id].def).mana_value); };
        std::set<std::multiset<std::string>> expected;
        for (std::size_t mask = 1; mask < (std::size_t{1} << cards.size()); ++mask) {
            int sum = 0, smallest = 1000;
            std::multiset<std::string> names;
            for (std::size_t i = 0; i < cards.size(); ++i) {
                if (((mask >> i) & 1u) == 0) continue;
                sum += mv(cards[i]);
                smallest = std::min(smallest, mv(cards[i]));
                names.insert(name_of(st.s, cards[i]));
            }
            if (sum >= 6 && sum - smallest < 6) expected.insert(names);  // minimale: togliere la più piccola basta a scendere
        }

        act(st, find_action(st.s, ActionType::CastSpell, "Detective's Phoenix", kBestow));
        act(st, target(st.s, "Stitcher's Supplier"));
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
    std::cout << "\n  prove della Phoenix: " << total << " insiemi confrontati con la forza bruta\n";
    CHECK(total > 10);
}

// ---------------------------------------------------------------------------
// Flare of Malice — {2}{B}{B} — Instant
//   You may sacrifice a nontoken black creature rather than pay this spell's
//   mana cost.
//   Each opponent sacrifices a creature or planeswalker with the greatest mana
//   value among creatures and planeswalkers they control.
// ---------------------------------------------------------------------------

TEST_CASE("Flare of Malice: l'avversario sacrifica la creatura di valore di mana piu' alto", "[flare]") {
    Staged st = staged(kRakdos, {"Plains"}, {"Flare of Malice"}, {"Guide of Souls", "Solitude"},
                       {"Swamp", "Swamp", "Blood Crypt", "Blood Crypt"});
    act(st, find_action(st.s, ActionType::CastSpell, "Flare of Malice", kNormal));
    pass_until(st, [](const GameState& s) { return s.stack_items.empty(); });
    CHECK(st.s.players[kUw].graveyard.size() == 1);
    CHECK(name_of(st.s, st.s.players[kUw].graveyard[0]) == "Solitude");
    CHECK(on_battlefield(st.s, "Guide of Souls"));
}

TEST_CASE("Flare of Malice: a pari valore di mana sceglie l'avversario", "[flare]") {
    Staged st = staged(kRakdos, {"Plains"}, {"Flare of Malice"}, {"Guide of Souls", "Ocelot Pride"},
                       {"Swamp", "Swamp", "Blood Crypt", "Blood Crypt"});
    act(st, find_action(st.s, ActionType::CastSpell, "Flare of Malice", kNormal));
    pass_until(st, [](const GameState& s) { return s.pending.kind == DecisionKind::SacrificeForEffect; });
    REQUIRE(is_decision(st.s, DecisionKind::SacrificeForEffect, kUw));
    CHECK(legal(st.s).size() == 2);
    act(st, find_action(st.s, ActionType::ChooseCard, "Ocelot Pride"));
    CHECK(on_battlefield(st.s, "Guide of Souls"));
    CHECK_FALSE(on_battlefield(st.s, "Ocelot Pride"));
}

TEST_CASE("Flare of Malice: copie identiche, nessuna decisione", "[flare]") {
    Staged st = staged(kRakdos, {"Plains"}, {"Flare of Malice"}, {"Guide of Souls", "Guide of Souls"},
                       {"Swamp", "Swamp", "Blood Crypt", "Blood Crypt"});
    act(st, find_action(st.s, ActionType::CastSpell, "Flare of Malice", kNormal));
    pass_until(st, [](const GameState& s) { return s.stack_items.empty(); });
    CHECK(st.s.players[kUw].graveyard.size() == 1);
}

TEST_CASE("Flare of Malice: costo alternativo, il sacrificio di Stitcher's Supplier macina", "[flare]") {
    Staged st = staged(kRakdos, {"Plains"}, {"Flare of Malice"}, {"Solitude"}, {"Stitcher's Supplier"});
    REQUIRE(is_decision(st.s, DecisionKind::Priority, kRakdos));  // istantaneo, senza mana
    act(st, find_action(st.s, ActionType::CastSpell, "Flare of Malice", kSacrificeAlt));
    // Unica creatura nera non token: sacrificata come costo; la sua macinata va in pila sopra Flare.
    CHECK(st.s.objects[find_cards(st.s, kRakdos, Zone::Graveyard, "Stitcher's Supplier")[0].card].zone ==
          Zone::Graveyard);
    pass_until(st, [](const GameState& s) { return s.stack_items.empty(); });
    CHECK(st.s.players[kRakdos].graveyard.size() == 5);  // Supplier, 3 macinate, Flare
    CHECK(st.s.players[kUw].graveyard.size() == 1);      // Solitude
}

TEST_CASE("Flare of Malice: costo alternativo, scelta tra creature nere", "[flare]") {
    Staged st = staged(kRakdos, {"Plains"}, {"Flare of Malice"}, {"Solitude"},
                       {"Stitcher's Supplier", "Street Wraith", "Fear of Missing Out"});
    act(st, find_action(st.s, ActionType::CastSpell, "Flare of Malice", kSacrificeAlt));
    REQUIRE(is_decision(st.s, DecisionKind::SacrificeForCost, kRakdos));
    std::set<std::string> options;
    for (const Action& a : legal(st.s)) options.insert(name_of(st.s, a.card));
    CHECK(options == std::set<std::string>{"Stitcher's Supplier", "Street Wraith"});  // FOMO è rossa
}

TEST_CASE("Flare of Malice: niente costo alternativo senza creature nere non token", "[flare]") {
    // La fetch dà a Rakdos una decisione nel mantenimento: si controllano le SUE azioni.
    Staged st = staged(kRakdos, {"Plains"}, {"Flare of Malice"}, {"Solitude"}, {"Marsh Flats", "Fear of Missing Out"});
    REQUIRE(is_decision(st.s, DecisionKind::Priority, kRakdos));
    CHECK_FALSE(can_cast(st.s, "Flare of Malice", kSacrificeAlt));
}

TEST_CASE("Flare of Malice: avversario senza creature, nessun effetto", "[flare]") {
    Staged st = staged(kRakdos, {"Plains"}, {"Flare of Malice"}, {}, {"Swamp", "Swamp", "Blood Crypt", "Blood Crypt"});
    act(st, find_action(st.s, ActionType::CastSpell, "Flare of Malice", kNormal));
    pass_until(st, [](const GameState& s) { return s.stack_items.empty(); });
    CHECK(st.s.players[kUw].graveyard.empty());
    CHECK(st.s.players[kRakdos].graveyard.size() == 1);
}

