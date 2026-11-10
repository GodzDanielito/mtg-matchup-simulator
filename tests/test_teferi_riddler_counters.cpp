// Test di Teferi, Time Raveler, Quantum Riddler, Consign to Memory e Force of Negation.
// Testo Oracle da Scryfall (bulk oracle_cards aggiornato il 9/11/2026, 10:02 UTC), riportato
// prima dei test di ogni carta. Regole: Comprehensive Rules in vigore dal 6/11/2026
// (planeswalker 306, abilità di fedeltà 606, azione di stato 704.5i, danno ai planeswalker
// 120.3c, attacchi ai planeswalker 506.2-506.4 e 508.1b, danno da combattimento 510.1b,
// replicare 702.56, copie di magie 707.10, neutralizzare 701.6, pescate sostituite 121.2a e
// 614.5, magie conferite 702.103b, "can't" batte "can" 101.2, bersagli illegali 608.2b).

#include "test_helpers.hpp"

#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace mtg;
using namespace test;

namespace {

constexpr PlayerId kUw = 0;
constexpr PlayerId kRakdos = 1;
constexpr std::uint8_t kPlusOne = 0;     // Teferi: +1
constexpr std::uint8_t kMinusThree = 1;  // Teferi: −3
constexpr const char* kTeferi = "Teferi, Time Raveler";

std::string name_of(const GameState& s, CardId id) { return db_with_tokens().get(s.objects[id].def).name; }

// Passa la priorità, non attacca e non blocca, finché la condizione non è vera. Qualunque
// altra decisione è un errore del test.
template <typename Pred>
void pass_until(Staged& st, Pred done) {
    for (int guard = 0; guard < 800 && !done(st.s); ++guard) {
        const DecisionKind k = st.s.pending.kind;
        if (k == DecisionKind::DeclareAttacker) {
            act(st, option(kNoAttack));
        } else if (k == DecisionKind::DeclareBlockers) {
            act(st, option(kDoneBlocking));
        } else {
            REQUIRE(k == DecisionKind::Priority);
            act(st, pass());
        }
    }
    REQUIRE(done(st.s));
}

void to_main(Staged& st, PlayerId p) {
    pass_until(st, [p](const GameState& s) {
        return s.turn.active_player == p && s.turn.step == Step::Main1 && s.pending.kind == DecisionKind::Priority &&
               s.pending.player == p;
    });
}

bool at_end_step_with_stack(const GameState& s) {
    return s.turn.step == Step::End && !s.stack_items.empty() && s.pending.kind == DecisionKind::Priority;
}

void pay_all(Staged& st) {
    while (st.s.pending.kind == DecisionKind::PayMana) act(st, option(0));
}

int untapped(const GameState& s, const char* name) {
    int n = 0;
    for (const CardId id : s.battlefield) {
        if (name_of(s, id) == name && !s.objects[id].has_flag(ObjectFlag::Tapped)) ++n;
    }
    return n;
}

// Paga scegliendo un modo che lasci stappata almeno una terra con quel nome.
void pay_leaving(Staged& st, const char* land) {
    while (st.s.pending.kind == DecisionKind::PayMana) {
        bool paid = false;
        for (const Action& a : legal(st.s)) {
            GameState copy = st.s;
            apply(copy, db_with_tokens(), a);
            if (untapped(copy, land) > 0) {
                act(st, a);
                paid = true;
                break;
            }
        }
        REQUIRE(paid);
    }
}

void resolve(Staged& st) {
    for (int guard = 0; guard < 50 && !st.s.stack_items.empty() && st.s.pending.kind == DecisionKind::Priority;
         ++guard) {
        act(st, pass());
    }
}

Action cast_action(const GameState& s, const char* name, CastMethod method = CastMethod::Normal) {
    return find_action(s, ActionType::CastSpell, name, static_cast<std::uint8_t>(method));
}

bool can_cast(const GameState& s, const char* name, CastMethod method = CastMethod::Normal) {
    for (const Action& a : legal(s)) {
        if (a.type == ActionType::CastSpell && a.value == static_cast<std::uint8_t>(method) && name_of(s, a.card) == name) {
            return true;
        }
    }
    return false;
}

bool can_activate(const GameState& s, const char* name, std::uint8_t value = 0) {
    for (const Action& a : legal(s)) {
        if (a.type == ActionType::ActivateAbility && a.value == value && name_of(s, a.card) == name) return true;
    }
    return false;
}

Action target(const GameState& s, const char* name) {
    for (const Action& a : legal(s)) {
        if (a.type == ActionType::TargetObject && name_of(s, a.card) == name) return a;
    }
    FAIL("bersaglio non trovato: " << name);
    return {};
}

Action choose(const GameState& s, const char* name) {
    for (const Action& a : legal(s)) {
        if (a.type == ActionType::ChooseCard && name_of(s, a.card) == name) return a;
    }
    FAIL("carta non trovata tra le scelte: " << name);
    return {};
}

// Bersagli in pila (Consign to Memory, Force of Negation): un'abilità innescata con quell'effetto...
Action stack_trigger(const GameState& s, StackEffect e) {
    for (const Action& a : legal(s)) {
        if (a.type != ActionType::TargetStackItem) continue;
        const StackItem& item = s.stack_items[a.value];
        if (item.kind == StackKind::TriggeredAbility && item.effect == e) return a;
    }
    FAIL("abilita' innescata non trovata tra i bersagli");
    return {};
}

// ...o la magia con quel nome.
Action stack_spell(const GameState& s, const char* name) {
    for (const Action& a : legal(s)) {
        if (a.type != ActionType::TargetStackItem) continue;
        const StackItem& item = s.stack_items[a.value];
        if (item.kind == StackKind::Spell && db_with_tokens().get(item.source_def).name == name) return a;
    }
    FAIL("magia non trovata tra i bersagli: " << name);
    return {};
}

// ...o una copia di una magia (707.10).
Action stack_copy(const GameState& s) {
    for (const Action& a : legal(s)) {
        if (a.type == ActionType::TargetStackItem && s.stack_items[a.value].copy) return a;
    }
    FAIL("copia non trovata tra i bersagli");
    return {};
}

int stack_targets(const GameState& s) {
    const auto actions = legal(s);
    return static_cast<int>(std::count_if(actions.begin(), actions.end(),
                                          [](const Action& a) { return a.type == ActionType::TargetStackItem; }));
}

std::set<std::string> target_choices(const GameState& s) {
    std::set<std::string> out;
    for (const Action& a : legal(s)) {
        if (a.type == ActionType::TargetObject) out.insert(name_of(s, a.card));
    }
    return out;
}

std::set<std::string> card_choices(const GameState& s) {
    std::set<std::string> out;
    for (const Action& a : legal(s)) {
        if (a.type == ActionType::ChooseCard) out.insert(name_of(s, a.card));
    }
    return out;
}

std::set<int> option_values(const GameState& s) {
    std::set<int> out;
    for (const Action& a : legal(s)) {
        if (a.type == ActionType::ChooseOption) out.insert(a.value);
    }
    return out;
}

bool has_option(const GameState& s, std::uint8_t value) { return option_values(s).contains(value); }

bool in_zone(const GameState& s, PlayerId owner, Zone z, const char* name) {
    for (std::size_t id = 0; id < kMaxCards; ++id) {
        const CardObject& o = s.objects[id];
        if (o.owner == owner && o.zone == z && db_with_tokens().get(o.def).name == name) return true;
    }
    return false;
}

int stack_count(const GameState& s, StackEffect e) {
    return static_cast<int>(std::count_if(s.stack_items.begin(), s.stack_items.end(),
                                          [&](const StackItem& i) { return i.effect == e; }));
}

void to_graveyard(GameState& s, PlayerId p, std::initializer_list<const char*> names) {
    for (const char* name : names) {
        move_object(s, db_with_tokens(), find_cards(s, p, Zone::Library, name)[0], Zone::Graveyard, Cause::Test);
    }
    require_invariants(s);
}

void put_first(Staged& st, StackEffect e) {
    REQUIRE(st.s.pending.kind == DecisionKind::OrderTriggers);
    for (std::size_t i = 0; i < st.s.pending_triggers.size(); ++i) {
        if (st.s.pending_triggers[i].effect == e) {
            act(st, option(static_cast<std::uint8_t>(i)));
            return;
        }
    }
    FAIL("trigger non trovato");
}

// Dichiara gli attaccanti: per ogni creatura nominata, "giocatore" o il nome del planeswalker
// attaccato. Le altre non attaccano.
void attack(Staged& st, std::vector<std::pair<std::string, std::string>> plan) {
    pass_until(st, [](const GameState& s) { return s.pending.kind == DecisionKind::DeclareAttacker; });
    while (st.s.pending.kind == DecisionKind::DeclareAttacker) {
        const std::string who = name_of(st.s, st.s.pending.subject.card);
        const auto it = std::find_if(plan.begin(), plan.end(), [&](const auto& p) { return p.first == who; });
        if (it == plan.end()) {
            act(st, option(kNoAttack));
            continue;
        }
        const std::string what = it->second;
        plan.erase(it);
        if (what == "giocatore") {
            act(st, Action{ActionType::TargetPlayer, kNoCard, static_cast<PlayerId>(1 - st.s.turn.active_player)});
        } else {
            act(st, Action{ActionType::TargetObject, find_on_battlefield(st.s, what).card, 0});
        }
    }
}

int loyalty(const GameState& s) { return s.objects[find_on_battlefield(s, kTeferi).card].counters.loyalty; }

int begin_combats(const GameLog& log, std::uint16_t turn) {
    return static_cast<int>(std::count_if(log.entries.begin(), log.entries.end(), [&](const LogEntry& e) {
        return e.kind == LogEntry::Kind::StepStart && e.step == Step::BeginCombat && e.turn == turn;
    }));
}

}  // namespace

