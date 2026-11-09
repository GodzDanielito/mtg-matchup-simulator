// Test delle prime carte di UW Blink: Ocelot Pride, Guide of Souls, Witch Enchanter,
// Recruiter of the Guard, Starfield Shepherd e Sanctifier en-Vec.
// Testo Oracle da Scryfall (bulk oracle_cards aggiornato il 9/11/2026, 10:02 UTC),
// riportato prima dei test di ogni carta. Regole: Comprehensive Rules in vigore dal
// 6/11/2026 (ascesa 702.131, warp 702.185, "if" interposto 603.4, trigger riflessivi
// 603.12, sostituzioni 614, energia 107.14, ricerca 701.23, copie 707.2).

#include "test_helpers.hpp"

#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

using namespace mtg;
using namespace test;

namespace {

constexpr PlayerId kUw = 0;
constexpr PlayerId kRakdos = 1;

std::string name_of(const GameState& s, CardId id) { return db_with_tokens().get(s.objects[id].def).name; }

// Passa la priorità, non attacca e non blocca, finché la condizione non è vera. Qualunque
// altra decisione (un pagamento, una ricerca) è un errore del test.
template <typename Pred>
void pass_until(Staged& st, Pred done) {
    for (int guard = 0; guard < 500 && !done(st.s); ++guard) {
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

// Fino alla priorità del giocatore nella sua prima fase principale.
void to_main(Staged& st, PlayerId p) {
    pass_until(st, [p](const GameState& s) {
        return s.turn.active_player == p && s.turn.step == Step::Main1 && s.pending.kind == DecisionKind::Priority &&
               s.pending.player == p;
    });
}

void pay_all(Staged& st) {
    while (st.s.pending.kind == DecisionKind::PayMana) act(st, option(0));
}

// Lancia la carta dalla mano (o dall'esilio) con il metodo indicato e paga il mana.
void cast(Staged& st, const char* name, CastMethod method = CastMethod::Normal) {
    act(st, find_action(st.s, ActionType::CastSpell, name, static_cast<std::uint8_t>(method)));
    pay_all(st);
}

// Passa la priorità finché la pila non è vuota; si ferma a qualunque altra decisione.
void resolve(Staged& st) {
    for (int guard = 0; guard < 50 && !st.s.stack_items.empty() && st.s.pending.kind == DecisionKind::Priority;
         ++guard) {
        act(st, pass());
    }
}

int count_tokens(const GameState& s, PlayerId p) {
    return static_cast<int>(std::count_if(s.battlefield.begin(), s.battlefield.end(), [&](CardId id) {
        return s.objects[id].has_flag(ObjectFlag::Token) && s.objects[id].controller == p;
    }));
}

int stack_count(const GameState& s, StackEffect e) {
    return static_cast<int>(std::count_if(s.stack_items.begin(), s.stack_items.end(),
                                          [&](const StackItem& i) { return i.effect == e; }));
}

std::set<std::string> card_choices(const GameState& s) {
    std::set<std::string> out;
    for (const Action& a : legal(s)) {
        if (a.type == ActionType::ChooseCard) out.insert(name_of(s, a.card));
    }
    return out;
}

std::set<std::string> target_choices(const GameState& s) {
    std::set<std::string> out;
    for (const Action& a : legal(s)) {
        if (a.type == ActionType::TargetObject) out.insert(name_of(s, a.card));
    }
    return out;
}

bool has_option(const GameState& s, std::uint8_t value) {
    const auto actions = legal(s);
    return std::find(actions.begin(), actions.end(), option(value)) != actions.end();
}

bool can_cast(const GameState& s, const char* name, CastMethod method) {
    for (const Action& a : legal(s)) {
        if (a.type == ActionType::CastSpell && a.value == static_cast<std::uint8_t>(method) && name_of(s, a.card) == name) {
            return true;
        }
    }
    return false;
}

bool in_zone(const GameState& s, PlayerId owner, Zone z, const char* name) {
    for (std::size_t id = 0; id < kMaxCards; ++id) {
        const CardObject& o = s.objects[id];
        if (o.owner == owner && o.zone == z && db_with_tokens().get(o.def).name == name) return true;
    }
    return false;
}

void to_graveyard(GameState& s, PlayerId p, std::initializer_list<const char*> names) {
    for (const char* name : names) {
        move_object(s, db_with_tokens(), find_cards(s, p, Zone::Library, name)[0], Zone::Graveyard, Cause::Test);
    }
    require_invariants(s);
}

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

void to_declare_attackers(Staged& st) {
    pass_until(st, [](const GameState& s) {
        return s.pending.kind == DecisionKind::DeclareAttacker ||
               (s.turn.step == Step::DeclareAttackers && s.pending.kind == DecisionKind::Priority);
    });
}

void attack_with(Staged& st, std::initializer_list<std::string> names) {
    to_declare_attackers(st);
    while (st.s.pending.kind == DecisionKind::DeclareAttacker) {
        const std::string who = name_of(st.s, st.s.pending.subject.card);
        const bool attacks = std::find(names.begin(), names.end(), who) != names.end();
        const PlayerId defender = static_cast<PlayerId>(1 - st.s.turn.active_player);
        act(st, attacks ? Action{ActionType::TargetPlayer, kNoCard, defender} : option(kNoAttack));
    }
}

bool at_end_step_with_stack(const GameState& s) {
    return s.turn.step == Step::End && !s.stack_items.empty() && s.pending.kind == DecisionKind::Priority;
}

}  // namespace

// ---------------------------------------------------------------------------
// Ocelot Pride — {W} — Creature — Cat 1/1
//   First strike, lifelink
//   Ascend (If you control ten or more permanents, you get the city's blessing for
//   the rest of the game.)
//   At the beginning of your end step, if you gained life this turn, create a 1/1
//   white Cat creature token. Then if you have the city's blessing, for each token
//   you control that entered this turn, create a token that's a copy of it.
// Arid Mesa in campo serve solo a dare a UW un'opzione oltre a "passa", così la
// priorità si ferma e si vede il trigger in pila.
// ---------------------------------------------------------------------------

TEST_CASE("Ocelot Pride: alla propria sottofase finale, se hai guadagnato vita, un gatto 1/1 bianco",
          "[uw][ocelot]") {
    Staged st = staged(kUw, {}, {}, {"Ocelot Pride", "Arid Mesa"});
    gain_life(st.s, kUw, 2);
    pass_until(st, at_end_step_with_stack);
    CHECK(stack_count(st.s, StackEffect::EndStepCats) == 1);
    resolve(st);
    REQUIRE(count_tokens(st.s, kUw) == 1);
    const CardId cat = st.s.battlefield.back();
    const CardDef& d = effective_def(st.s, db_with_tokens(), cat);
    CHECK(d.name == "Cat Token");
    CHECK(d.has_type(CardType::Creature));
    CHECK(d.colors == color_bit(Color::White));
    CHECK(power(st.s, db_with_tokens(), cat) == 1);
    CHECK(toughness(st.s, db_with_tokens(), cat) == 1);
    CHECK(st.s.objects[cat].controller == kUw);
}

TEST_CASE("Ocelot Pride: senza vita guadagnata nel turno, nessun gatto", "[uw][ocelot]") {
    Staged st = staged(kUw, {}, {}, {"Ocelot Pride", "Arid Mesa"});
    pass_until(st, [](const GameState& s) { return s.turn.step == Step::End; });
    CHECK(st.s.stack_items.empty());
    pass_until(st, [](const GameState& s) { return s.turn.active_player == kRakdos; });
    CHECK(count_tokens(st.s, kUw) == 0);
}

TEST_CASE("Ocelot Pride: uccisa con il trigger gia' in pila, il gatto arriva lo stesso", "[uw][ocelot]") {
    Staged st = staged(kUw, {}, {}, {"Ocelot Pride", "Arid Mesa"});
    gain_life(st.s, kUw, 1);
    pass_until(st, at_end_step_with_stack);
    move_object(st.s, db_with_tokens(), find_on_battlefield(st.s, "Ocelot Pride"), Zone::Graveyard, Cause::Test);
    require_invariants(st.s);
    resolve(st);
    CHECK(count_tokens(st.s, kUw) == 1);
}

TEST_CASE("Ocelot Pride: non scatta nella sottofase finale dell'avversario", "[uw][ocelot]") {
    Staged st = staged(kRakdos, {}, {}, {"Ocelot Pride", "Arid Mesa"});
    gain_life(st.s, kUw, 3);  // vita guadagnata nel turno di Rakdos
    pass_until(st, [](const GameState& s) { return s.turn.step == Step::End; });
    CHECK(st.s.stack_items.empty());
    CHECK(st.s.pending_triggers.empty());
    // Nel turno dopo (di UW) la vita guadagnata nel turno di Rakdos non conta più.
    pass_until(st, [](const GameState& s) { return s.turn.active_player == kUw && s.turn.step == Step::End; });
    CHECK(st.s.stack_items.empty());
    CHECK(count_tokens(st.s, kUw) == 0);
}

TEST_CASE("Ocelot Pride: ascesa, con dieci permanenti la benedizione della citta' per il resto della partita",
          "[uw][ocelot]") {
    SECTION("dieci permanenti con Ocelot Pride: benedizione, e resta anche scendendo sotto i dieci") {
        Staged st = staged(kUw, {}, {},
                           {"Ocelot Pride", "Plains", "Plains", "Plains", "Hallowed Fountain", "Hallowed Fountain",
                            "Hallowed Fountain", "Hallowed Fountain", "Meticulous Archive", "Arid Mesa"});
        CHECK(st.s.players[kUw].city_blessing);
        CHECK_FALSE(st.s.players[kRakdos].city_blessing);
        move_object(st.s, db_with_tokens(), find_on_battlefield(st.s, "Plains"), Zone::Graveyard, Cause::Test);
        act(st, pass());
        CHECK(st.s.players[kUw].city_blessing);
    }
    SECTION("nove permanenti: niente benedizione") {
        Staged st = staged(kUw, {}, {},
                           {"Ocelot Pride", "Plains", "Plains", "Plains", "Hallowed Fountain", "Hallowed Fountain",
                            "Hallowed Fountain", "Hallowed Fountain", "Arid Mesa"});
        CHECK_FALSE(st.s.players[kUw].city_blessing);
    }
    SECTION("dieci permanenti senza un permanente con ascesa: niente benedizione") {
        Staged st = staged(kUw, {}, {},
                           {"Plains", "Plains", "Plains", "Hallowed Fountain", "Hallowed Fountain", "Hallowed Fountain",
                            "Hallowed Fountain", "Meticulous Archive", "Meticulous Archive", "Arid Mesa"});
        CHECK_FALSE(st.s.players[kUw].city_blessing);
    }
}

TEST_CASE("Ocelot Pride: con la benedizione, una copia di ogni token entrato in questo turno", "[uw][ocelot]") {
    Staged st{new_game(1), {}};
    put(st.s, kUw,
        {"Ocelot Pride", "Plains", "Plains", "Plains", "Hallowed Fountain", "Hallowed Fountain", "Hallowed Fountain",
         "Hallowed Fountain", "Arid Mesa"},
        Zone::Battlefield);
    // Un gatto entrato prima di questo turno: non si copia.
    create_token(st.s, db_with_tokens(), db_with_tokens().id_of("Cat Token"), kUw);
    start_game(st.s, db_with_tokens(), StartOptions{kUw, false}, &st.log);
    REQUIRE(st.s.players[kUw].city_blessing);  // 10 permanenti, gatto compreso
    gain_life(st.s, kUw, 1);
    pass_until(st, at_end_step_with_stack);
    resolve(st);
    // Il gatto nuovo e la sua copia; il vecchio resta da solo.
    CHECK(count_tokens(st.s, kUw) == 3);
    int entered_this_turn = 0;
    for (const CardId id : st.s.battlefield) {
        const CardObject& o = st.s.objects[id];
        if (o.has_flag(ObjectFlag::Token) && o.zone_turn == st.s.turn.turn_number) ++entered_this_turn;
    }
    CHECK(entered_this_turn == 2);
}

TEST_CASE("Ocelot Pride: il gatto porta a dieci permanenti, e la benedizione arriva in tempo per la copia",
          "[uw][ocelot]") {
    // 702.131b: la benedizione arriva "in qualsiasi momento" si controllano dieci permanenti,
    // anche a meta' della risoluzione: il "Then if you have the city's blessing" la vede.
    Staged st = staged(kUw, {}, {},
                       {"Ocelot Pride", "Plains", "Plains", "Plains", "Hallowed Fountain", "Hallowed Fountain",
                        "Hallowed Fountain", "Hallowed Fountain", "Arid Mesa"});
    REQUIRE_FALSE(st.s.players[kUw].city_blessing);  // 9 permanenti
    gain_life(st.s, kUw, 1);
    pass_until(st, at_end_step_with_stack);
    resolve(st);
    CHECK(st.s.players[kUw].city_blessing);
    CHECK(count_tokens(st.s, kUw) == 2);
}

// ---------------------------------------------------------------------------
// Guide of Souls — {W} — Creature — Human Cleric 1/2
//   Whenever another creature you control enters, you gain 1 life and get {E} (an
//   energy counter).
//   Whenever you attack, you may pay {E}{E}{E}. When you do, put two +1/+1 counters
//   and a flying counter on target attacking creature. It becomes an Angel in
//   addition to its other types.
// ---------------------------------------------------------------------------

TEST_CASE("Guide of Souls: un'altra creatura che entra sotto il tuo controllo da' 1 vita e 1 energia",
          "[uw][guide]") {
    Staged st = staged(kUw, {"Ocelot Pride"}, {}, {"Guide of Souls", "Plains"});
    cast(st, "Ocelot Pride");
    resolve(st);
    CHECK(st.s.players[kUw].life == 21);
    CHECK(st.s.players[kUw].energy == 1);
    CHECK(st.s.players[kUw].gained_life_this_turn);
}

TEST_CASE("Guide of Souls: non scatta per se stessa, per le terre ne' per le creature avversarie", "[uw][guide]") {
    SECTION("Guide of Souls che entra") {
        Staged st = staged(kUw, {"Guide of Souls"}, {}, {"Plains"});
        cast(st, "Guide of Souls");
        resolve(st);
        CHECK(in_zone(st.s, kUw, Zone::Battlefield, "Guide of Souls"));
        CHECK(st.s.players[kUw].life == 20);
        CHECK(st.s.players[kUw].energy == 0);
    }
    SECTION("una creatura di Rakdos che entra") {
        Staged st = staged(kRakdos, {}, {"Stitcher's Supplier"}, {"Guide of Souls"}, {"Swamp"});
        cast(st, "Stitcher's Supplier");
        resolve(st);
        CHECK(in_zone(st.s, kRakdos, Zone::Battlefield, "Stitcher's Supplier"));
        CHECK(st.s.players[kUw].energy == 0);
        CHECK(st.s.players[kUw].life == 20);
    }
    SECTION("una terra giocata") {
        Staged st = staged(kUw, {"Plains"}, {}, {"Guide of Souls"});
        act(st, find_action(st.s, ActionType::PlayLand, "Plains"));
        CHECK(st.s.players[kUw].energy == 0);
    }
}

TEST_CASE("Guide of Souls: ogni gatto di Ocelot Pride la fa scattare", "[uw][guide][ocelot]") {
    Staged st = staged(kUw, {}, {}, {"Ocelot Pride", "Guide of Souls", "Arid Mesa"});
    gain_life(st.s, kUw, 1);
    pass_until(st, at_end_step_with_stack);
    resolve(st);
    CHECK(count_tokens(st.s, kUw) == 1);
    CHECK(st.s.players[kUw].life == 22);
    CHECK(st.s.players[kUw].energy == 1);
}

// Bug trovato dal test di robustezza (passo 13): con la benedizione della città entrano
// insieme decine di gatti, e Guide of Souls scatta una volta per ognuno.
TEST_CASE("Guide of Souls: ventidue gatti che entrano, ventidue vite e ventidue energie", "[uw][guide][ocelot]") {
    Staged st = staged(kUw, {}, {},
                       {"Ocelot Pride", "Guide of Souls", "Arid Mesa", "Plains", "Plains", "Plains",
                        "Hallowed Fountain", "Hallowed Fountain", "Hallowed Fountain", "Hallowed Fountain"});
    REQUIRE(st.s.players[kUw].city_blessing);
    // Venti gatti entrati in questo turno (senza trigger: preparazione del test).
    for (int i = 0; i < 20; ++i) create_token(st.s, db_with_tokens(), db_with_tokens().id_of("Cat Token"), kUw);
    require_invariants(st.s);
    gain_life(st.s, kUw, 1);
    pass_until(st, at_end_step_with_stack);
    resolve(st);
    // Un gatto nuovo, poi una copia di ognuno dei 21 token entrati nel turno.
    CHECK(count_tokens(st.s, kUw) == 20 + 1 + 21);
    CHECK(st.s.players[kUw].energy == 22);
    CHECK(st.s.players[kUw].life == 21 + 22);
}

TEST_CASE("Guide of Souls: attaccando, tre energie per due segnalini +1/+1, un segnalino volare e il tipo Angelo",
          "[uw][guide]") {
    Staged st = staged(kUw, {}, {}, {"Guide of Souls", "Ocelot Pride", "Arid Mesa"});
    st.s.players[kUw].energy = 3;
    attack_with(st, {"Ocelot Pride"});
    CHECK(stack_count(st.s, StackEffect::EnergyAttack) == 1);
    pass_until(st, [](const GameState& s) { return s.pending.kind == DecisionKind::PayOptionalCost; });
    CHECK(st.s.pending.player == kUw);
    CHECK(st.s.pending.amount == 3);
    act(st, option(kPayCost));
    CHECK(st.s.players[kUw].energy == 0);
    // Trigger riflessivo (603.12): il bersaglio si sceglie quando va in pila.
    REQUIRE(is_decision(st.s, DecisionKind::ChooseTarget, kUw));
    CHECK(target_choices(st.s) == std::set<std::string>{"Ocelot Pride"});  // solo le creature attaccanti
    act(st, Action{ActionType::TargetObject, find_on_battlefield(st.s, "Ocelot Pride").card, 0});
    resolve(st);
    const CardId ocelot = find_on_battlefield(st.s, "Ocelot Pride").card;
    const CardObject& o = st.s.objects[ocelot];
    CHECK(o.counters.plus_one == 2);
    CHECK(o.counters.keywords[static_cast<std::size_t>(KeywordCounter::Flying)] == 1);
    CHECK(o.has_flag(ObjectFlag::Angel));
    CHECK(power(st.s, db_with_tokens(), ocelot) == 3);
    CHECK(toughness(st.s, db_with_tokens(), ocelot) == 3);
    CHECK(has_keyword(st.s, db_with_tokens(), ocelot, Keyword::Flying));
}

TEST_CASE("Guide of Souls: si puo' non pagare, e con meno di tre energie non si chiede nulla", "[uw][guide]") {
    SECTION("tre energie, non paga") {
        Staged st = staged(kUw, {}, {}, {"Guide of Souls", "Ocelot Pride", "Arid Mesa"});
        st.s.players[kUw].energy = 3;
        attack_with(st, {"Ocelot Pride"});
        pass_until(st, [](const GameState& s) { return s.pending.kind == DecisionKind::PayOptionalCost; });
        act(st, option(kDeclineCost));
        CHECK(st.s.players[kUw].energy == 3);
        CHECK(stack_count(st.s, StackEffect::AttackCounters) == 0);
        CHECK(st.s.pending_triggers.empty());
        CHECK(st.s.objects[find_on_battlefield(st.s, "Ocelot Pride").card].counters.plus_one == 0);
    }
    SECTION("due energie: il trigger si risolve senza decisioni") {
        Staged st = staged(kUw, {}, {}, {"Guide of Souls", "Ocelot Pride", "Arid Mesa"});
        st.s.players[kUw].energy = 2;
        attack_with(st, {"Ocelot Pride"});
        // pass_until richiede una decisione di priorità a ogni passo: un pagamento chiesto fallirebbe.
        pass_until(st, [](const GameState& s) { return s.turn.step == Step::EndCombat; });
        CHECK(st.s.players[kUw].energy == 2);
        CHECK(st.s.objects[find_on_battlefield(st.s, "Ocelot Pride").card].counters.plus_one == 0);
    }
}

TEST_CASE("Guide of Souls: un solo trigger per attacco, qualunque sia il numero di attaccanti", "[uw][guide]") {
    Staged st = staged(kUw, {}, {}, {"Guide of Souls", "Ocelot Pride", "Arid Mesa"});
    st.s.players[kUw].energy = 4;
    attack_with(st, {"Ocelot Pride", "Guide of Souls"});
    CHECK(stack_count(st.s, StackEffect::EnergyAttack) == 1);
    pass_until(st, [](const GameState& s) { return s.pending.kind == DecisionKind::PayOptionalCost; });
    act(st, option(kPayCost));
    CHECK(st.s.players[kUw].energy == 1);
    CHECK(target_choices(st.s) == std::set<std::string>{"Ocelot Pride", "Guide of Souls"});
}

// ---------------------------------------------------------------------------
// Witch Enchanter // Witch-Blessed Meadow (modal_dfc)
//   Fronte — {3}{W} — Creature — Human Warlock 2/2:
//     When this creature enters, destroy target artifact or enchantment an
//     opponent controls.
//   Retro — Land:
//     As this land enters, you may pay 3 life. If you don't, it enters tapped.
//     {T}: Add {W}.
// Il retro ha già i suoi test in test_lands.cpp.
// ---------------------------------------------------------------------------

TEST_CASE("Witch Enchanter: il fronte si lancia, il retro resta giocabile come terra", "[uw][witch]") {
    Staged st = staged(kUw, {"Witch Enchanter"}, {}, {"Plains", "Plains", "Plains", "Hallowed Fountain"});
    CHECK(can_cast(st.s, "Witch Enchanter", CastMethod::Normal));
    CHECK(find_action(st.s, ActionType::PlayLand, "Witch Enchanter", 1).card != kNoCard);
}

TEST_CASE("Witch Enchanter: entrando distrugge un artefatto o un incantesimo dell'avversario", "[uw][witch]") {
    Staged st = staged(kUw, {"Witch Enchanter"}, {}, {"Plains", "Plains", "Plains", "Hallowed Fountain"},
                       {"Mishra's Bauble", "Detective's Phoenix", "Fear of Missing Out", "Nethergoyf"});
    to_main(st, kUw);
    cast(st, "Witch Enchanter");
    resolve(st);
    REQUIRE(is_decision(st.s, DecisionKind::ChooseTarget, kUw));
    // Phoenix e Fear of Missing Out sono creature incantesimo; Nethergoyf è solo una creatura.
    CHECK(target_choices(st.s) == std::set<std::string>{"Mishra's Bauble", "Detective's Phoenix", "Fear of Missing Out"});
    act(st, Action{ActionType::TargetObject, find_on_battlefield(st.s, "Detective's Phoenix").card, 0});
    resolve(st);
    CHECK(in_zone(st.s, kRakdos, Zone::Graveyard, "Detective's Phoenix"));
    CHECK(in_zone(st.s, kRakdos, Zone::Battlefield, "Mishra's Bauble"));
    CHECK(in_zone(st.s, kUw, Zone::Battlefield, "Witch Enchanter"));
}

TEST_CASE("Witch Enchanter: solo permanenti controllati dall'avversario; senza bersagli il trigger non fa nulla",
          "[uw][witch]") {
    Staged st = staged(kUw, {"Witch Enchanter"}, {}, {"Plains", "Plains", "Plains", "Hallowed Fountain"},
                       {"Mishra's Bauble", "Nethergoyf"});
    // La Bauble di Rakdos passa sotto il controllo di UW: non è più un bersaglio.
    st.s.objects[find_on_battlefield(st.s, "Mishra's Bauble").card].controller = kUw;
    to_main(st, kUw);
    cast(st, "Witch Enchanter");
    resolve(st);
    CHECK(st.s.pending.kind != DecisionKind::ChooseTarget);
    CHECK(st.s.stack_items.empty());
    CHECK(in_zone(st.s, kRakdos, Zone::Battlefield, "Mishra's Bauble"));
}

// ---------------------------------------------------------------------------
// Recruiter of the Guard — {2}{W} — Creature — Human Soldier 1/1
//   When this creature enters, you may search your library for a creature card with
//   toughness 2 or less, reveal it, put it into your hand, then shuffle.
// ---------------------------------------------------------------------------

TEST_CASE("Recruiter of the Guard: cerca una creatura con costituzione 2 o meno, la rivela e la mette in mano",
          "[uw][recruiter]") {
    Staged st = staged(kUw, {"Recruiter of the Guard"}, {}, {"Plains", "Plains", "Plains"});
    cast(st, "Recruiter of the Guard");
    resolve(st);
    REQUIRE(is_decision(st.s, DecisionKind::SearchLibrary, kUw));
    // Quantum Riddler (4/6) no; Solitude e Starfield Shepherd (3/2) sì; Witch Enchanter nel
    // grimorio ha le caratteristiche del fronte (2/2).
    CHECK(card_choices(st.s) == std::set<std::string>{"Guide of Souls", "Ocelot Pride", "Phelia, Exuberant Shepherd",
                                                      "Sanctifier en-Vec", "Solitude", "Starfield Shepherd",
                                                      "Witch Enchanter"});
    CHECK(has_option(st.s, kDeclineSearch));
    CHECK(has_option(st.s, kSkipSearch));  // "you may search"
    const std::size_t library = st.s.players[kUw].library.size();
    Action pick{};
    for (const Action& a : legal(st.s)) {
        if (a.type == ActionType::ChooseCard && name_of(st.s, a.card) == "Solitude") pick = a;
    }
    act(st, pick);
    CHECK(st.s.objects[pick.card].zone == Zone::Hand);
    CHECK(st.s.objects[pick.card].known_to == 0b11);  // rivelata
    CHECK(st.s.players[kUw].library.size() == library - 1);
}

TEST_CASE("Recruiter of the Guard: non cercare non mescola; cercare senza trovare mescola", "[uw][recruiter]") {
    Staged st = staged(kUw, {"Recruiter of the Guard"}, {}, {"Plains", "Plains", "Plains"});
    // UW conosce la carta in cima: il mescolamento lo fa dimenticare.
    const CardId top = st.s.players[kUw].library.back();
    st.s.objects[top].known_to = 0b01;
    const std::vector<CardId> before(st.s.players[kUw].library.begin(), st.s.players[kUw].library.end());
    cast(st, "Recruiter of the Guard");
    resolve(st);
    REQUIRE(is_decision(st.s, DecisionKind::SearchLibrary, kUw));
    SECTION("non cerca") {
        act(st, option(kSkipSearch));
        CHECK(std::vector<CardId>(st.s.players[kUw].library.begin(), st.s.players[kUw].library.end()) == before);
        CHECK(st.s.objects[top].known_to == 0b01);
    }
    SECTION("cerca e non trova nulla") {
        act(st, option(kDeclineSearch));
        CHECK(st.s.players[kUw].library.size() == before.size());
        CHECK(st.s.objects[top].known_to == 0);
    }
}

// ---------------------------------------------------------------------------
// Starfield Shepherd — {3}{W}{W} — Creature — Angel 3/2
//   Flying
//   When this creature enters, search your library for a basic Plains card or a
//   creature card with mana value 1 or less, reveal it, put it into your hand, then
//   shuffle.
//   Warp {1}{W} (You may cast this card from your hand for its warp cost. Exile this
//   creature at the beginning of the next end step, then you may cast it from exile
//   on a later turn.)
// CR 702.185a: se il costo di warp è stato pagato, il permanente si esilia all'inizio
// della prossima sottofase finale, e il proprietario può lanciarlo dopo la fine del
// turno corrente finché resta esiliato. Il warp si paga solo lanciando dalla mano.
// ---------------------------------------------------------------------------

TEST_CASE("Starfield Shepherd: entrando cerca una Plains base o una creatura di valore di mana 1 o meno",
          "[uw][starfield]") {
    Staged st = staged(kUw, {"Starfield Shepherd"}, {},
                       {"Hallowed Fountain", "Hallowed Fountain", "Hallowed Fountain", "Hallowed Fountain",
                        "Meticulous Archive"});
    cast(st, "Starfield Shepherd");
    resolve(st);
    REQUIRE(is_decision(st.s, DecisionKind::SearchLibrary, kUw));
    // Hallowed Fountain e Meticulous Archive sono Plains, ma non base.
    CHECK(card_choices(st.s) == std::set<std::string>{"Plains", "Ocelot Pride", "Guide of Souls"});
    CHECK(has_option(st.s, kDeclineSearch));
    CHECK_FALSE(has_option(st.s, kSkipSearch));  // la ricerca non è facoltativa
    Action pick{};
    for (const Action& a : legal(st.s)) {
        if (a.type == ActionType::ChooseCard && name_of(st.s, a.card) == "Plains") pick = a;
    }
    act(st, pick);
    CHECK(st.s.objects[pick.card].zone == Zone::Hand);
    CHECK(st.s.objects[pick.card].known_to == 0b11);
}

TEST_CASE("Starfield Shepherd: warp dalla mano, esilio alla sottofase finale, rilancio dall'esilio in un turno dopo",
          "[uw][starfield][warp]") {
    Staged st = staged(kUw, {"Starfield Shepherd"}, {}, {"Plains", "Hallowed Fountain", "Arid Mesa"});
    to_main(st, kUw);
    CHECK(can_cast(st.s, "Starfield Shepherd", CastMethod::Warp));
    CHECK_FALSE(can_cast(st.s, "Starfield Shepherd", CastMethod::Normal));  // due mana soli
    cast(st, "Starfield Shepherd", CastMethod::Warp);
    resolve(st);
    REQUIRE(is_decision(st.s, DecisionKind::SearchLibrary, kUw));
    act(st, option(kDeclineSearch));
    REQUIRE(in_zone(st.s, kUw, Zone::Battlefield, "Starfield Shepherd"));

    pass_until(st, at_end_step_with_stack);
    CHECK(stack_count(st.s, StackEffect::WarpExile) == 1);
    resolve(st);
    REQUIRE(in_zone(st.s, kUw, Zone::Exile, "Starfield Shepherd"));
    CardId shepherd = kNoCard;
    for (const CardId id : st.s.exile) {
        if (name_of(st.s, id) == "Starfield Shepherd") shepherd = id;
    }
    CHECK(st.s.objects[shepherd].has_flag(ObjectFlag::WarpExiled));

    // Turno successivo di UW: con due mana non si lancia (dall'esilio niente warp).
    pass_until(st, [](const GameState& s) { return s.turn.active_player == kUw && s.turn.step == Step::Main1; });
    CHECK_FALSE(can_cast(st.s, "Starfield Shepherd", CastMethod::Warp));
    CHECK_FALSE(can_cast(st.s, "Starfield Shepherd", CastMethod::Normal));
    // Tre terre bianche qualsiasi dal grimorio (alcune possono essere già state pescate).
    for (int added = 0; added < 3;) {
        bool moved = false;
        for (const CardId id : st.s.players[kUw].library) {
            const std::string n = name_of(st.s, id);
            if (n == "Plains" || n == "Hallowed Fountain" || n == "Meticulous Archive") {
                move_object(st.s, db_with_tokens(), st.s.ref(id), Zone::Battlefield, Cause::Test);
                moved = true;
                break;
            }
        }
        REQUIRE(moved);
        ++added;
    }
    require_invariants(st.s);
    REQUIRE(can_cast(st.s, "Starfield Shepherd", CastMethod::Normal));  // cinque mana, dall'esilio
    CHECK_FALSE(can_cast(st.s, "Starfield Shepherd", CastMethod::Warp));
    cast(st, "Starfield Shepherd");
    resolve(st);
    REQUIRE(is_decision(st.s, DecisionKind::SearchLibrary, kUw));  // entra di nuovo: cerca di nuovo
    act(st, option(kDeclineSearch));
    REQUIRE(in_zone(st.s, kUw, Zone::Battlefield, "Starfield Shepherd"));
    // Lanciata per il suo costo: alla sottofase finale resta in campo.
    pass_until(st, [](const GameState& s) { return s.turn.active_player == kRakdos; });
    CHECK(in_zone(st.s, kUw, Zone::Battlefield, "Starfield Shepherd"));
}

TEST_CASE("Starfield Shepherd: lanciata con il warp e uscita dal campo prima, resta dov'e'", "[uw][starfield][warp]") {
    Staged st = staged(kUw, {"Starfield Shepherd"}, {}, {"Plains", "Hallowed Fountain", "Arid Mesa"});
    to_main(st, kUw);
    cast(st, "Starfield Shepherd", CastMethod::Warp);
    resolve(st);
    act(st, option(kDeclineSearch));
    move_object(st.s, db_with_tokens(), find_on_battlefield(st.s, "Starfield Shepherd"), Zone::Graveyard, Cause::Test);
    require_invariants(st.s);
    pass_until(st, [](const GameState& s) { return s.turn.active_player == kRakdos; });
    CHECK(in_zone(st.s, kUw, Zone::Graveyard, "Starfield Shepherd"));
    CHECK_FALSE(in_zone(st.s, kUw, Zone::Exile, "Starfield Shepherd"));
}

// ---------------------------------------------------------------------------
// Sanctifier en-Vec — {W}{W} — Creature — Human Cleric 2/2
//   Protection from black and from red
//   When this creature enters, exile all cards that are black or red from all
//   graveyards.
//   If a black or red permanent, spell, or card not on the battlefield would be put
//   into a graveyard, exile it instead.
// La protezione ha i suoi test in test_combat.cpp. Le terre sono incolori: la fetch
// va nel cimitero anche con Sanctifier in campo.
// ---------------------------------------------------------------------------

TEST_CASE("Sanctifier en-Vec: entrando esilia le carte nere e rosse da tutti i cimiteri", "[uw][sanctifier]") {
    Staged st = staged(kUw, {"Sanctifier en-Vec"}, {}, {"Plains", "Plains"});
    to_graveyard(st.s, kRakdos,
                 {"Lightning Bolt", "Thoughtseize", "Nethergoyf", "Detective's Phoenix", "Bloodstained Mire",
                  "Mishra's Bauble"});
    to_graveyard(st.s, kUw, {"Solitude"});
    cast(st, "Sanctifier en-Vec");
    resolve(st);
    REQUIRE(in_zone(st.s, kUw, Zone::Battlefield, "Sanctifier en-Vec"));
    std::set<std::string> gy;
    for (const CardId id : st.s.players[kRakdos].graveyard) gy.insert(name_of(st.s, id));
    CHECK(gy == std::set<std::string>{"Bloodstained Mire", "Mishra's Bauble"});
    for (const char* name : {"Lightning Bolt", "Thoughtseize", "Nethergoyf", "Detective's Phoenix"}) {
        INFO(name);
        CHECK(in_zone(st.s, kRakdos, Zone::Exile, name));
    }
    CHECK(in_zone(st.s, kUw, Zone::Graveyard, "Solitude"));  // bianca: resta
    CHECK(st.s.players[kRakdos].gy.distinct_types() == 2);    // terra e artefatto
}

TEST_CASE("Sanctifier en-Vec: la fetch di Rakdos va nel cimitero, Thoughtseize in esilio", "[uw][sanctifier]") {
    Staged st = staged(kRakdos, {"Solitude"}, {"Thoughtseize"}, {"Sanctifier en-Vec"}, {"Bloodstained Mire", "Swamp"});
    act(st, find_action(st.s, ActionType::ActivateAbility, "Bloodstained Mire"));
    CHECK(in_zone(st.s, kRakdos, Zone::Graveyard, "Bloodstained Mire"));
    resolve(st);
    REQUIRE(is_decision(st.s, DecisionKind::SearchLibrary, kRakdos));
    act(st, option(kDeclineSearch));

    cast(st, "Thoughtseize");
    REQUIRE(is_decision(st.s, DecisionKind::ChooseTarget, kRakdos));
    act(st, Action{ActionType::TargetPlayer, kNoCard, kUw});
    pay_all(st);
    resolve(st);
    REQUIRE(is_decision(st.s, DecisionKind::ChooseFromHand, kRakdos));
    act(st, find_action(st.s, ActionType::ChooseCard, "Solitude"));
    CHECK(in_zone(st.s, kUw, Zone::Graveyard, "Solitude"));     // bianca: nel cimitero
    CHECK(in_zone(st.s, kRakdos, Zone::Exile, "Thoughtseize"));  // nera: in esilio
    CHECK_FALSE(in_zone(st.s, kRakdos, Zone::Graveyard, "Thoughtseize"));
    CHECK(st.s.players[kRakdos].gy.card_count == 1);
}

TEST_CASE("Sanctifier en-Vec: Moonshadow non perde segnalini per le carte esiliate invece che messe nel cimitero",
          "[uw][sanctifier][moonshadow]") {
    const bool with_sanctifier = GENERATE(true, false);
    INFO("Sanctifier in campo: " << with_sanctifier);
    Staged st = with_sanctifier
                    ? staged(kRakdos, {}, {"Street Wraith", "Stitcher's Supplier"}, {"Sanctifier en-Vec"},
                             {"Moonshadow", "Swamp"})
                    : staged(kRakdos, {}, {"Street Wraith", "Stitcher's Supplier"}, {}, {"Moonshadow", "Swamp"});
    st.s.objects[find_on_battlefield(st.s, "Moonshadow").card].counters.minus_one = 6;
    auto counters = [&] { return st.s.objects[find_on_battlefield(st.s, "Moonshadow").card].counters.minus_one; };

    // Ciclo di Street Wraith: scartare una carta creatura nera.
    act(st, find_action(st.s, ActionType::ActivateAbility, "Street Wraith"));
    resolve(st);
    CHECK(in_zone(st.s, kRakdos, with_sanctifier ? Zone::Exile : Zone::Graveyard, "Street Wraith"));
    CHECK(counters() == (with_sanctifier ? 6 : 5));

    // Macinare tre carte, una sola terra: un evento, al massimo un segnalino.
    stack_top(st.s, kRakdos, {"Thoughtseize", "Nethergoyf", "Bloodstained Mire"});
    const int before = counters();
    cast(st, "Stitcher's Supplier");
    resolve(st);
    CHECK(in_zone(st.s, kRakdos, Zone::Graveyard, "Bloodstained Mire"));  // incolore: nel cimitero in ogni caso
    CHECK(in_zone(st.s, kRakdos, with_sanctifier ? Zone::Exile : Zone::Graveyard, "Nethergoyf"));
    CHECK(counters() == before - 1);  // la terra basta a far scattare Moonshadow
}

TEST_CASE("Sanctifier en-Vec: il delirium resta bloccato", "[uw][sanctifier]") {
    const bool with_sanctifier = GENERATE(true, false);
    INFO("Sanctifier in campo: " << with_sanctifier);
    Staged st = with_sanctifier ? staged(kRakdos, {}, {"Lightning Bolt", "Street Wraith"}, {"Sanctifier en-Vec"},
                                         {"Blood Crypt"})
                                : staged(kRakdos, {}, {"Lightning Bolt", "Street Wraith"}, {}, {"Blood Crypt"});
    to_graveyard(st.s, kRakdos, {"Bloodstained Mire", "Mishra's Bauble"});  // terra e artefatto
    cast(st, "Lightning Bolt");
    REQUIRE(is_decision(st.s, DecisionKind::ChooseTarget, kRakdos));
    act(st, Action{ActionType::TargetPlayer, kNoCard, kUw});
    pay_all(st);
    resolve(st);
    act(st, find_action(st.s, ActionType::ActivateAbility, "Street Wraith"));
    resolve(st);
    const GraveyardSummary& gy = st.s.players[kRakdos].gy;
    CHECK(gy.distinct_types() == (with_sanctifier ? 2 : 4));
    CHECK(gy.delirium() == !with_sanctifier);
}

TEST_CASE("Sanctifier en-Vec: una creatura nera che muore insieme a Sanctifier va in esilio, senza trigger di morte",
          "[uw][sanctifier]") {
    Staged st = staged(kRakdos, {}, {}, {"Sanctifier en-Vec"}, {"Stitcher's Supplier", "Bloodstained Mire"});
    REQUIRE(st.s.pending.kind == DecisionKind::Priority);
    const std::size_t library = st.s.players[kRakdos].library.size();
    st.s.objects[find_on_battlefield(st.s, "Stitcher's Supplier").card].damage = 1;
    st.s.objects[find_on_battlefield(st.s, "Sanctifier en-Vec").card].damage = 2;
    act(st, pass());  // azioni di stato: muoiono insieme
    CHECK(in_zone(st.s, kUw, Zone::Graveyard, "Sanctifier en-Vec"));
    CHECK(in_zone(st.s, kRakdos, Zone::Exile, "Stitcher's Supplier"));
    CHECK(st.s.stack_items.empty());  // non è morta: niente macinata
    CHECK(st.s.players[kRakdos].library.size() == library);
    // Senza Sanctifier la sostituzione finisce.
    to_graveyard(st.s, kRakdos, {"Lightning Bolt"});
    CHECK(in_zone(st.s, kRakdos, Zone::Graveyard, "Lightning Bolt"));
}
