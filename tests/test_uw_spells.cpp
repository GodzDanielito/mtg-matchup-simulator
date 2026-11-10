// Test di Solitude, Ephemerate, Phelia, March of Otherworldly Light, Thraben Charm e Seam Rip.
// Testo Oracle da Scryfall (bulk oracle_cards aggiornato il 9/11/2026, 10:02 UTC), riportato
// prima dei test di ogni carta. Regole: Comprehensive Rules in vigore dal 6/11/2026
// (evocazione 702.74, rimbalzo 702.88, lampo 702.8, durate "until" 610.3, trigger ritardati
// 603.7c, oggetti nuovi 400.7, X 107.3, riduzioni di costo 118.7 e 601.2f, modali 700.2,
// "fino a un bersaglio" 115.6).

#include "test_helpers.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

using namespace mtg;
using namespace test;

namespace {

constexpr PlayerId kUw = 0;
constexpr PlayerId kRakdos = 1;
constexpr std::uint8_t kThrabenDamage = 0;
constexpr std::uint8_t kThrabenDestroy = 1;
constexpr std::uint8_t kThrabenGraveyards = 2;

std::string name_of(const GameState& s, CardId id) { return db_with_tokens().get(s.objects[id].def).name; }

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

void pay_all(Staged& st) {
    while (st.s.pending.kind == DecisionKind::PayMana) act(st, option(0));
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

int count_tokens(const GameState& s, PlayerId p) {
    return static_cast<int>(std::count_if(s.battlefield.begin(), s.battlefield.end(), [&](CardId id) {
        return s.objects[id].has_flag(ObjectFlag::Token) && s.objects[id].controller == p;
    }));
}

void to_graveyard(GameState& s, PlayerId p, std::initializer_list<const char*> names) {
    for (const char* name : names) {
        move_object(s, db_with_tokens(), find_cards(s, p, Zone::Library, name)[0], Zone::Graveyard, Cause::Test);
    }
    require_invariants(s);
}

// Nella decisione OrderTriggers, mette in pila per primo (si risolverà per ultimo) il trigger con quell'effetto.
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

void attack_with(Staged& st, std::initializer_list<std::string> names) {
    pass_until(st, [](const GameState& s) { return s.pending.kind == DecisionKind::DeclareAttacker; });
    while (st.s.pending.kind == DecisionKind::DeclareAttacker) {
        const std::string who = name_of(st.s, st.s.pending.subject.card);
        const bool attacks = std::find(names.begin(), names.end(), who) != names.end();
        const PlayerId defender = static_cast<PlayerId>(1 - st.s.turn.active_player);
        act(st, attacks ? Action{ActionType::TargetPlayer, kNoCard, defender} : option(kNoAttack));
    }
}

bool tapped(const GameState& s, ObjRef r) { return s.objects[r.card].has_flag(ObjectFlag::Tapped); }

}  // namespace

// ---------------------------------------------------------------------------
// Solitude — {3}{W}{W} — Creature — Elemental Incarnation 3/2
//   Flash
//   Lifelink
//   When this creature enters, exile up to one other target creature. That
//   creature's controller gains life equal to its power.
//   Evoke—Exile a white card from your hand.
// CR 702.74a: l'evocazione è un costo alternativo; "When this permanent enters, if its
// evoke cost was paid, its controller sacrifices it." I due trigger di entrata scattano
// insieme e il controllore sceglie l'ordine (603.3b).
// ---------------------------------------------------------------------------

TEST_CASE("Solitude: lampo, anche nel turno dell'avversario; evocazione solo con un'altra carta bianca",
          "[uw][solitude]") {
    SECTION("con un'altra carta bianca in mano") {
        Staged st = staged(kRakdos, {"Solitude", "Ocelot Pride"}, {},
                           {"Plains", "Plains", "Plains", "Hallowed Fountain", "Hallowed Fountain"});
        REQUIRE(st.s.turn.active_player == kRakdos);
        REQUIRE(is_decision(st.s, DecisionKind::Priority, kUw));
        CHECK(can_cast(st.s, "Solitude"));
        CHECK(can_cast(st.s, "Solitude", CastMethod::Evoke));
        CHECK(has_keyword(st.s, db_with_tokens(), find_cards(st.s, kUw, Zone::Hand, "Solitude")[0].card,
                          Keyword::Lifelink));
    }
    SECTION("senza: la terra non è bianca, e Solitude non esilia se stessa") {
        Staged st = staged(kRakdos, {"Solitude", "Plains"}, {}, {"Hallowed Fountain", "Arid Mesa"});
        REQUIRE(is_decision(st.s, DecisionKind::Priority, kUw));  // la fetch tiene la priorità nel turno di Rakdos
        CHECK_FALSE(can_cast(st.s, "Solitude", CastMethod::Evoke));
    }
}

TEST_CASE("Solitude: entrando esilia fino a un'altra creatura, e il suo controllore guadagna vita pari alla forza",
          "[uw][solitude]") {
    Staged st = staged(kUw, {"Solitude"}, {},
                       {"Plains", "Plains", "Plains", "Hallowed Fountain", "Hallowed Fountain", "Ocelot Pride"},
                       {"Nethergoyf"});
    to_graveyard(st.s, kRakdos, {"Lightning Bolt", "Bloodstained Mire", "Mishra's Bauble"});  // Nethergoyf 3/4
    act(st, cast_action(st.s, "Solitude"));
    pay_all(st);
    resolve(st);
    REQUIRE(is_decision(st.s, DecisionKind::ChooseTarget, kUw));
    CHECK(target_choices(st.s) == std::set<std::string>{"Nethergoyf", "Ocelot Pride"});  // non se stessa
    CHECK(has_option(st.s, kNoTarget));                                                   // "up to one"
    SECTION("esilia Nethergoyf: Rakdos guadagna 3 vita") {
        act(st, target(st.s, "Nethergoyf"));
        resolve(st);
        CHECK(in_zone(st.s, kRakdos, Zone::Exile, "Nethergoyf"));
        CHECK(st.s.players[kRakdos].life == 23);
        CHECK(st.s.players[kUw].life == 20);
    }
    SECTION("nessun bersaglio: non succede nulla") {
        act(st, option(kNoTarget));
        resolve(st);
        CHECK(in_zone(st.s, kRakdos, Zone::Battlefield, "Nethergoyf"));
        CHECK(in_zone(st.s, kUw, Zone::Battlefield, "Ocelot Pride"));
        CHECK(st.s.players[kRakdos].life == 20);
    }
}

TEST_CASE("Solitude evocata: si esilia una carta bianca dalla mano; sacrificio ed entrata insieme, ordine a scelta",
          "[uw][solitude][evoke]") {
    // Arid Mesa: un'opzione oltre a "passa", così la pila non si risolve da sola e si vede l'ordine.
    Staged st = staged(kUw, {"Solitude", "Ocelot Pride", "Guide of Souls"}, {}, {"Plains", "Arid Mesa"},
                       {"Stitcher's Supplier"});
    act(st, cast_action(st.s, "Solitude", CastMethod::Evoke));
    REQUIRE(is_decision(st.s, DecisionKind::ExileFromHandForCost, kUw));
    CHECK(card_choices(st.s) == std::set<std::string>{"Ocelot Pride", "Guide of Souls"});
    act(st, choose(st.s, "Ocelot Pride"));
    CHECK(in_zone(st.s, kUw, Zone::Exile, "Ocelot Pride"));
    CHECK_FALSE(tapped(st.s, find_on_battlefield(st.s, "Plains")));  // nessun mana
    resolve(st);
    REQUIRE(is_decision(st.s, DecisionKind::OrderTriggers, kUw));
    CHECK(legal(st.s).size() == 2);
    SECTION("sacrificio in cima: Solitude muore, l'esilio avviene lo stesso") {
        put_first(st, StackEffect::ExileOtherCreature);
        REQUIRE(is_decision(st.s, DecisionKind::ChooseTarget, kUw));
        act(st, target(st.s, "Stitcher's Supplier"));
        CHECK(st.s.stack_items.back().effect == StackEffect::EvokeSacrifice);
        resolve(st);
        CHECK(in_zone(st.s, kUw, Zone::Graveyard, "Solitude"));
        CHECK(in_zone(st.s, kRakdos, Zone::Exile, "Stitcher's Supplier"));
    }
    SECTION("entrata in cima: prima l'esilio, poi il sacrificio") {
        put_first(st, StackEffect::EvokeSacrifice);
        REQUIRE(is_decision(st.s, DecisionKind::ChooseTarget, kUw));
        act(st, target(st.s, "Stitcher's Supplier"));
        CHECK(st.s.stack_items.back().effect == StackEffect::ExileOtherCreature);
        resolve(st);
        CHECK(in_zone(st.s, kUw, Zone::Graveyard, "Solitude"));
        CHECK(in_zone(st.s, kRakdos, Zone::Exile, "Stitcher's Supplier"));
    }
}

TEST_CASE("Solitude evocata ed Ephemerate in risposta al sacrificio: resta in campo, l'entrata scatta due volte",
          "[uw][solitude][ephemerate][evoke]") {
    Staged st = staged(kUw, {"Solitude", "Ocelot Pride", "Ephemerate"}, {}, {"Plains", "Arid Mesa"},
                       {"Nethergoyf", "Stitcher's Supplier"});
    act(st, cast_action(st.s, "Solitude", CastMethod::Evoke));
    REQUIRE(is_decision(st.s, DecisionKind::ExileFromHandForCost, kUw));
    act(st, choose(st.s, "Ocelot Pride"));
    resolve(st);
    put_first(st, StackEffect::ExileOtherCreature);  // l'entrata in fondo, il sacrificio in cima
    act(st, target(st.s, "Nethergoyf"));
    REQUIRE(st.s.stack_items.size() == 2);
    REQUIRE(st.s.stack_items.back().effect == StackEffect::EvokeSacrifice);

    // In risposta al sacrificio: Ephemerate su Solitude.
    act(st, cast_action(st.s, "Ephemerate"));
    REQUIRE(is_decision(st.s, DecisionKind::ChooseTarget, kUw));
    CHECK(target_choices(st.s) == std::set<std::string>{"Solitude"});  // solo creature proprie
    act(st, target(st.s, "Solitude"));
    pay_all(st);
    resolve(st);
    // Solitude è un oggetto nuovo: il suo trigger di entrata scatta di nuovo (non evocata: niente sacrificio).
    REQUIRE(is_decision(st.s, DecisionKind::ChooseTarget, kUw));
    act(st, target(st.s, "Stitcher's Supplier"));
    CHECK(stack_count(st.s, StackEffect::ExileOtherCreature) == 2);
    CHECK(stack_count(st.s, StackEffect::EvokeSacrifice) == 1);
    resolve(st);
    CHECK(st.s.stack_items.empty());
    CHECK(in_zone(st.s, kUw, Zone::Battlefield, "Solitude"));  // il sacrificio non trova più l'oggetto
    CHECK(in_zone(st.s, kRakdos, Zone::Exile, "Nethergoyf"));
    CHECK(in_zone(st.s, kRakdos, Zone::Exile, "Stitcher's Supplier"));
    CHECK(in_zone(st.s, kUw, Zone::Exile, "Ephemerate"));  // lanciata dalla mano: rimbalzo
}

// ---------------------------------------------------------------------------
// Ephemerate — {W} — Instant
//   Exile target creature you control, then return it to the battlefield under its
//   owner's control.
//   Rebound (If you cast this spell from your hand, exile it as it resolves. At the
//   beginning of your next upkeep, you may cast this card from exile without paying
//   its mana cost.)
// CR 702.88a; 400.7: la creatura torna come oggetto nuovo. Un token che lascia il
// campo non torna (111.8).
// ---------------------------------------------------------------------------

TEST_CASE("Ephemerate: la creatura torna come oggetto nuovo e il suo trigger di entrata scatta di nuovo",
          "[uw][ephemerate]") {
    Staged st = staged(kUw, {"Ephemerate"}, {}, {"Plains", "Recruiter of the Guard"}, {"Nethergoyf"});
    const ObjRef old = find_on_battlefield(st.s, "Recruiter of the Guard");
    st.s.objects[old.card].counters.plus_one = 2;
    act(st, cast_action(st.s, "Ephemerate"));
    REQUIRE(is_decision(st.s, DecisionKind::ChooseTarget, kUw));
    CHECK(target_choices(st.s) == std::set<std::string>{"Recruiter of the Guard"});  // non Nethergoyf
    act(st, target(st.s, "Recruiter of the Guard"));
    pay_all(st);
    resolve(st);
    REQUIRE(is_decision(st.s, DecisionKind::SearchLibrary, kUw));  // di nuovo il trigger di entrata
    act(st, option(kSkipSearch));
    const ObjRef now = find_on_battlefield(st.s, "Recruiter of the Guard");
    CHECK(now != old);
    CHECK(st.s.objects[now.card].counters.plus_one == 0);
    CHECK(st.s.objects[now.card].has_flag(ObjectFlag::SummoningSick));
    CHECK(st.s.objects[now.card].controller == kUw);
    CHECK(in_zone(st.s, kUw, Zone::Exile, "Ephemerate"));
}

TEST_CASE("Ephemerate: rimbalzo, al prossimo mantenimento si rilancia dall'esilio senza pagare", "[uw][ephemerate]") {
    Staged st = staged(kUw, {"Ephemerate"}, {}, {"Plains", "Ocelot Pride", "Arid Mesa"});
    act(st, cast_action(st.s, "Ephemerate"));
    act(st, target(st.s, "Ocelot Pride"));
    pay_all(st);
    resolve(st);
    REQUIRE(in_zone(st.s, kUw, Zone::Exile, "Ephemerate"));
    // Nessuna decisione fino al mantenimento successivo di UW (nemmeno in quello di Rakdos).
    pass_until(st, [](const GameState& s) { return s.pending.kind == DecisionKind::CastFromExile; });
    CHECK(st.s.turn.active_player == kUw);
    CHECK(st.s.turn.step == Step::Upkeep);
    CHECK(st.s.turn.turn_number == 3);
    SECTION("la rilancia: senza pagare, poi va nel cimitero") {
        act(st, option(kCastIt));
        REQUIRE(is_decision(st.s, DecisionKind::ChooseTarget, kUw));
        act(st, target(st.s, "Ocelot Pride"));
        CHECK(st.s.pending.kind != DecisionKind::PayMana);
        CHECK_FALSE(tapped(st.s, find_on_battlefield(st.s, "Plains")));
        resolve(st);
        CHECK(in_zone(st.s, kUw, Zone::Graveyard, "Ephemerate"));  // non lanciata dalla mano: niente rimbalzo
    }
    SECTION("non la rilancia: resta in esilio, e non viene chiesto di nuovo") {
        act(st, option(kDeclineCast));
        pass_until(st, [](const GameState& s) {
            return s.turn.turn_number == 5 && s.turn.step == Step::Main1 && s.pending.kind == DecisionKind::Priority;
        });
        CHECK(in_zone(st.s, kUw, Zone::Exile, "Ephemerate"));
    }
}

TEST_CASE("Ephemerate: senza una propria creatura al mantenimento non si rilancia", "[uw][ephemerate]") {
    Staged st = staged(kUw, {"Ephemerate"}, {}, {"Plains", "Ocelot Pride", "Arid Mesa"});
    act(st, cast_action(st.s, "Ephemerate"));
    act(st, target(st.s, "Ocelot Pride"));
    pay_all(st);
    resolve(st);
    move_object(st.s, db_with_tokens(), find_on_battlefield(st.s, "Ocelot Pride"), Zone::Graveyard, Cause::Test);
    require_invariants(st.s);
    pass_until(st, [](const GameState& s) {
        return s.turn.turn_number == 3 && s.turn.step == Step::Main1 && s.pending.kind == DecisionKind::Priority;
    });
    CHECK(in_zone(st.s, kUw, Zone::Exile, "Ephemerate"));
}

TEST_CASE("Ephemerate: un token esiliato non torna", "[uw][ephemerate]") {
    Staged st{new_game(1), {}};
    put(st.s, kUw, {"Plains"}, Zone::Battlefield);
    put(st.s, kUw, {"Ephemerate"}, Zone::Hand);
    create_token(st.s, db_with_tokens(), db_with_tokens().id_of("Cat Token"), kUw);
    start_game(st.s, db_with_tokens(), StartOptions{kUw, false}, &st.log);
    act(st, cast_action(st.s, "Ephemerate"));
    act(st, target(st.s, "Cat Token"));
    pay_all(st);
    resolve(st);
    CHECK(count_tokens(st.s, kUw) == 0);
}

TEST_CASE("Ephemerate: con il bersaglio sparito si neutralizza e va nel cimitero, senza rimbalzo", "[uw][ephemerate]") {
    Staged st = staged(kUw, {"Ephemerate"}, {}, {"Plains", "Ocelot Pride", "Arid Mesa"});
    act(st, cast_action(st.s, "Ephemerate"));
    act(st, target(st.s, "Ocelot Pride"));
    pay_all(st);
    move_object(st.s, db_with_tokens(), find_on_battlefield(st.s, "Ocelot Pride"), Zone::Graveyard, Cause::Test);
    require_invariants(st.s);
    resolve(st);
    CHECK(in_zone(st.s, kUw, Zone::Graveyard, "Ephemerate"));
}

// ---------------------------------------------------------------------------
// Phelia, Exuberant Shepherd — {1}{W} — Legendary Creature — Dog 2/2
//   Flash
//   Whenever Phelia attacks, exile up to one other target nonland permanent. At the
//   beginning of the next end step, return that card to the battlefield under its
//   owner's control. If it entered under your control, put a +1/+1 counter on Phelia.
// CR 603.7c e 400.7: torna come oggetto nuovo; un token esiliato smette di esistere.
// Una Phoenix conferita torna come creatura: è conferita solo se lanciata così (702.103b).
// ---------------------------------------------------------------------------

TEST_CASE("Phelia: lampo; il bersaglio e' un altro permanente non terra, o nessuno", "[uw][phelia]") {
    SECTION("lampo") {
        Staged st = staged(kRakdos, {"Phelia, Exuberant Shepherd"}, {}, {"Plains", "Plains"});
        REQUIRE(is_decision(st.s, DecisionKind::Priority, kUw));
        CHECK(can_cast(st.s, "Phelia, Exuberant Shepherd"));
    }
    SECTION("bersagli") {
        Staged st = staged(kUw, {}, {}, {"Phelia, Exuberant Shepherd", "Plains", "Ocelot Pride"},
                           {"Nethergoyf", "Swamp", "Mishra's Bauble"});
        attack_with(st, {"Phelia, Exuberant Shepherd"});
        REQUIRE(is_decision(st.s, DecisionKind::ChooseTarget, kUw));
        CHECK(target_choices(st.s) == std::set<std::string>{"Ocelot Pride", "Nethergoyf", "Mishra's Bauble"});
        CHECK(has_option(st.s, kNoTarget));
    }
}

TEST_CASE("Phelia: un token esiliato non torna", "[uw][phelia]") {
    Staged st{new_game(1), {}};
    put(st.s, kUw, {"Phelia, Exuberant Shepherd", "Arid Mesa"}, Zone::Battlefield);
    create_token(st.s, db_with_tokens(), db_with_tokens().id_of("Cat Token"), kRakdos);
    start_game(st.s, db_with_tokens(), StartOptions{kUw, false}, &st.log);
    attack_with(st, {"Phelia, Exuberant Shepherd"});
    act(st, target(st.s, "Cat Token"));
    resolve(st);
    CHECK(count_tokens(st.s, kRakdos) == 0);
    pass_until(st, [](const GameState& s) { return s.turn.active_player == kRakdos; });
    CHECK(count_tokens(st.s, kRakdos) == 0);
    CHECK(st.s.objects[find_on_battlefield(st.s, "Phelia, Exuberant Shepherd").card].counters.plus_one == 0);
}

TEST_CASE("Phelia: Moonshadow esiliato torna come oggetto nuovo con sei segnalini -1/-1, sotto Rakdos",
          "[uw][phelia][moonshadow]") {
    Staged st = staged(kUw, {}, {}, {"Phelia, Exuberant Shepherd", "Arid Mesa"}, {"Moonshadow"});
    const ObjRef old = find_on_battlefield(st.s, "Moonshadow");
    st.s.objects[old.card].counters.minus_one = 2;
    attack_with(st, {"Phelia, Exuberant Shepherd"});
    act(st, target(st.s, "Moonshadow"));
    resolve(st);
    REQUIRE(in_zone(st.s, kRakdos, Zone::Exile, "Moonshadow"));
    pass_until(st, [](const GameState& s) { return s.turn.active_player == kRakdos; });
    const ObjRef now = find_on_battlefield(st.s, "Moonshadow");
    CHECK(now != old);
    CHECK(st.s.objects[now.card].counters.minus_one == 6);
    CHECK(st.s.objects[now.card].controller == kRakdos);
    // È entrata sotto il controllo di Rakdos: niente segnalino su Phelia.
    CHECK(st.s.objects[find_on_battlefield(st.s, "Phelia, Exuberant Shepherd").card].counters.plus_one == 0);
}

TEST_CASE("Phelia: una creatura di UW esiliata torna, rifa' il trigger di entrata e Phelia prende il segnalino",
          "[uw][phelia]") {
    Staged st = staged(kUw, {}, {}, {"Phelia, Exuberant Shepherd", "Recruiter of the Guard", "Arid Mesa"});
    attack_with(st, {"Phelia, Exuberant Shepherd"});
    act(st, target(st.s, "Recruiter of the Guard"));
    resolve(st);
    REQUIRE(in_zone(st.s, kUw, Zone::Exile, "Recruiter of the Guard"));
    pass_until(st, [](const GameState& s) { return s.pending.kind == DecisionKind::SearchLibrary; });
    CHECK(st.s.turn.step == Step::End);
    CHECK(in_zone(st.s, kUw, Zone::Battlefield, "Recruiter of the Guard"));
    act(st, option(kSkipSearch));
    CHECK(st.s.objects[find_on_battlefield(st.s, "Phelia, Exuberant Shepherd").card].counters.plus_one == 1);
}

TEST_CASE("Phelia: se Phelia non e' piu' in campo la carta torna, ma il segnalino non va da nessuna parte",
          "[uw][phelia]") {
    Staged st = staged(kUw, {}, {}, {"Phelia, Exuberant Shepherd", "Ocelot Pride", "Arid Mesa"});
    attack_with(st, {"Phelia, Exuberant Shepherd"});
    act(st, target(st.s, "Ocelot Pride"));
    resolve(st);
    move_object(st.s, db_with_tokens(), find_on_battlefield(st.s, "Phelia, Exuberant Shepherd"), Zone::Graveyard,
                Cause::Test);
    require_invariants(st.s);
    pass_until(st, [](const GameState& s) { return s.turn.active_player == kRakdos; });
    CHECK(in_zone(st.s, kUw, Zone::Battlefield, "Ocelot Pride"));
}

TEST_CASE("Phelia: una Phoenix conferita esiliata torna come creatura", "[uw][phelia][phoenix]") {
    Staged st{new_game(1), {}};
    put(st.s, kUw, {"Phelia, Exuberant Shepherd", "Arid Mesa"}, Zone::Battlefield);
    put(st.s, kRakdos, {"Stitcher's Supplier", "Detective's Phoenix"}, Zone::Battlefield);
    const ObjRef host = find_on_battlefield(st.s, "Stitcher's Supplier");
    CardObject& phoenix = st.s.objects[find_on_battlefield(st.s, "Detective's Phoenix").card];
    phoenix.set_flag(ObjectFlag::Bestowed);
    phoenix.attached_to = host;
    start_game(st.s, db_with_tokens(), StartOptions{kUw, false}, &st.log);
    require_invariants(st.s);
    attack_with(st, {"Phelia, Exuberant Shepherd"});
    act(st, target(st.s, "Detective's Phoenix"));
    resolve(st);
    REQUIRE(in_zone(st.s, kRakdos, Zone::Exile, "Detective's Phoenix"));
    pass_until(st, [](const GameState& s) { return s.turn.active_player == kRakdos; });
    const CardObject& back = st.s.objects[find_on_battlefield(st.s, "Detective's Phoenix").card];
    CHECK_FALSE(back.has_flag(ObjectFlag::Bestowed));
    CHECK(back.attached_to.is_none());
    CHECK(back.controller == kRakdos);
    CHECK(power(st.s, db_with_tokens(), find_on_battlefield(st.s, "Detective's Phoenix").card) == 2);
    CHECK(power(st.s, db_with_tokens(), host.card) == 1);  // il Supplier non ha più l'Aura
}

// ---------------------------------------------------------------------------
// March of Otherworldly Light — {X}{W} — Instant
//   As an additional cost to cast this spell, you may exile any number of white cards
//   from your hand. This spell costs {2} less to cast for each card exiled this way.
//   Exile target artifact, creature, or enchantment with mana value X or less.
// CR 107.3a: X si annuncia lanciando; 118.7a: una riduzione di mana generico tocca solo
// la parte generica ({X}), mai il {W}; 601.2f: non sotto {0}.
// ---------------------------------------------------------------------------

TEST_CASE("March of Otherworldly Light: X e bersagli con valore di mana X o meno", "[uw][march]") {
    Staged st = staged(kUw, {"March of Otherworldly Light"}, {}, {"Plains", "Plains", "Plains"},
                       {"Mishra's Bauble", "Nethergoyf", "Fear of Missing Out", "Swamp"});
    act(st, cast_action(st.s, "March of Otherworldly Light"));
    REQUIRE(is_decision(st.s, DecisionKind::ChooseX, kUw));
    CHECK(option_values(st.s) == std::set<int>{0, 1, 2});  // tre Plains: {2}{W} al massimo
    act(st, option(1));
    REQUIRE(is_decision(st.s, DecisionKind::ChooseTarget, kUw));
    CHECK(target_choices(st.s) == std::set<std::string>{"Mishra's Bauble", "Nethergoyf"});
    act(st, target(st.s, "Nethergoyf"));
    pay_all(st);
    resolve(st);
    CHECK(in_zone(st.s, kRakdos, Zone::Exile, "Nethergoyf"));
    CHECK(in_zone(st.s, kUw, Zone::Graveyard, "March of Otherworldly Light"));
    int tapped_plains = 0;
    for (const CardId id : st.s.battlefield) {
        if (name_of(st.s, id) == "Plains" && st.s.objects[id].has_flag(ObjectFlag::Tapped)) ++tapped_plains;
    }
    CHECK(tapped_plains == 2);  // {1}{W}
}

TEST_CASE("March of Otherworldly Light: ogni carta bianca esiliata dalla mano toglie {2}, mai il {W}",
          "[uw][march]") {
    SECTION("una Plains e due carte bianche: X fino a 4, bersagli fino a 3") {
        Staged st = staged(kUw, {"March of Otherworldly Light", "Ocelot Pride", "Guide of Souls"}, {}, {"Plains"},
                           {"Moonshadow", "Detective's Phoenix", "Troll of Khazad-dûm"});
        act(st, cast_action(st.s, "March of Otherworldly Light"));
        REQUIRE(is_decision(st.s, DecisionKind::ChooseX, kUw));
        // Solo gli X pari al valore di mana di un bersaglio: X = 2 avrebbe gli stessi bersagli di
        // X = 1 a un costo maggiore (dominato). Il Troll vorrebbe X = 6: troppo caro.
        CHECK(option_values(st.s) == std::set<int>{1, 3});
        act(st, option(3));
        act(st, target(st.s, "Detective's Phoenix"));
        // {3}{W} con una Plains: servono entrambe le carte bianche (le scelte obbligate avvengono da sole).
        while (st.s.pending.kind == DecisionKind::ExileFromHandForCost) {
            CHECK_FALSE(has_option(st.s, kStopExiling));
            act(st, legal(st.s).front());
        }
        pay_all(st);
        resolve(st);
        CHECK(in_zone(st.s, kRakdos, Zone::Exile, "Detective's Phoenix"));
        CHECK(in_zone(st.s, kUw, Zone::Exile, "Ocelot Pride"));
        CHECK(in_zone(st.s, kUw, Zone::Exile, "Guide of Souls"));
        CHECK(tapped(st.s, find_on_battlefield(st.s, "Plains")));
    }
    SECTION("senza terre non si lancia: il {W} resta") {
        Staged st = staged(kUw, {"March of Otherworldly Light", "Ocelot Pride", "Guide of Souls"}, {}, {"Arid Mesa"},
                           {"Moonshadow"});
        CHECK_FALSE(can_cast(st.s, "March of Otherworldly Light"));
    }
    SECTION("si esiliano solo le carte che servono: con il mana per pagare si puo' fermarsi") {
        Staged st = staged(kUw, {"March of Otherworldly Light", "Ocelot Pride", "Guide of Souls"}, {},
                           {"Plains", "Plains", "Plains"}, {"Moonshadow"});
        act(st, cast_action(st.s, "March of Otherworldly Light"));
        // Un solo bersaglio (valore di mana 1): X = 1 è una scelta obbligata e avviene da sola.
        REQUIRE(is_decision(st.s, DecisionKind::ChooseTarget, kUw));
        CHECK(st.s.stack_items.back().x == 1);
        act(st, target(st.s, "Moonshadow"));
        REQUIRE(is_decision(st.s, DecisionKind::ExileFromHandForCost, kUw));
        CHECK(has_option(st.s, kStopExiling));
        CHECK(card_choices(st.s) == std::set<std::string>{"Ocelot Pride", "Guide of Souls"});
        act(st, choose(st.s, "Ocelot Pride"));
        // X = 1: una carta toglie già tutto il generico, una seconda non servirebbe.
        CHECK(st.s.pending.kind != DecisionKind::ExileFromHandForCost);
        pay_all(st);
        resolve(st);
        CHECK(in_zone(st.s, kRakdos, Zone::Exile, "Moonshadow"));
        CHECK(in_zone(st.s, kUw, Zone::Hand, "Guide of Souls"));
        int tapped_plains = 0;
        for (const CardId id : st.s.battlefield) {
            if (name_of(st.s, id) == "Plains" && st.s.objects[id].has_flag(ObjectFlag::Tapped)) ++tapped_plains;
        }
        CHECK(tapped_plains == 1);  // solo il {W}
    }
}

// ---------------------------------------------------------------------------
// Thraben Charm — {1}{W} — Instant
//   Choose one —
//   • Thraben Charm deals damage equal to twice the number of creatures you control to
//     target creature.
//   • Destroy target enchantment.
//   • Exile any number of target players' graveyards.
// CR 700.2a: si sceglie il modo lanciando, solo tra quelli con bersagli legali.
// ---------------------------------------------------------------------------

TEST_CASE("Thraben Charm: si offrono solo i modi con bersagli legali", "[uw][thraben]") {
    SECTION("creature e incantesimi in campo: tre modi") {
        Staged st = staged(kUw, {"Thraben Charm"}, {}, {"Plains", "Plains", "Ocelot Pride"}, {"Fear of Missing Out"});
        act(st, cast_action(st.s, "Thraben Charm"));
        REQUIRE(is_decision(st.s, DecisionKind::ChooseMode, kUw));
        CHECK(option_values(st.s) == std::set<int>{kThrabenDamage, kThrabenDestroy, kThrabenGraveyards});
    }
    SECTION("campo vuoto: resta solo l'esilio dei cimiteri, scelto da solo") {
        Staged st = staged(kUw, {"Thraben Charm"}, {}, {"Plains", "Plains"});
        act(st, cast_action(st.s, "Thraben Charm"));
        REQUIRE(is_decision(st.s, DecisionKind::ChooseTarget, kUw));
        // Un'opzione per ogni insieme non vuoto di giocatori: bit del giocatore.
        CHECK(option_values(st.s) == std::set<int>{1, 2, 3});
    }
}

TEST_CASE("Thraben Charm: danno pari al doppio delle proprie creature", "[uw][thraben]") {
    Staged st = staged(kUw, {"Thraben Charm"}, {}, {"Plains", "Plains", "Ocelot Pride", "Guide of Souls"},
                       {"Troll of Khazad-dûm"});
    act(st, cast_action(st.s, "Thraben Charm"));
    act(st, option(kThrabenDamage));
    CHECK(target_choices(st.s) == std::set<std::string>{"Ocelot Pride", "Guide of Souls", "Troll of Khazad-dûm"});
    act(st, target(st.s, "Troll of Khazad-dûm"));
    pay_all(st);
    resolve(st);
    CHECK(st.s.objects[find_on_battlefield(st.s, "Troll of Khazad-dûm").card].damage == 4);
    CHECK(in_zone(st.s, kUw, Zone::Graveyard, "Thraben Charm"));
}

TEST_CASE("Thraben Charm: distrugge un incantesimo, anche proprio", "[uw][thraben]") {
    Staged st = staged(kUw, {"Thraben Charm"}, {}, {"Plains", "Plains", "Seam Rip"},
                       {"Fear of Missing Out", "Nethergoyf"});
    act(st, cast_action(st.s, "Thraben Charm"));
    act(st, option(kThrabenDestroy));
    CHECK(target_choices(st.s) == std::set<std::string>{"Fear of Missing Out", "Seam Rip"});
    act(st, target(st.s, "Fear of Missing Out"));
    pay_all(st);
    resolve(st);
    CHECK(in_zone(st.s, kRakdos, Zone::Graveyard, "Fear of Missing Out"));
}

TEST_CASE("Thraben Charm: esilia i cimiteri dei giocatori bersaglio", "[uw][thraben]") {
    Staged st = staged(kUw, {"Thraben Charm"}, {}, {"Plains", "Plains"});
    to_graveyard(st.s, kRakdos, {"Lightning Bolt", "Bloodstained Mire", "Nethergoyf"});
    to_graveyard(st.s, kUw, {"Solitude"});
    act(st, cast_action(st.s, "Thraben Charm"));
    REQUIRE(is_decision(st.s, DecisionKind::ChooseTarget, kUw));
    act(st, option(static_cast<std::uint8_t>(1u << kRakdos)));
    pay_all(st);
    resolve(st);
    CHECK(st.s.players[kRakdos].graveyard.empty());
    CHECK(st.s.players[kRakdos].gy.card_count == 0);
    CHECK(in_zone(st.s, kRakdos, Zone::Exile, "Nethergoyf"));
    CHECK(in_zone(st.s, kUw, Zone::Graveyard, "Solitude"));
    CHECK(in_zone(st.s, kUw, Zone::Graveyard, "Thraben Charm"));
}

// ---------------------------------------------------------------------------
// Seam Rip — {W} — Enchantment
//   When this enchantment enters, exile target nonland permanent an opponent controls
//   with mana value 2 or less until this enchantment leaves the battlefield.
// CR 610.3: quando Seam Rip lascia il campo, il permanente torna (sotto il controllo
// del proprietario, 610.3c) come oggetto nuovo. 610.3b: se Seam Rip ha già lasciato il
// campo quando il trigger si risolve, il permanente non si muove affatto.
// ---------------------------------------------------------------------------

TEST_CASE("Seam Rip: esilia un permanente non terra dell'avversario di valore di mana 2 o meno", "[uw][seamrip]") {
    Staged st = staged(kUw, {"Seam Rip"}, {}, {"Plains", "Ocelot Pride"},
                       {"Fear of Missing Out", "Nethergoyf", "Mishra's Bauble", "Detective's Phoenix", "Swamp"});
    to_main(st, kUw);
    act(st, cast_action(st.s, "Seam Rip"));
    pay_all(st);
    resolve(st);
    REQUIRE(is_decision(st.s, DecisionKind::ChooseTarget, kUw));
    CHECK(target_choices(st.s) == std::set<std::string>{"Fear of Missing Out", "Nethergoyf", "Mishra's Bauble"});
    act(st, target(st.s, "Fear of Missing Out"));
    resolve(st);
    CHECK(in_zone(st.s, kRakdos, Zone::Exile, "Fear of Missing Out"));
    CHECK(in_zone(st.s, kUw, Zone::Battlefield, "Seam Rip"));
}

TEST_CASE("Seam Rip: se lascia il campo prima che il trigger si risolva, il permanente non viene esiliato",
          "[uw][seamrip][thraben]") {
    Staged st = staged(kUw, {"Seam Rip", "Thraben Charm"}, {}, {"Plains", "Plains", "Plains"},
                       {"Fear of Missing Out"});
    to_main(st, kUw);
    const ObjRef fomo = find_on_battlefield(st.s, "Fear of Missing Out");
    act(st, cast_action(st.s, "Seam Rip"));
    pay_all(st);
    resolve(st);
    act(st, target(st.s, "Fear of Missing Out"));
    REQUIRE(stack_count(st.s, StackEffect::ExileUntilLeaves) == 1);
    // In risposta: Thraben Charm distrugge Seam Rip.
    act(st, cast_action(st.s, "Thraben Charm"));
    act(st, option(kThrabenDestroy));
    act(st, target(st.s, "Seam Rip"));
    pay_all(st);
    resolve(st);
    CHECK(in_zone(st.s, kUw, Zone::Graveyard, "Seam Rip"));
    CHECK(find_on_battlefield(st.s, "Fear of Missing Out") == fomo);  // mai mosso: stesso oggetto
}

TEST_CASE("Seam Rip: se lascia il campo dopo, il permanente torna come oggetto nuovo", "[uw][seamrip][thraben]") {
    Staged st = staged(kUw, {"Seam Rip", "Thraben Charm"}, {}, {"Plains", "Plains", "Plains", "Arid Mesa"}, {"Moonshadow"});
    to_main(st, kUw);
    const ObjRef old = find_on_battlefield(st.s, "Moonshadow");
    st.s.objects[old.card].counters.minus_one = 3;
    act(st, cast_action(st.s, "Seam Rip"));
    pay_all(st);
    resolve(st);
    act(st, target(st.s, "Moonshadow"));
    resolve(st);
    REQUIRE(in_zone(st.s, kRakdos, Zone::Exile, "Moonshadow"));

    act(st, cast_action(st.s, "Thraben Charm"));
    act(st, option(kThrabenDestroy));
    act(st, target(st.s, "Seam Rip"));
    pay_all(st);
    resolve(st);
    CHECK(in_zone(st.s, kUw, Zone::Graveyard, "Seam Rip"));
    const ObjRef now = find_on_battlefield(st.s, "Moonshadow");
    CHECK(now != old);
    CHECK(st.s.objects[now.card].controller == kRakdos);
    CHECK(st.s.objects[now.card].counters.minus_one == 6);
    CHECK(st.s.objects[now.card].has_flag(ObjectFlag::SummoningSick));
}

// ---------------------------------------------------------------------------
// Witch Enchanter // Witch-Blessed Meadow in mano: fuori dal campo e dalla pila una
// bifronte ha solo le caratteristiche del fronte (Creature — Human Warlock, {3}{W}),
// quindi è una carta bianca, anche se il retro è una terra incolore.
// ---------------------------------------------------------------------------

TEST_CASE("Witch Enchanter in mano e' una carta bianca: si esilia per evocare Solitude e per ridurre March",
          "[uw][witch][solitude][march]") {
    SECTION("evocazione di Solitude") {
        Staged st = staged(kUw, {"Solitude", "Witch Enchanter"}, {}, {"Arid Mesa"});
        REQUIRE(can_cast(st.s, "Solitude", CastMethod::Evoke));
        act(st, cast_action(st.s, "Solitude", CastMethod::Evoke));  // una sola carta bianca: scelta obbligata
        CHECK(in_zone(st.s, kUw, Zone::Exile, "Witch Enchanter"));
        REQUIRE(st.s.stack_items.size() == 1);
        CHECK(st.s.stack_items.back().method == CastMethod::Evoke);
    }
    SECTION("una terra in mano non basta: Plains non e' bianca") {
        Staged st = staged(kUw, {"Solitude", "Plains"}, {}, {"Arid Mesa"});
        CHECK_FALSE(can_cast(st.s, "Solitude", CastMethod::Evoke));
    }
    SECTION("riduzione di March of Otherworldly Light") {
        // Phoenix ha valore di mana 3: {3}{W} con due Plains si paga solo esiliando una carta bianca.
        // Arid Mesa: un'opzione oltre a "passa", cosi' la magia resta in pila per i controlli.
        Staged st = staged(kUw, {"March of Otherworldly Light", "Witch Enchanter"}, {},
                           {"Plains", "Plains", "Arid Mesa"}, {"Detective's Phoenix"});
        act(st, cast_action(st.s, "March of Otherworldly Light"));
        REQUIRE(is_decision(st.s, DecisionKind::ChooseTarget, kUw));
        CHECK(st.s.stack_items.back().x == 3);
        act(st, target(st.s, "Detective's Phoenix"));
        pay_all(st);
        CHECK(in_zone(st.s, kUw, Zone::Exile, "Witch Enchanter"));
        CHECK(st.s.stack_items.back().exiled_for_cost == 1);
        resolve(st);
        CHECK(in_zone(st.s, kRakdos, Zone::Exile, "Detective's Phoenix"));
    }
    SECTION("senza Witch Enchanter March non arriva a X = 3") {
        Staged st = staged(kUw, {"March of Otherworldly Light", "Plains"}, {}, {"Plains", "Plains"},
                           {"Detective's Phoenix"});
        CHECK_FALSE(can_cast(st.s, "March of Otherworldly Light"));
    }
}