// ---------------------------------------------------------------------------
// Teferi, Time Raveler — {1}{W}{U} — Legendary Planeswalker — Teferi — Loyalty 4
//   Each opponent can cast spells only any time they could cast a sorcery.
//   +1: Until your next turn, you may cast sorcery spells as though they had flash.
//   −3: Return up to one target artifact, creature, or enchantment to its owner's
//   hand. Draw a card.
// CR 306.5b: entra con tanti segnalini fedeltà quanta la fedeltà stampata. 606.3: le
// abilità di fedeltà si attivano con la priorità, a pila vuota, in una fase principale
// del proprio turno, e solo se in quel turno nessuna è già stata attivata. 606.6: un
// costo negativo richiede almeno altrettanti segnalini. 704.5i: fedeltà 0, cimitero.
// ---------------------------------------------------------------------------

TEST_CASE("Teferi: entra con quattro segnalini fedelta'", "[uw][teferi]") {
    Staged st = staged(kUw, {kTeferi}, {}, {"Plains", "Hallowed Fountain", "Hallowed Fountain"});
    to_main(st, kUw);
    act(st, cast_action(st.s, kTeferi));
    pay_all(st);
    resolve(st);
    REQUIRE(in_zone(st.s, kUw, Zone::Battlefield, kTeferi));
    CHECK(loyalty(st.s) == 4);
    CHECK(db_with_tokens().get(db_with_tokens().id_of(kTeferi)).loyalty == 4);
}

TEST_CASE("Teferi: abilita' di fedelta' una volta per turno, a velocita' di stregoneria", "[uw][teferi]") {
    // Arid Mesa: UW ha la priorità anche fuori dalla fase principale e nel turno di Rakdos.
    Staged st = staged(kUw, {}, {}, {kTeferi, "Arid Mesa"});
    REQUIRE(is_decision(st.s, DecisionKind::Priority, kUw));
    REQUIRE(st.s.turn.step == Step::Upkeep);
    CHECK_FALSE(can_activate(st.s, kTeferi, kPlusOne));  // non è una fase principale

    to_main(st, kUw);
    CHECK(can_activate(st.s, kTeferi, kPlusOne));
    CHECK(can_activate(st.s, kTeferi, kMinusThree));
    act(st, find_action(st.s, ActionType::ActivateAbility, kTeferi, kPlusOne));
    CHECK(loyalty(st.s) == 5);  // il costo si paga attivando
    REQUIRE(st.s.stack_items.size() == 1);
    CHECK_FALSE(can_activate(st.s, kTeferi, kMinusThree));  // pila non vuota
    resolve(st);
    CHECK(st.s.players[kUw].sorceries_as_flash);
    REQUIRE(is_decision(st.s, DecisionKind::Priority, kUw));
    CHECK_FALSE(can_activate(st.s, kTeferi, kPlusOne));  // già attivata in questo turno
    CHECK_FALSE(can_activate(st.s, kTeferi, kMinusThree));

    pass_until(st, [](const GameState& s) {
        return s.turn.active_player == kRakdos && s.pending.kind == DecisionKind::Priority && s.pending.player == kUw;
    });
    CHECK_FALSE(can_activate(st.s, kTeferi, kPlusOne));  // non è il proprio turno

    to_main(st, kUw);  // turno successivo di UW: di nuovo disponibile
    CHECK(can_activate(st.s, kTeferi, kPlusOne));
    CHECK(loyalty(st.s) == 5);
    CHECK_FALSE(st.s.players[kUw].sorceries_as_flash);  // "until your next turn"
}

TEST_CASE("Teferi: -3 riporta in mano fino a un artefatto, creatura o incantesimo e fa pescare", "[uw][teferi]") {
    Staged st = staged(kUw, {}, {}, {kTeferi, "Ocelot Pride", "Arid Mesa"},
                       {"Nethergoyf", "Mishra's Bauble", "Swamp", "Fear of Missing Out"});
    to_main(st, kUw);
    const std::size_t hand = st.s.players[kUw].hand.size();
    act(st, find_action(st.s, ActionType::ActivateAbility, kTeferi, kMinusThree));
    REQUIRE(is_decision(st.s, DecisionKind::ChooseTarget, kUw));
    // Non le terre, non Teferi stesso (è un planeswalker).
    CHECK(target_choices(st.s) ==
          std::set<std::string>{"Ocelot Pride", "Nethergoyf", "Mishra's Bauble", "Fear of Missing Out"});
    CHECK(has_option(st.s, kNoTarget));  // "up to one"
    SECTION("un bersaglio: torna in mano al proprietario, poi si pesca") {
        act(st, target(st.s, "Nethergoyf"));
        CHECK(loyalty(st.s) == 1);
        resolve(st);
        CHECK(in_zone(st.s, kRakdos, Zone::Hand, "Nethergoyf"));
        CHECK(st.s.players[kUw].hand.size() == hand + 1);
    }
    SECTION("nessun bersaglio: si pesca soltanto") {
        act(st, option(kNoTarget));
        CHECK(loyalty(st.s) == 1);
        resolve(st);
        CHECK(in_zone(st.s, kRakdos, Zone::Battlefield, "Nethergoyf"));
        CHECK(st.s.players[kUw].hand.size() == hand + 1);
    }
    SECTION("il bersaglio scelto sparisce: l'abilita' non si risolve, niente pescata (608.2b)") {
        act(st, target(st.s, "Nethergoyf"));
        move_object(st.s, db_with_tokens(), find_on_battlefield(st.s, "Nethergoyf"), Zone::Graveyard, Cause::Test);
        require_invariants(st.s);
        resolve(st);
        CHECK(st.s.players[kUw].hand.size() == hand);
    }
}

TEST_CASE("Teferi: -3 solo con almeno tre segnalini; a zero va nel cimitero e l'abilita' si risolve lo stesso",
          "[uw][teferi]") {
    Staged st = staged(kUw, {}, {}, {kTeferi, "Arid Mesa"});
    to_main(st, kUw);
    const CardId teferi = find_on_battlefield(st.s, kTeferi).card;
    st.s.objects[teferi].counters.loyalty = 2;
    CHECK_FALSE(can_activate(st.s, kTeferi, kMinusThree));  // 606.6
    CHECK(can_activate(st.s, kTeferi, kPlusOne));
    st.s.objects[teferi].counters.loyalty = 3;
    const std::size_t hand = st.s.players[kUw].hand.size();
    act(st, find_action(st.s, ActionType::ActivateAbility, kTeferi, kMinusThree));  // nessun bersaglio possibile
    CHECK(in_zone(st.s, kUw, Zone::Graveyard, kTeferi));  // 704.5i
    REQUIRE(st.s.stack_items.size() == 1);
    resolve(st);
    CHECK(st.s.players[kUw].hand.size() == hand + 1);
}

TEST_CASE("Teferi: -3 su un token e su una Phoenix conferita", "[uw][teferi][phoenix]") {
    Staged st{new_game(1), {}};
    put(st.s, kUw, {kTeferi, "Arid Mesa"}, Zone::Battlefield);
    put(st.s, kRakdos, {"Stitcher's Supplier", "Detective's Phoenix"}, Zone::Battlefield);
    create_token(st.s, db_with_tokens(), db_with_tokens().id_of("Cat Token"), kUw);
    const ObjRef host = find_on_battlefield(st.s, "Stitcher's Supplier");
    CardObject& phoenix = st.s.objects[find_on_battlefield(st.s, "Detective's Phoenix").card];
    phoenix.set_flag(ObjectFlag::Bestowed);
    phoenix.attached_to = host;
    start_game(st.s, db_with_tokens(), StartOptions{kUw, false}, &st.log);
    require_invariants(st.s);
    to_main(st, kUw);
    act(st, find_action(st.s, ActionType::ActivateAbility, kTeferi, kMinusThree));
    SECTION("il token smette di esistere") {
        act(st, target(st.s, "Cat Token"));
        resolve(st);
        for (std::size_t slot = kMaxCards; slot < kMaxObjects; ++slot) CHECK(st.s.objects[slot].zone == Zone::None);
        CHECK(st.s.players[kUw].hand.size() == 1);  // solo la carta pescata
    }
    SECTION("la Phoenix conferita e' un incantesimo: torna in mano a Rakdos") {
        act(st, target(st.s, "Detective's Phoenix"));
        resolve(st);
        CHECK(in_zone(st.s, kRakdos, Zone::Hand, "Detective's Phoenix"));
        CHECK(power(st.s, db_with_tokens(), host.card) == 1);
    }
}

TEST_CASE("Teferi: gli avversari lanciano magie solo quando potrebbero lanciare una stregoneria", "[uw][teferi]") {
    const std::initializer_list<const char*> rakdos_hand = {"Lightning Bolt", "Flare of Malice", "Street Wraith"};
    const std::initializer_list<const char*> rakdos_field = {"Blood Crypt", "Blood Crypt", "Swamp",       "Swamp",
                                                             "Bloodstained Mire", "Mishra's Bauble", "Stitcher's Supplier"};
    const auto sacrifice = CastMethod::SacrificeAlternative;
    SECTION("nel turno di UW: niente Bolt ne' Flare, ma ciclo, fetch e Bauble si'") {
        Staged st = staged(kUw, {}, rakdos_hand, {kTeferi}, rakdos_field);
        REQUIRE(st.s.turn.active_player == kUw);
        REQUIRE(is_decision(st.s, DecisionKind::Priority, kRakdos));
        CHECK_FALSE(can_cast(st.s, "Lightning Bolt"));
        CHECK_FALSE(can_cast(st.s, "Flare of Malice"));
        CHECK_FALSE(can_cast(st.s, "Flare of Malice", sacrifice));
        CHECK(can_activate(st.s, "Street Wraith"));
        CHECK(can_activate(st.s, "Bloodstained Mire"));
        CHECK(can_activate(st.s, "Mishra's Bauble"));
    }
    SECTION("senza Teferi le stesse magie si lanciano nel turno di UW") {
        Staged st = staged(kUw, {}, rakdos_hand, {}, rakdos_field);
        REQUIRE(st.s.turn.active_player == kUw);
        REQUIRE(is_decision(st.s, DecisionKind::Priority, kRakdos));
        CHECK(can_cast(st.s, "Lightning Bolt"));
        CHECK(can_cast(st.s, "Flare of Malice"));
        CHECK(can_cast(st.s, "Flare of Malice", sacrifice));
    }
    SECTION("nel proprio turno: solo in fase principale e a pila vuota") {
        Staged st = staged(kRakdos, {}, rakdos_hand, {kTeferi}, rakdos_field);
        REQUIRE(is_decision(st.s, DecisionKind::Priority, kRakdos));
        REQUIRE(st.s.turn.step == Step::Upkeep);
        CHECK_FALSE(can_cast(st.s, "Lightning Bolt"));  // mantenimento
        to_main(st, kRakdos);
        CHECK(can_cast(st.s, "Lightning Bolt"));
        CHECK(can_cast(st.s, "Flare of Malice"));
        CHECK(can_cast(st.s, "Flare of Malice", sacrifice));
        // Mishra's Bauble in pila: la pila non è più vuota.
        act(st, find_action(st.s, ActionType::ActivateAbility, "Mishra's Bauble"));
        act(st, Action{ActionType::TargetPlayer, kNoCard, kRakdos});
        REQUIRE(is_decision(st.s, DecisionKind::Priority, kRakdos));
        REQUIRE(st.s.stack_items.size() == 1);
        CHECK_FALSE(can_cast(st.s, "Lightning Bolt"));
        CHECK_FALSE(can_cast(st.s, "Flare of Malice"));
        CHECK_FALSE(can_cast(st.s, "Flare of Malice", sacrifice));
        CHECK(can_activate(st.s, "Street Wraith"));      // le abilità non sono magie
        CHECK(can_activate(st.s, "Bloodstained Mire"));
    }
}

TEST_CASE("Teferi: +1, stregonerie come se avessero lampo fino al proprio prossimo turno", "[uw][teferi]") {
    // Nelle liste UW non ha stregonerie: mazzo di prova con Teferi e Thoughtseize.
    Decklist deck;
    deck.name = "Teferi con stregonerie (prova)";
    deck.format = "Modern";
    deck.main = {{kTeferi, 1}, {"Thoughtseize", 4}, {"Swamp", 30}, {"Plains", 25}};
    const bool plus_one = GENERATE(true, false);
    INFO("+1 attivato: " << plus_one);
    Staged st{create_game(db_with_tokens(), deck, rakdos_deck(), 1), {}};
    put(st.s, 0, {kTeferi, "Swamp"}, Zone::Battlefield);
    put(st.s, 0, {"Thoughtseize"}, Zone::Hand);
    start_game(st.s, db_with_tokens(), StartOptions{0, false}, &st.log);
    require_invariants(st.s);
    to_main(st, 0);
    if (plus_one) {
        act(st, find_action(st.s, ActionType::ActivateAbility, kTeferi, kPlusOne));
        resolve(st);
        CHECK(st.s.players[0].sorceries_as_flash);
    }
    // Turno dell'avversario: Thoughtseize si lancia solo con il +1.
    bool castable_in_opponents_turn = false;
    for (int guard = 0; guard < 300 && st.s.turn.turn_number < 3; ++guard) {
        if (st.s.pending.kind == DecisionKind::DeclareAttacker) {
            act(st, option(kNoAttack));
            continue;
        }
        REQUIRE(st.s.pending.kind == DecisionKind::Priority);
        if (st.s.turn.active_player == 1 && st.s.pending.player == 0) {
            castable_in_opponents_turn = castable_in_opponents_turn || can_cast(st.s, "Thoughtseize");
        }
        act(st, pass());
    }
    REQUIRE(st.s.turn.turn_number == 3);
    CHECK(castable_in_opponents_turn == plus_one);
    CHECK_FALSE(st.s.players[0].sorceries_as_flash);  // finisce quando inizia il proprio turno
}

TEST_CASE("Teferi: l'avversario non rilancia una magia con il rimbalzo", "[uw][teferi][ephemerate]") {
    // 101.2: la restrizione di Teferi batte il permesso del rimbalzo. Al mantenimento non si
    // potrebbe lanciare una stregoneria, quindi Ephemerate resta in esilio. Serve un mirror di UW.
    Staged st{create_game(db_with_tokens(), uw_sim_deck(), uw_sim_deck(), 1), {}};
    put(st.s, 0, {kTeferi}, Zone::Battlefield);
    put(st.s, 1, {"Ocelot Pride", "Plains"}, Zone::Battlefield);
    put(st.s, 1, {"Ephemerate"}, Zone::Hand);
    start_game(st.s, db_with_tokens(), StartOptions{1, false}, &st.log);
    require_invariants(st.s);
    to_main(st, 1);
    act(st, cast_action(st.s, "Ephemerate"));  // fase principale, pila vuota: permesso
    act(st, target(st.s, "Ocelot Pride"));
    pay_all(st);
    resolve(st);
    REQUIRE(in_zone(st.s, 1, Zone::Exile, "Ephemerate"));
    // pass_until fallisce se compare la decisione di rilancio (CastFromExile).
    pass_until(st, [](const GameState& s) { return s.turn.turn_number >= 3 && s.turn.step >= Step::Main1; });
    CHECK(in_zone(st.s, 1, Zone::Exile, "Ephemerate"));
    CHECK(st.s.delayed.empty());
}

TEST_CASE("Teferi: Lightning Bolt lo puo' bersagliare, e il danno toglie segnalini fedelta'", "[uw][teferi][bolt]") {
    Staged st = staged(kRakdos, {}, {"Lightning Bolt", "Lightning Bolt"}, {kTeferi}, {"Blood Crypt", "Blood Crypt"});
    to_main(st, kRakdos);
    act(st, cast_action(st.s, "Lightning Bolt"));
    REQUIRE(is_decision(st.s, DecisionKind::ChooseTarget, kRakdos));
    CHECK(target_choices(st.s) == std::set<std::string>{kTeferi});
    act(st, target(st.s, kTeferi));
    pay_all(st);
    resolve(st);
    REQUIRE(in_zone(st.s, kUw, Zone::Battlefield, kTeferi));
    CHECK(loyalty(st.s) == 1);  // 120.3c
    CHECK(st.s.objects[find_on_battlefield(st.s, kTeferi).card].damage == 0);  // nessun danno segnato

    act(st, cast_action(st.s, "Lightning Bolt"));
    act(st, target(st.s, kTeferi));
    pay_all(st);
    resolve(st);
    CHECK(in_zone(st.s, kUw, Zone::Graveyard, kTeferi));  // 704.5i
    CHECK(st.s.players[kUw].life == 20);
}

TEST_CASE("Teferi: si puo' attaccare, e il danno da combattimento non bloccato toglie fedelta'", "[uw][teferi][combat]") {
    Staged st = staged(kRakdos, {}, {}, {kTeferi, "Ocelot Pride"}, {"Stitcher's Supplier", "Street Wraith"});
    SECTION("ogni attaccante sceglie tra il giocatore e Teferi") {
        pass_until(st, [](const GameState& s) { return s.pending.kind == DecisionKind::DeclareAttacker; });
        const auto actions = legal(st.s);
        CHECK(actions.size() == 3);  // non attacca, attacca UW, attacca Teferi
        CHECK(std::count(actions.begin(), actions.end(),
                         Action{ActionType::TargetObject, find_on_battlefield(st.s, kTeferi).card, 0}) == 1);
    }
    SECTION("non bloccati: uno su Teferi, uno sul giocatore") {
        attack(st, {{"Stitcher's Supplier", kTeferi}, {"Street Wraith", "giocatore"}});
        REQUIRE(st.s.combat.attackers.size() == 2);
        pass_until(st, [](const GameState& s) { return s.turn.turn_number == 2; });  // UW non blocca
        CHECK(loyalty(st.s) == 3);
        CHECK(st.s.players[kUw].life == 17);
    }
    SECTION("bloccato: Teferi non subisce danno") {
        attack(st, {{"Street Wraith", kTeferi}});
        REQUIRE(is_decision(st.s, DecisionKind::DeclareBlockers, kUw));
        act(st, choose(st.s, "Ocelot Pride"));
        pass_until(st, [](const GameState& s) { return s.turn.turn_number == 2; });
        CHECK(loyalty(st.s) == 4);
        CHECK(st.s.players[kUw].life == 21);  // legame vitale di Ocelot Pride
        CHECK(in_zone(st.s, kUw, Zone::Graveyard, "Ocelot Pride"));
    }
    SECTION("quattro danni: Teferi va nel cimitero") {
        attack(st, {{"Stitcher's Supplier", kTeferi}, {"Street Wraith", kTeferi}});
        pass_until(st, [](const GameState& s) { return s.turn.turn_number == 2; });
        CHECK(in_zone(st.s, kUw, Zone::Graveyard, kTeferi));
        CHECK(st.s.players[kUw].life == 20);
    }
    SECTION("Teferi lascia il campo prima del danno: l'attaccante non infligge danno (510.1b)") {
        attack(st, {{"Street Wraith", kTeferi}});
        REQUIRE(is_decision(st.s, DecisionKind::DeclareBlockers, kUw));
        move_object(st.s, db_with_tokens(), find_on_battlefield(st.s, kTeferi), Zone::Graveyard, Cause::Test);
        require_invariants(st.s);
        pass_until(st, [](const GameState& s) { return s.turn.turn_number == 2; });
        CHECK(st.s.players[kUw].life == 20);
    }
}

TEST_CASE("Teferi e Flare of Malice: obbligato, a scelta di UW, protetto", "[uw][teferi][flare]") {
    // "Each opponent sacrifices a creature or planeswalker with the greatest mana value among
    // creatures and planeswalkers they control." Teferi ha valore di mana 3.
    const std::initializer_list<const char*> lands = {"Blood Crypt", "Blood Crypt", "Swamp", "Swamp"};
    SECTION("solo creature di valore piu' basso: Teferi e' la scelta obbligata") {
        Staged st = staged(kRakdos, {}, {"Flare of Malice"}, {kTeferi, "Ocelot Pride", "Guide of Souls"}, lands);
        to_main(st, kRakdos);
        act(st, cast_action(st.s, "Flare of Malice"));
        pay_all(st);
        resolve(st);
        CHECK(in_zone(st.s, kUw, Zone::Graveyard, kTeferi));
        CHECK(in_zone(st.s, kUw, Zone::Battlefield, "Ocelot Pride"));
        CHECK(in_zone(st.s, kUw, Zone::Battlefield, "Guide of Souls"));
    }
    SECTION("pari valore con Recruiter of the Guard: sceglie UW") {
        Staged st = staged(kRakdos, {}, {"Flare of Malice"}, {kTeferi, "Recruiter of the Guard"}, lands);
        to_main(st, kRakdos);
        act(st, cast_action(st.s, "Flare of Malice"));
        pay_all(st);
        resolve(st);
        REQUIRE(is_decision(st.s, DecisionKind::SacrificeForEffect, kUw));
        CHECK(card_choices(st.s) == std::set<std::string>{kTeferi, "Recruiter of the Guard"});
        act(st, choose(st.s, "Recruiter of the Guard"));
        CHECK(in_zone(st.s, kUw, Zone::Battlefield, kTeferi));
        CHECK(in_zone(st.s, kUw, Zone::Graveyard, "Recruiter of the Guard"));
    }
    SECTION("Witch Enchanter (4) lo protegge") {
        Staged st = staged(kRakdos, {}, {"Flare of Malice"}, {kTeferi, "Witch Enchanter"}, lands);
        to_main(st, kRakdos);
        act(st, cast_action(st.s, "Flare of Malice"));
        pay_all(st);
        resolve(st);
        CHECK(in_zone(st.s, kUw, Zone::Battlefield, kTeferi));
        CHECK(in_zone(st.s, kUw, Zone::Graveyard, "Witch Enchanter"));
    }
}

// ---------------------------------------------------------------------------
// Quantum Riddler — {3}{U}{U} — Creature — Sphinx 4/6
//   Flying
//   When this creature enters, draw a card.
//   As long as you have one or fewer cards in hand, if you would draw one or more
//   cards, you draw that many cards plus one instead.
//   Warp {1}{U}
// CR 121.2a: la sostituzione modifica l'istruzione di pescare prima delle singole
// pescate; 614.5: ogni effetto di sostituzione si applica una volta sola all'evento.
// ---------------------------------------------------------------------------

TEST_CASE("Quantum Riddler: volare; entrando fa pescare una carta", "[uw][riddler]") {
    Staged st = staged(kUw, {"Quantum Riddler", "Plains", "Plains", "Plains"}, {},
                       {"Hallowed Fountain", "Hallowed Fountain", "Hallowed Fountain", "Hallowed Fountain",
                        "Meticulous Archive"});
    to_main(st, kUw);
    act(st, cast_action(st.s, "Quantum Riddler"));
    pay_all(st);
    resolve(st);  // con tre carte in mano: la sostituzione non si applica
    const CardId riddler = find_on_battlefield(st.s, "Quantum Riddler").card;
    CHECK(has_keyword(st.s, db_with_tokens(), riddler, Keyword::Flying));
    CHECK(st.s.players[kUw].hand.size() == 4);  // tre carte in mano: una pescata sola
}

TEST_CASE("Quantum Riddler: con una carta in mano o nessuna si pesca una carta in piu'", "[uw][riddler]") {
    const std::initializer_list<const char*> lands = {"Hallowed Fountain", "Hallowed Fountain", "Hallowed Fountain",
                                                      "Hallowed Fountain", "Meticulous Archive"};
    SECTION("entra con la mano vuota: due carte") {
        Staged st = staged(kUw, {"Quantum Riddler"}, {}, lands);
        to_main(st, kUw);
        act(st, cast_action(st.s, "Quantum Riddler"));
        pay_all(st);
        resolve(st);
        CHECK(st.s.players[kUw].hand.size() == 2);
    }
    SECTION("entra con una carta in mano: due carte") {
        Staged st = staged(kUw, {"Quantum Riddler", "Plains"}, {}, lands);
        to_main(st, kUw);
        act(st, cast_action(st.s, "Quantum Riddler"));
        pay_all(st);
        resolve(st);
        CHECK(st.s.players[kUw].hand.size() == 3);
    }
    SECTION("entra con due carte in mano: una carta") {
        Staged st = staged(kUw, {"Quantum Riddler", "Plains", "Plains"}, {}, lands);
        to_main(st, kUw);
        act(st, cast_action(st.s, "Quantum Riddler"));
        pay_all(st);
        resolve(st);
        CHECK(st.s.players[kUw].hand.size() == 3);
    }
    SECTION("pescata del turno con una carta in mano: due carte") {
        Staged st = staged(kRakdos, {"Plains"}, {}, {"Quantum Riddler", "Arid Mesa"});
        to_main(st, kUw);  // turno 2: la prima pescata di UW
        CHECK(st.s.players[kUw].hand.size() == 3);
    }
    SECTION("pescata del turno con due carte in mano: una carta") {
        Staged st = staged(kRakdos, {"Plains", "Plains"}, {}, {"Quantum Riddler", "Arid Mesa"});
        to_main(st, kUw);
        CHECK(st.s.players[kUw].hand.size() == 3);
    }
    SECTION("due Quantum Riddler: ognuno aggiunge una carta") {
        Staged st = staged(kRakdos, {}, {}, {"Quantum Riddler", "Quantum Riddler", "Arid Mesa"});
        to_main(st, kUw);
        CHECK(st.s.players[kUw].hand.size() == 3);
    }
    SECTION("il Quantum Riddler dell'avversario non conta") {
        Staged st{create_game(db_with_tokens(), uw_sim_deck(), uw_sim_deck(), 1), {}};
        put(st.s, 0, {"Quantum Riddler"}, Zone::Battlefield);
        put(st.s, 1, {"Arid Mesa"}, Zone::Battlefield);
        start_game(st.s, db_with_tokens(), StartOptions{0, false}, &st.log);
        to_main(st, 1);  // turno 2: pesca il giocatore 1, a mano vuota
        CHECK(st.s.players[1].hand.size() == 1);
    }
}

TEST_CASE("Quantum Riddler: warp, esilio alla sottofase finale", "[uw][riddler][warp]") {
    Staged st = staged(kUw, {"Quantum Riddler"}, {}, {"Hallowed Fountain", "Plains", "Arid Mesa"});
    to_main(st, kUw);
    CHECK(can_cast(st.s, "Quantum Riddler", CastMethod::Warp));
    CHECK_FALSE(can_cast(st.s, "Quantum Riddler"));  // due mana soli
    act(st, cast_action(st.s, "Quantum Riddler", CastMethod::Warp));
    pay_all(st);
    resolve(st);
    REQUIRE(in_zone(st.s, kUw, Zone::Battlefield, "Quantum Riddler"));
    CHECK(st.s.players[kUw].hand.size() == 2);  // entra a mano vuota
    pass_until(st, at_end_step_with_stack);
    CHECK(stack_count(st.s, StackEffect::WarpExile) == 1);
    resolve(st);
    REQUIRE(in_zone(st.s, kUw, Zone::Exile, "Quantum Riddler"));
    CardId riddler = kNoCard;
    for (const CardId id : st.s.exile) {
        if (name_of(st.s, id) == "Quantum Riddler") riddler = id;
    }
    CHECK(st.s.objects[riddler].has_flag(ObjectFlag::WarpExiled));
}

// ---------------------------------------------------------------------------
// Consign to Memory — {U} — Instant
//   Replicate {1} (When you cast this spell, copy it for each time you paid its
//   replicate cost. You may choose new targets for the copies.)
//   Counter target triggered ability or colorless spell.
// CR 701.6a: neutralizzare toglie dalla pila; una magia va nel cimitero del
// proprietario. 702.56a: replicare è un costo aggiuntivo e un'abilità innescata
// "When you cast this spell". 707.10: le copie sono magie senza carta; 707.10c: per
// ogni copia si può lasciare il bersaglio o sceglierne uno nuovo, legale.
// Bersagli: QUALSIASI abilità innescata in pila (anche ritardata, anche propria), mai
// un'abilità attivata (fetch, ciclo, fedeltà) né una magia colorata.
// ---------------------------------------------------------------------------

TEST_CASE("Consign to Memory: il ritorno di Phelia neutralizzato lascia la carta in esilio per sempre",
          "[uw][consign][phelia]") {
    Staged st = staged(kUw, {"Consign to Memory"}, {}, {"Phelia, Exuberant Shepherd", "Hallowed Fountain"},
                       {"Nethergoyf"});
    attack(st, {{"Phelia, Exuberant Shepherd", "giocatore"}});
    REQUIRE(is_decision(st.s, DecisionKind::ChooseTarget, kUw));
    act(st, target(st.s, "Nethergoyf"));
    // UW potrebbe neutralizzare il proprio trigger di esilio: passa una volta sola (resolve() passerebbe
    // anche il ritorno, che la partita raggiunge da sola nella sottofase finale).
    REQUIRE(is_decision(st.s, DecisionKind::Priority, kUw));
    act(st, pass());
    REQUIRE(in_zone(st.s, kRakdos, Zone::Exile, "Nethergoyf"));
    pass_until(st, at_end_step_with_stack);
    REQUIRE(stack_count(st.s, StackEffect::PheliaReturn) == 1);
    REQUIRE(is_decision(st.s, DecisionKind::Priority, kUw));
    act(st, cast_action(st.s, "Consign to Memory"));
    REQUIRE(is_decision(st.s, DecisionKind::ChooseTarget, kUw));
    CHECK(stack_targets(st.s) == 1);
    act(st, stack_trigger(st.s, StackEffect::PheliaReturn));  // un trigger ritardato, e proprio
    pay_all(st);
    resolve(st);
    CHECK(st.s.stack_items.empty());
    CHECK(in_zone(st.s, kUw, Zone::Graveyard, "Consign to Memory"));
    CHECK(st.s.delayed.empty());  // il trigger ritardato è scattato una volta sola (603.7b)
    pass_until(st, [](const GameState& s) { return s.turn.turn_number >= 4; });
    CHECK(in_zone(st.s, kRakdos, Zone::Exile, "Nethergoyf"));
    CHECK(st.s.objects[find_on_battlefield(st.s, "Phelia, Exuberant Shepherd").card].counters.plus_one == 0);
}

TEST_CASE("Consign to Memory: il sacrificio di Solitude evocata neutralizzato la lascia in campo",
          "[uw][consign][solitude][evoke]") {
    Staged st = staged(kUw, {"Solitude", "Ocelot Pride", "Consign to Memory"}, {}, {"Hallowed Fountain"},
                       {"Stitcher's Supplier"});
    to_main(st, kUw);
    act(st, cast_action(st.s, "Solitude", CastMethod::Evoke));  // Ocelot Pride è l'unica carta bianca
    REQUIRE(in_zone(st.s, kUw, Zone::Exile, "Ocelot Pride"));
    resolve(st);
    put_first(st, StackEffect::ExileOtherCreature);
    act(st, target(st.s, "Stitcher's Supplier"));
    REQUIRE(is_decision(st.s, DecisionKind::Priority, kUw));
    act(st, cast_action(st.s, "Consign to Memory"));
    REQUIRE(is_decision(st.s, DecisionKind::ChooseTarget, kUw));
    CHECK(stack_targets(st.s) == 2);  // entrata e sacrificio
    act(st, stack_trigger(st.s, StackEffect::EvokeSacrifice));
    pay_all(st);
    resolve(st);
    CHECK(st.s.stack_items.empty());
    CHECK(in_zone(st.s, kUw, Zone::Battlefield, "Solitude"));
    CHECK(in_zone(st.s, kRakdos, Zone::Exile, "Stitcher's Supplier"));
    pass_until(st, [](const GameState& s) { return s.turn.turn_number >= 3; });
    CHECK(in_zone(st.s, kUw, Zone::Battlefield, "Solitude"));
}

TEST_CASE("Consign to Memory: l'esilio del warp neutralizzato lascia la creatura in campo",
          "[uw][consign][warp][starfield][riddler]") {
    SECTION("Starfield Shepherd") {
        Staged st = staged(kUw, {"Starfield Shepherd", "Consign to Memory"}, {},
                           {"Plains", "Plains", "Hallowed Fountain"});
        to_main(st, kUw);
        act(st, cast_action(st.s, "Starfield Shepherd", CastMethod::Warp));
        pay_leaving(st, "Hallowed Fountain");
        resolve(st);
        REQUIRE(is_decision(st.s, DecisionKind::SearchLibrary, kUw));
        act(st, option(kDeclineSearch));
        pass_until(st, at_end_step_with_stack);
        act(st, cast_action(st.s, "Consign to Memory"));
        act(st, stack_trigger(st.s, StackEffect::WarpExile));
        pay_all(st);
        resolve(st);
        CHECK(in_zone(st.s, kUw, Zone::Battlefield, "Starfield Shepherd"));
        pass_until(st, [](const GameState& s) { return s.turn.turn_number >= 3; });  // un'altra sottofase finale
        CHECK(in_zone(st.s, kUw, Zone::Battlefield, "Starfield Shepherd"));
        CHECK(st.s.delayed.empty());
    }
    SECTION("Quantum Riddler") {
        Staged st = staged(kUw, {"Quantum Riddler", "Consign to Memory"}, {},
                           {"Hallowed Fountain", "Hallowed Fountain", "Plains"});
        to_main(st, kUw);
        act(st, cast_action(st.s, "Quantum Riddler", CastMethod::Warp));
        pay_leaving(st, "Hallowed Fountain");
        resolve(st);  // la pescata dell'entrata: UW potrebbe neutralizzarla, non lo fa
        REQUIRE(in_zone(st.s, kUw, Zone::Battlefield, "Quantum Riddler"));
        pass_until(st, at_end_step_with_stack);
        act(st, cast_action(st.s, "Consign to Memory"));
        act(st, stack_trigger(st.s, StackEffect::WarpExile));
        pay_all(st);
        resolve(st);
        CHECK(in_zone(st.s, kUw, Zone::Battlefield, "Quantum Riddler"));
        pass_until(st, [](const GameState& s) { return s.turn.turn_number >= 3; });
        CHECK(in_zone(st.s, kUw, Zone::Battlefield, "Quantum Riddler"));
    }
}

TEST_CASE("Consign to Memory: il trigger di attacco di Fear of Missing Out neutralizzato, niente combattimento in piu'",
          "[uw][consign][fomo]") {
    const bool counter = GENERATE(true, false);
    INFO("Consign to Memory lanciata: " << counter);
    Staged st = staged(kRakdos, {"Consign to Memory"}, {}, {"Hallowed Fountain"}, {"Fear of Missing Out"});
    REQUIRE(is_decision(st.s, DecisionKind::DeclareAttacker, kRakdos));
    to_graveyard(st.s, kRakdos, {"Lightning Bolt", "Bloodstained Mire", "Mishra's Bauble", "Nethergoyf"});  // delirium
    act(st, Action{ActionType::TargetPlayer, kNoCard, kUw});
    REQUIRE(is_decision(st.s, DecisionKind::ChooseTarget, kRakdos));
    act(st, target(st.s, "Fear of Missing Out"));
    REQUIRE(is_decision(st.s, DecisionKind::Priority, kUw));
    REQUIRE(stack_count(st.s, StackEffect::UntapAndExtraCombat) == 1);
    if (counter) {
        act(st, cast_action(st.s, "Consign to Memory"));
        act(st, stack_trigger(st.s, StackEffect::UntapAndExtraCombat));
        pay_all(st);
    }
    // Fino al turno 2; nel combattimento aggiuntivo, se c'è, Fear of Missing Out attacca di nuovo.
    for (int guard = 0; guard < 200 && st.s.turn.turn_number < 2; ++guard) {
        if (st.s.pending.kind == DecisionKind::DeclareAttacker) {
            act(st, Action{ActionType::TargetPlayer, kNoCard, kUw});
        } else {
            REQUIRE(st.s.pending.kind == DecisionKind::Priority);
            act(st, pass());
        }
    }
    REQUIRE(st.s.turn.turn_number == 2);
    CHECK(begin_combats(st.log, 1) == (counter ? 1 : 2));
    CHECK(st.s.players[kUw].life == (counter ? 18 : 16));
}

TEST_CASE("Consign to Memory: la macinata di Stitcher's Supplier, il trigger di Moonshadow, Mishra's Bauble lanciato",
          "[uw][consign]") {
    SECTION("la macinata di Stitcher's Supplier") {
        Staged st = staged(kRakdos, {"Consign to Memory"}, {"Stitcher's Supplier"}, {"Hallowed Fountain"}, {"Swamp"});
        to_main(st, kRakdos);
        const std::size_t library = st.s.players[kRakdos].library.size();
        act(st, cast_action(st.s, "Stitcher's Supplier"));
        pay_all(st);
        // La magia creatura nera non è un bersaglio: UW riceve la priorità solo con il trigger in pila.
        REQUIRE(is_decision(st.s, DecisionKind::Priority, kUw));
        REQUIRE(stack_count(st.s, StackEffect::Mill) == 1);
        act(st, cast_action(st.s, "Consign to Memory"));
        act(st, stack_trigger(st.s, StackEffect::Mill));
        pay_all(st);
        resolve(st);
        CHECK(st.s.players[kRakdos].library.size() == library);
        CHECK(st.s.players[kRakdos].graveyard.empty());
        CHECK(in_zone(st.s, kRakdos, Zone::Battlefield, "Stitcher's Supplier"));
    }
    SECTION("il trigger di Moonshadow; l'abilita' attivata della fetch non e' un bersaglio") {
        Staged st = staged(kRakdos, {"Consign to Memory"}, {}, {"Hallowed Fountain"}, {"Moonshadow", "Bloodstained Mire"});
        st.s.objects[find_on_battlefield(st.s, "Moonshadow").card].counters.minus_one = 6;
        REQUIRE(is_decision(st.s, DecisionKind::Priority, kRakdos));
        act(st, find_action(st.s, ActionType::ActivateAbility, "Bloodstained Mire"));
        REQUIRE(is_decision(st.s, DecisionKind::Priority, kUw));
        REQUIRE(st.s.stack_items.size() == 2);  // l'abilità della fetch e il trigger di Moonshadow
        act(st, cast_action(st.s, "Consign to Memory"));
        CHECK(stack_targets(st.s) == 1);
        act(st, stack_trigger(st.s, StackEffect::RemoveMinusCounter));
        pay_all(st);
        resolve(st);
        REQUIRE(is_decision(st.s, DecisionKind::SearchLibrary, kRakdos));  // la fetch si risolve
        CHECK(st.s.objects[find_on_battlefield(st.s, "Moonshadow").card].counters.minus_one == 6);
    }
    SECTION("Mishra's Bauble lanciato: una magia incolore, va nel cimitero") {
        Staged st = staged(kRakdos, {"Consign to Memory"}, {"Mishra's Bauble"}, {"Hallowed Fountain"});
        to_main(st, kRakdos);
        act(st, cast_action(st.s, "Mishra's Bauble"));
        pay_all(st);
        REQUIRE(is_decision(st.s, DecisionKind::Priority, kUw));
        act(st, cast_action(st.s, "Consign to Memory"));
        CHECK(stack_targets(st.s) == 1);
        act(st, stack_spell(st.s, "Mishra's Bauble"));
        pay_all(st);
        resolve(st);
        CHECK(in_zone(st.s, kRakdos, Zone::Graveyard, "Mishra's Bauble"));  // 701.6a
        CHECK(st.s.stack_items.empty());
        CHECK(st.s.stack.empty());
    }
    SECTION("una magia colorata non e' un bersaglio: non si lancia") {
        Staged st = staged(kRakdos, {"Consign to Memory"}, {"Lightning Bolt"}, {"Hallowed Fountain", "Arid Mesa"},
                           {"Blood Crypt"});
        pass_until(st, [](const GameState& s) { return s.pending.player == kRakdos; });
        act(st, cast_action(st.s, "Lightning Bolt"));
        act(st, Action{ActionType::TargetPlayer, kNoCard, kUw});
        pay_all(st);
        REQUIRE(is_decision(st.s, DecisionKind::Priority, kUw));
        REQUIRE(st.s.stack_items.size() == 1);
        CHECK_FALSE(can_cast(st.s, "Consign to Memory"));
    }
}

TEST_CASE("Consign to Memory: replicata una volta, neutralizza due bersagli diversi", "[uw][consign][replicate]") {
    // Arid Mesa: UW tiene la priorità, così la pila non si risolve da sola tra un controllo e l'altro.
    Staged st = staged(kRakdos, {"Consign to Memory"}, {}, {"Hallowed Fountain", "Hallowed Fountain", "Arid Mesa"},
                       {"Moonshadow", "Moonshadow", "Bloodstained Mire"});
    for (const CardId id : st.s.battlefield) {
        if (name_of(st.s, id) == "Moonshadow") st.s.objects[id].counters.minus_one = 6;
    }
    REQUIRE(is_decision(st.s, DecisionKind::Priority, kRakdos));
    act(st, find_action(st.s, ActionType::ActivateAbility, "Bloodstained Mire"));
    REQUIRE(is_decision(st.s, DecisionKind::Priority, kUw));
    REQUIRE(stack_count(st.s, StackEffect::RemoveMinusCounter) == 2);

    act(st, cast_action(st.s, "Consign to Memory"));
    REQUIRE(is_decision(st.s, DecisionKind::ChooseTarget, kUw));
    CHECK(stack_targets(st.s) == 2);  // i due trigger, non l'abilità della fetch
    act(st, stack_trigger(st.s, StackEffect::RemoveMinusCounter));
    const std::uint16_t first_target = st.s.stack_items.back().target_id;
    // Quante volte pagare {1}: al più una, perché c'è un solo altro bersaglio.
    REQUIRE(is_decision(st.s, DecisionKind::ChooseReplicate, kUw));
    CHECK(option_values(st.s) == std::set<int>{0, 1});
    act(st, option(1));
    pay_all(st);
    CHECK(untapped(st.s, "Hallowed Fountain") == 0);  // {U} più {1}

    // "When you cast this spell": il trigger si risolve e crea la copia, che può cambiare bersaglio.
    REQUIRE(is_decision(st.s, DecisionKind::Priority, kUw));
    REQUIRE(stack_count(st.s, StackEffect::ReplicateCopies) == 1);
    act(st, pass());
    REQUIRE(is_decision(st.s, DecisionKind::ChooseTarget, kUw));
    REQUIRE(st.s.stack_items.back().copy);
    CHECK(st.s.stack_items.back().target_id == first_target);  // la copia nasce con le stesse scelte (707.10)
    CHECK(has_option(st.s, kKeepTarget));
    CHECK(stack_targets(st.s) == 1);  // l'altro trigger
    act(st, stack_trigger(st.s, StackEffect::RemoveMinusCounter));
    CHECK(st.s.stack_items.back().target_id != first_target);
    CHECK(st.s.stack.size() == 1);  // la copia non ha una carta: in zona pila c'è solo l'originale

    resolve(st);
    REQUIRE(is_decision(st.s, DecisionKind::SearchLibrary, kRakdos));
    CHECK(st.s.stack_items.empty());
    for (const CardId id : st.s.battlefield) {
        if (name_of(st.s, id) == "Moonshadow") CHECK(st.s.objects[id].counters.minus_one == 6);
    }
    CHECK(in_zone(st.s, kUw, Zone::Graveyard, "Consign to Memory"));
    CHECK(st.s.players[kUw].graveyard.size() == 1);  // la copia ha smesso di esistere
}

TEST_CASE("Consign to Memory: senza replicare ne neutralizza uno solo; il trigger del replicare e' un bersaglio",
          "[uw][consign][replicate]") {
    Staged st = staged(kRakdos, {"Consign to Memory", "Consign to Memory"}, {},
                       {"Hallowed Fountain", "Hallowed Fountain", "Hallowed Fountain"},
                       {"Moonshadow", "Moonshadow", "Bloodstained Mire"});
    for (const CardId id : st.s.battlefield) {
        if (name_of(st.s, id) == "Moonshadow") st.s.objects[id].counters.minus_one = 6;
    }
    act(st, find_action(st.s, ActionType::ActivateAbility, "Bloodstained Mire"));
    act(st, cast_action(st.s, "Consign to Memory"));
    act(st, stack_trigger(st.s, StackEffect::RemoveMinusCounter));
    REQUIRE(is_decision(st.s, DecisionKind::ChooseReplicate, kUw));
    SECTION("non replicata: un solo trigger neutralizzato") {
        act(st, option(0));
        pay_all(st);
        resolve(st);
        int total = 0;
        for (const CardId id : st.s.battlefield) {
            if (name_of(st.s, id) == "Moonshadow") total += st.s.objects[id].counters.minus_one;
        }
        CHECK(total == 11);
    }
    SECTION("replicata, ma la seconda Consign neutralizza il trigger del replicare: nessuna copia") {
        act(st, option(1));
        pay_all(st);
        REQUIRE(is_decision(st.s, DecisionKind::Priority, kUw));
        REQUIRE(stack_count(st.s, StackEffect::ReplicateCopies) == 1);
        act(st, cast_action(st.s, "Consign to Memory"));
        act(st, stack_trigger(st.s, StackEffect::ReplicateCopies));
        if (st.s.pending.kind == DecisionKind::ChooseReplicate) act(st, option(0));
        pay_all(st);
        resolve(st);
        int total = 0;
        for (const CardId id : st.s.battlefield) {
            if (name_of(st.s, id) == "Moonshadow") total += st.s.objects[id].counters.minus_one;
        }
        CHECK(total == 11);  // solo l'originale ha neutralizzato un trigger
        CHECK(st.s.players[kUw].graveyard.size() == 2);
    }
}

// ---------------------------------------------------------------------------
// Force of Negation — {1}{U}{U} — Instant
//   If it's not your turn, you may exile a blue card from your hand rather than pay
//   this spell's mana cost.
//   Counter target noncreature spell. If that spell is countered this way, exile it
//   instead of putting it into its owner's graveyard.
// CR 702.103b: una magia lanciata conferita è un'Aura, non una magia creatura.
// ---------------------------------------------------------------------------

TEST_CASE("Force of Negation: gratis solo nel turno avversario, esiliando una carta blu dalla mano",
          "[uw][fon]") {
    SECTION("turno di Rakdos: costo alternativo") {
        Staged st = staged(kRakdos, {"Force of Negation", "Consign to Memory"}, {"Thoughtseize"}, {}, {"Swamp"});
        to_main(st, kRakdos);
        act(st, cast_action(st.s, "Thoughtseize"));
        act(st, Action{ActionType::TargetPlayer, kNoCard, kUw});
        pay_all(st);
        REQUIRE(is_decision(st.s, DecisionKind::Priority, kUw));
        CHECK(can_cast(st.s, "Force of Negation", CastMethod::Pitch));
        CHECK_FALSE(can_cast(st.s, "Force of Negation"));  // nessuna terra
        act(st, cast_action(st.s, "Force of Negation", CastMethod::Pitch));
        REQUIRE(is_decision(st.s, DecisionKind::ChooseTarget, kUw));
        CHECK(stack_targets(st.s) == 1);
        act(st, stack_spell(st.s, "Thoughtseize"));  // Consign to Memory è l'unica carta blu: esiliata
        resolve(st);
        CHECK(in_zone(st.s, kUw, Zone::Exile, "Consign to Memory"));
        CHECK(in_zone(st.s, kUw, Zone::Graveyard, "Force of Negation"));
        CHECK(in_zone(st.s, kRakdos, Zone::Exile, "Thoughtseize"));  // esiliata, non nel cimitero
        CHECK(st.s.players[kRakdos].graveyard.empty());
        CHECK(st.s.players[kRakdos].life == 20);  // Thoughtseize non si è risolta
        CHECK(st.s.stack.empty());
    }
    SECTION("turno di UW: solo pagando {1}{U}{U}") {
        Staged st = staged(kUw, {"Force of Negation", "Consign to Memory"}, {"Lightning Bolt"},
                           {"Hallowed Fountain", "Hallowed Fountain", "Plains"}, {"Blood Crypt"});
        REQUIRE(st.s.turn.active_player == kUw);
        REQUIRE(is_decision(st.s, DecisionKind::Priority, kRakdos));
        act(st, cast_action(st.s, "Lightning Bolt"));
        act(st, Action{ActionType::TargetPlayer, kNoCard, kUw});
        pay_all(st);
        REQUIRE(is_decision(st.s, DecisionKind::Priority, kUw));
        CHECK_FALSE(can_cast(st.s, "Force of Negation", CastMethod::Pitch));
        REQUIRE(can_cast(st.s, "Force of Negation"));
        act(st, cast_action(st.s, "Force of Negation"));
        act(st, stack_spell(st.s, "Lightning Bolt"));
        pay_all(st);
        resolve(st);
        CHECK(in_zone(st.s, kRakdos, Zone::Exile, "Lightning Bolt"));
        CHECK(in_zone(st.s, kUw, Zone::Hand, "Consign to Memory"));
        CHECK(st.s.players[kUw].life == 20);
        CHECK(untapped(st.s, "Hallowed Fountain") + untapped(st.s, "Plains") == 0);
    }
    SECTION("senza un'altra carta blu in mano niente costo alternativo") {
        Staged st = staged(kRakdos, {"Force of Negation", "Solitude"}, {"Mishra's Bauble"}, {"Arid Mesa"});
        to_main(st, kRakdos);
        act(st, cast_action(st.s, "Mishra's Bauble"));
        pay_all(st);
        REQUIRE(is_decision(st.s, DecisionKind::Priority, kUw));
        CHECK_FALSE(can_cast(st.s, "Force of Negation", CastMethod::Pitch));
    }
}

TEST_CASE("Force of Negation: non bersaglia una magia creatura, ma una Phoenix conferita si'", "[uw][fon][phoenix]") {
    SECTION("magia creatura: non si lancia") {
        Staged st = staged(kRakdos, {"Force of Negation", "Consign to Memory"}, {"Stitcher's Supplier"}, {"Arid Mesa"},
                           {"Swamp"});
        to_main(st, kRakdos);
        act(st, cast_action(st.s, "Stitcher's Supplier"));
        pay_all(st);
        REQUIRE(is_decision(st.s, DecisionKind::Priority, kUw));
        REQUIRE(st.s.stack_items.size() == 1);
        CHECK_FALSE(can_cast(st.s, "Force of Negation", CastMethod::Pitch));
    }
    SECTION("Detective's Phoenix conferita e' una magia Aura: neutralizzata ed esiliata") {
        Staged st{new_game(1), {}};
        put(st.s, kUw, {"Force of Negation", "Consign to Memory"}, Zone::Hand);
        put(st.s, kRakdos, {"Detective's Phoenix"}, Zone::Hand);
        put(st.s, kRakdos, {"Blood Crypt", "Nethergoyf"}, Zone::Battlefield);
        put(st.s, kRakdos, {"Troll of Khazad-dûm"}, Zone::Graveyard);  // prove: valore di mana 6
        start_game(st.s, db_with_tokens(), StartOptions{kRakdos, false}, &st.log);
        require_invariants(st.s);
        to_main(st, kRakdos);
        act(st, cast_action(st.s, "Detective's Phoenix", CastMethod::Bestow));
        act(st, target(st.s, "Nethergoyf"));
        pay_all(st);
        REQUIRE(is_decision(st.s, DecisionKind::Priority, kUw));
        REQUIRE(can_cast(st.s, "Force of Negation", CastMethod::Pitch));
        act(st, cast_action(st.s, "Force of Negation", CastMethod::Pitch));
        act(st, stack_spell(st.s, "Detective's Phoenix"));
        resolve(st);
        CHECK(in_zone(st.s, kRakdos, Zone::Exile, "Detective's Phoenix"));
        CHECK(st.s.stack.empty());
    }
}

TEST_CASE("Force of Negation: neutralizza una copia di Consign to Memory, che smette di esistere",
          "[uw][fon][consign][replicate]") {
    // Mirror di UW. Il giocatore 1 evoca Solitude; il giocatore 0 replica Consign to Memory sui due
    // trigger; il giocatore 1 neutralizza la copia con Force of Negation, pagandola (è il suo turno).
    Staged st{create_game(db_with_tokens(), uw_sim_deck(), uw_sim_deck(), 1), {}};
    put(st.s, 0, {"Consign to Memory"}, Zone::Hand);
    put(st.s, 0, {"Hallowed Fountain", "Hallowed Fountain", "Ocelot Pride"}, Zone::Battlefield);
    put(st.s, 1, {"Solitude", "Ocelot Pride", "Force of Negation"}, Zone::Hand);
    put(st.s, 1, {"Hallowed Fountain", "Hallowed Fountain", "Hallowed Fountain"}, Zone::Battlefield);
    start_game(st.s, db_with_tokens(), StartOptions{1, false}, &st.log);
    require_invariants(st.s);
    to_main(st, 1);
    act(st, cast_action(st.s, "Solitude", CastMethod::Evoke));
    put_first(st, StackEffect::ExileOtherCreature);
    act(st, target(st.s, "Ocelot Pride"));  // quella del giocatore 0: l'altra è stata esiliata come costo
    REQUIRE(is_decision(st.s, DecisionKind::Priority, 0));
    act(st, cast_action(st.s, "Consign to Memory"));
    act(st, stack_trigger(st.s, StackEffect::ExileOtherCreature));
    REQUIRE(is_decision(st.s, DecisionKind::ChooseReplicate, 0));
    act(st, option(1));
    pay_all(st);
    // Il giocatore 1 potrebbe già neutralizzare l'originale: aspetta la copia.
    REQUIRE(is_decision(st.s, DecisionKind::Priority, 1));
    CHECK(can_cast(st.s, "Force of Negation"));
    CHECK_FALSE(can_cast(st.s, "Force of Negation", CastMethod::Pitch));  // è il suo turno
    act(st, pass());
    REQUIRE(is_decision(st.s, DecisionKind::ChooseTarget, 0));
    act(st, stack_trigger(st.s, StackEffect::EvokeSacrifice));
    REQUIRE(is_decision(st.s, DecisionKind::Priority, 1));
    act(st, cast_action(st.s, "Force of Negation"));
    CHECK(stack_targets(st.s) == 2);  // l'originale e la copia
    act(st, stack_copy(st.s));
    pay_all(st);
    resolve(st);
    CHECK(st.s.stack_items.empty());
    CHECK(st.s.exile.size() == 1);                               // solo la carta esiliata per l'evocazione
    CHECK(in_zone(st.s, 1, Zone::Graveyard, "Solitude"));        // il sacrificio si è risolto
    CHECK(in_zone(st.s, 0, Zone::Battlefield, "Ocelot Pride"));  // l'esilio l'ha neutralizzato l'originale
    CHECK(in_zone(st.s, 0, Zone::Graveyard, "Consign to Memory"));
    CHECK(in_zone(st.s, 1, Zone::Graveyard, "Force of Negation"));
}
